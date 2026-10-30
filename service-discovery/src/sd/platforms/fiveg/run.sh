#!/usr/bin/env bash
#
# run.sh -- run one discovery protocol over the virtual 5G MBS testbed:
#   advertise    in each UE netns on oaitun_ue1 (UE1 camera, UE2 thermostat, UE3 camera)
#   bridge ue    per UE: MBS payload on udp 5001/5002/5003 -> protocol multicast on oaitun_ue1, replies uplink
#   bridge as    in the AF/AS netns: protocol multicast -> MBS GTP-U, uplinked replies -> discover
#   discover     in the AF/AS netns
# See bridge.py for the path. Every process runs from the host venv ('uv sync' in the repo).
#
# Needs a running stack with an active session (./docs/virtual-mbs.sh up && activate).
# Usage:
#   ./run.sh <sssd|mdns|zenoh>              tmux demo
#   ./run.sh <sssd|mdns|zenoh> --test 30    headless: run 30 s, check discover found exactly the
#                                           SERVICE_TYPE UEs (by their oaitun_ue1 address), exit 0/1
# Env overrides:
#   E2E_ROOT          testbed checkout (default: the e2e5mbs folder next to this repo)
#   GROUP, TEID       MBS tunnel to send on (default: latest "Joined multicast group" in the gNB log)
#   GNB_LOG           gNB log to read the tunnel from
#   SESSION=sd-<protocol>  tmux session name
#   SERVICE_TYPE=camera    service type discover looks for
#   LOG_DIR=/tmp/sd-5g/<protocol>  logs for --test
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
PROTOCOL="${1:-}"
[[ "$PROTOCOL" =~ ^(sssd|mdns|zenoh)$ ]] || { echo "usage: $0 <sssd|mdns|zenoh> [--test SECONDS]" >&2; exit 2; }
TEST_SECONDS=""
if [[ "${2:-}" == "--test" ]]; then
  TEST_SECONDS="${3:-30}"
fi

ROOT="${E2E_ROOT:-$REPO/../e2e5mbs}"
GNB_LOG="${GNB_LOG:-$ROOT/logs/ocudu-gnb/ocudu_gnb_mbs.log}"
SESSION="${SESSION:-sd-$PROTOCOL}"
SERVICE_TYPE="${SERVICE_TYPE:-camera}"
LOG_DIR="${LOG_DIR:-/tmp/sd-5g/$PROTOCOL}"
AF_CONTAINER="${AF_CONTAINER:-test_mbs_af_as}"
AS_ADDR=10.33.33.10
PY="$REPO/.venv/bin/python"
UES=(oai_nrue_1 oai_nrue_2 oai_nrue_3)
TYPES=(camera thermostat camera)
PORTS=(5001 5002 5003)  # OAI_NRUE_MBS_UDP per UE
KILL_PATTERN='[s]d\.(protocols|platforms\.fiveg\.bridge)'

die() { echo "ERROR: $*" >&2; exit 1; }
pid_of() { docker inspect -f '{{.State.Pid}}' "$1"; }

[[ -x "$PY" ]] || die "$PY not found; run 'uv sync' in $REPO"
[[ -n "$TEST_SECONDS" ]] || command -v tmux >/dev/null || die "tmux is not installed (sudo apt install tmux)"
docker ps --format '{{.Names}}' | grep -qx "$AF_CONTAINER" || die "$AF_CONTAINER is not running; start the stack first"

if [[ -z "${GROUP:-}" || -z "${TEID:-}" ]]; then
  [[ -f "$GNB_LOG" ]] || die "gNB log not found: $GNB_LOG (set E2E_ROOT?)"
  tunnel=$(grep -o "Joined multicast group [0-9.]*:2152 teid=0x[0-9a-fA-F]*" "$GNB_LOG" | tail -n 1 |
    sed -E 's/.*group ([0-9.]+):2152 teid=(0x[0-9a-fA-F]+)/\1 \2/' || true)
  [[ -n "$tunnel" ]] || die "no active MBS tunnel in $GNB_LOG; run ./docs/virtual-mbs.sh activate"
  read -r GROUP TEID <<<"$tunnel"
fi
echo "==> $PROTOCOL over MBS tunnel: group=$GROUP teid=$TEID"

AF_PID=$(pid_of "$AF_CONTAINER")
AF_IFACE=$(sudo nsenter -t "$AF_PID" -n ip -o -4 addr show | awk -v a="$AS_ADDR/" 'index($4, a) == 1 {print $2}')
[[ -n "$AF_IFACE" ]] || die "no interface with $AS_ADDR in $AF_CONTAINER"

# The protocols' multicast needs a multicast-capable interface; it dies at the UPF (TTL 1), the bridge carries it.
declare -A UE_ADDR
for ue in "${UES[@]}"; do
  pid=$(pid_of "$ue") || die "$ue is not running"
  addr=$(sudo nsenter -t "$pid" -n ip -o -4 addr show oaitun_ue1 2>/dev/null | awk '{sub("/.*", "", $4); print $4}')
  [[ -n "$addr" ]] || die "$ue has no oaitun_ue1 (no PDU session yet?)"
  UE_ADDR[$ue]=$addr
  sudo nsenter -t "$pid" -n ip link set oaitun_ue1 multicast on
  sudo nsenter -t "$pid" -n ip route replace 224.0.0.0/4 dev oaitun_ue1
  echo "==> $ue: oaitun_ue1 $addr"
done

# Only one source per session: stop the demo senders and any previous run (the bridges hold udp 5001-5003).
docker exec "$AF_CONTAINER" sh -c "pkill -f '[m]bs_(text|video)_stream.py|[q]uerier.py'; true"
sudo pkill -f "$KILL_PATTERN" || true

cmds=()
for i in "${!UES[@]}"; do
  cmds+=("sudo nsenter -t $(pid_of "${UES[$i]}") -n '$PY' -u -m sd.protocols.$PROTOCOL advertise --service-type ${TYPES[$i]} --name ue$((i + 1)) --iface oaitun_ue1")
done
for i in "${!UES[@]}"; do
  cmds+=("sudo '$PY' -u -m sd.platforms.fiveg.bridge ue --protocol $PROTOCOL --port ${PORTS[$i]} --ue ${UES[$i]} --as-addr $AS_ADDR")
done
cmds+=("sudo nsenter -t $AF_PID -n '$PY' -u -m sd.platforms.fiveg.bridge as --protocol $PROTOCOL --mbs-group $GROUP --teid $TEID --interface $AS_ADDR")
discover="sudo nsenter -t $AF_PID -n '$PY' -u -m sd.protocols.$PROTOCOL discover --iface $AF_IFACE --service-type $SERVICE_TYPE"
names=(advertise_ue1 advertise_ue2 advertise_ue3 bridge_ue1 bridge_ue2 bridge_ue3 bridge_as)

if [[ -n "$TEST_SECONDS" ]]; then
  mkdir -p "$LOG_DIR"
  for i in "${!cmds[@]}"; do
    bash -c "${cmds[$i]}" >"$LOG_DIR/${names[$i]}.log" 2>&1 &
  done
  sleep 2
  echo "==> Discovering for $TEST_SECONDS s (logs in $LOG_DIR)"
  bash -c "$discover --duration $TEST_SECONDS" >"$LOG_DIR/discover.log" 2>&1 || true
  # Zenoh can hang in shutdown once discover is gone, so force-kill stragglers.
  sudo pkill -f "$KILL_PATTERN" || true
  sleep 1
  sudo pkill -9 -f "$KILL_PATTERN" || true
  wait 2>/dev/null || true

  want=()
  for i in "${!UES[@]}"; do
    [[ "${TYPES[$i]}" == "$SERVICE_TYPE" ]] && want+=("${UE_ADDR[${UES[$i]}]}")
  done
  exec "$PY" -m sd.common.check "$LOG_DIR/discover.log" "${want[@]}"
fi

if tmux has-session -t "$SESSION" 2>/dev/null; then
  echo "==> Replacing existing tmux session '$SESSION'"
  tmux kill-session -t "$SESSION"
fi

tmux new-session -d -s "$SESSION" -n demo "${cmds[0]}; read -p '${names[0]} exited, press enter'"
for i in $(seq 1 $((${#cmds[@]} - 1))); do
  tmux split-window -t "$SESSION:demo" "${cmds[$i]}; read -p '${names[$i]} exited, press enter'"
  tmux select-layout -t "$SESSION:demo" tiled >/dev/null
done
tmux split-window -t "$SESSION:demo" "sleep 2; $discover; read -p 'discover exited, press enter'"
tmux select-layout -t "$SESSION:demo" tiled >/dev/null
tmux set-option -t "$SESSION" mouse on >/dev/null

if [[ -n "${TMUX:-}" ]]; then
  tmux switch-client -t "$SESSION"
else
  exec tmux attach -t "$SESSION"
fi

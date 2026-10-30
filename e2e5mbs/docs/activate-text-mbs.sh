#!/usr/bin/env bash
#
# activate-text-mbs.sh -- text analogue of activate-live-mbs.sh.
# Creates a fresh MBS broadcast session through MB-SMF, waits for the gNB to
# join the assigned multicast tunnel, then starts the TEXT sender
# (mbs_text_stream.py) on that exact group/TEID and verifies G-RNTI scheduling.
#
# Env overrides:
#   TEXT_PREFIX="testMsg"   message is "<prefix> <N> Tx_ns=<timestamp>"
#   TEXT_INTERVAL=1         seconds between messages
#   TEXT_DURATION=1800      total broadcast duration (s); 30 minutes
#   TEXT_START=1            first message number N
set -euo pipefail

AF_CONTAINER="${AF_CONTAINER:-test_mbs_af_as}"
GNB_LOG="${GNB_LOG:-/tmp/ocudu_gnb_sdr.log}"
TEXT_PREFIX="${TEXT_PREFIX:-testMsg}"
TEXT_INTERVAL="${TEXT_INTERVAL:-1}"
TEXT_DURATION="${TEXT_DURATION:-1800}"
TEXT_START="${TEXT_START:-1}"
ACTIVATE_TIMEOUT="${ACTIVATE_TIMEOUT:-30}"
VERIFY_TIMEOUT="${VERIFY_TIMEOUT:-30}"

die() { echo "ERROR: $*" >&2; exit 1; }
require_cmd() { command -v "$1" >/dev/null 2>&1 || die "missing required command: $1"; }

wait_for_pattern() {
  local timeout=$1 offset=$2 pattern=$3 started now
  started=$(date +%s)
  while true; do
    if tail -c +"$offset" "$GNB_LOG" 2>/dev/null | grep -E "$pattern" >/dev/null; then
      return 0
    fi
    now=$(date +%s)
    (( now - started >= timeout )) && return 1
    sleep 1
  done
}

extract_latest_tunnel() {
  local offset=$1
  tail -c +"$offset" "$GNB_LOG" 2>/dev/null |
    sed -n -E \
      -e 's/.*NG-U shared multicast tunnel \{Addr=([0-9.]+) TEID=(0x[0-9a-fA-F]+)\}.*/\1 \2/p' \
      -e 's/.*Joined multicast group ([0-9.]+):2152 teid=(0x[0-9a-fA-F]+).*/\1 \2/p' |
    tail -n 1
}

# Stop any running sender (video OR text) so only one MBS source exists.
stop_existing_senders() {
  docker exec "$AF_CONTAINER" sh -lc '
    pids=$(ps -eo pid,args | awk "/[m]bs_(video|text)_stream.py/ {print \$1}")
    if [ -n "$pids" ]; then kill $pids 2>/dev/null || true; sleep 1; fi
    pids=$(ps -eo pid,args | awk "/[m]bs_(video|text)_stream.py/ {print \$1}")
    if [ -n "$pids" ]; then kill -9 $pids 2>/dev/null || true; fi
    true
  '
}

start_text_sender() {
  local group=$1 teid=$2
  docker exec -d "$AF_CONTAINER" sh -lc \
    "exec python3 /test/tools/mbs_text_stream.py --group '$group' --teid '$teid' \
       --prefix '$TEXT_PREFIX' --interval '$TEXT_INTERVAL' --duration '$TEXT_DURATION' \
       --start-index '$TEXT_START' >/tmp/mbs_text_stream.log 2>&1"
}

text_sender_count() {
  docker exec "$AF_CONTAINER" sh -lc "ps -eo args | awk '/[m]bs_text_stream.py/ {c++} END {print c + 0}'"
}

require_cmd docker
require_cmd grep
require_cmd sed
[[ -f "$GNB_LOG" ]] || die "gNB log not found: $GNB_LOG"

start_offset=$(($(stat -c %s "$GNB_LOG") + 1))

echo "==> Creating a fresh MBS broadcast session through MB-SMF..."
docker exec "$AF_CONTAINER" sh -lc \
  "cd /test && python3 -m unittest -q MB_SMF.MBSSessionServiceAPI.MBSSessionServiceOperation.test_create_and_release_mbs_session"

echo "==> Waiting for gNB MBS tunnel assignment..."
if ! wait_for_pattern "$ACTIVATE_TIMEOUT" "$start_offset" 'Joined multicast group'; then
  die "gNB did not join an MBS multicast group within ${ACTIVATE_TIMEOUT}s. Check AMF gNB-N2 state and $GNB_LOG"
fi

tunnel=$(extract_latest_tunnel "$start_offset")
[[ -n "$tunnel" ]] || die "could not parse multicast group/TEID from $GNB_LOG"
read -r group teid <<<"$tunnel"

echo "==> Active MBS tunnel: group=$group teid=$teid"
echo "==> Starting AF/AS TEXT sender on the active tunnel..."
stop_existing_senders
start_text_sender "$group" "$teid"

count=$(text_sender_count)
[[ "$count" == "1" ]] || die "expected exactly one text sender, found $count"

verify_offset=$(($(stat -c %s "$GNB_LOG") + 1))
echo "==> Verifying live MTCH/G-RNTI scheduling (text every ${TEXT_INTERVAL}s)..."
if ! wait_for_pattern "$VERIFY_TIMEOUT" "$verify_offset" 'MTCH: rnti=0xfe01|PDCCH: rnti=0xfe01|PDSCH: rnti=0xfe01'; then
  die "text sender started but gNB did not emit G-RNTI PDCCH within ${VERIFY_TIMEOUT}s"
fi

echo "OK: TEXT MBS is live on $group / $teid -- broadcasting '$TEXT_PREFIX N' every ${TEXT_INTERVAL}s for ${TEXT_DURATION}s."
echo "    Sender log (in $AF_CONTAINER): /tmp/mbs_text_stream.log"

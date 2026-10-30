#!/usr/bin/env bash
set -euo pipefail

AF_CONTAINER="${AF_CONTAINER:-test_mbs_af_as}"
GNB_LOG="${GNB_LOG:-/tmp/ocudu_gnb_sdr.log}"
MEDIA_FILE="${MEDIA_FILE:-/test/media/demo.ts}"
BITRATE_KBPS="${BITRATE_KBPS:-220}"
ACTIVATE_TIMEOUT="${ACTIVATE_TIMEOUT:-30}"
VERIFY_TIMEOUT="${VERIFY_TIMEOUT:-20}"

die() {
  echo "ERROR: $*" >&2
  exit 1
}

require_cmd() {
  command -v "$1" >/dev/null 2>&1 || die "missing required command: $1"
}

wait_for_pattern() {
  local timeout=$1
  local offset=$2
  local pattern=$3
  local started now

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

stop_existing_sender() {
  docker exec "$AF_CONTAINER" sh -lc '
    pids=$(ps -eo pid,args | awk "/[m]bs_video_stream.py/ {print \$1}")
    if [ -n "$pids" ]; then
      kill $pids 2>/dev/null || true
      sleep 1
    fi
    pids=$(ps -eo pid,args | awk "/[m]bs_video_stream.py/ {print \$1}")
    if [ -n "$pids" ]; then
      kill -9 $pids 2>/dev/null || true
    fi
  '
}

start_sender() {
  local group=$1
  local teid=$2

  docker exec -d "$AF_CONTAINER" sh -lc \
    "exec python3 /test/tools/mbs_video_stream.py --input '$MEDIA_FILE' --group '$group' --teid '$teid' --bitrate-kbps '$BITRATE_KBPS' --loop >/tmp/mbs_video_stream.log 2>&1"
}

sender_count() {
  docker exec "$AF_CONTAINER" sh -lc \
    "ps -eo args | awk '/[m]bs_video_stream.py/ {count++} END {print count + 0}'"
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
echo "==> Restarting AF/AS media sender on the active tunnel..."
stop_existing_sender
start_sender "$group" "$teid"

count=$(sender_count)
[[ "$count" == "1" ]] || die "expected exactly one media sender, found $count"

verify_offset=$(($(stat -c %s "$GNB_LOG") + 1))
echo "==> Verifying live MTCH/G-RNTI scheduling..."
if ! wait_for_pattern "$VERIFY_TIMEOUT" "$verify_offset" 'MTCH: rnti=0xfe01|PDCCH: rnti=0xfe01|PDSCH: rnti=0xfe01'; then
  die "media sender started but gNB did not emit G-RNTI PDCCH within ${VERIFY_TIMEOUT}s"
fi

echo "OK: MBS is live on $group / $teid with one media sender and gNB emitting rnti=0xfe01."

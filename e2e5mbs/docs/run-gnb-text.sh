#!/bin/bash
#
# run-gnb-text.sh -- start the OCUDU gNB and broadcast TEXT messages over MBS
# (instead of the default video). It reuses run-gnb.sh for all the gNB startup
# and real-time tuning, but disables the video auto-activation and instead runs
# the text activation (activate-text-mbs.sh -> mbs_text_stream.py), which sends
# "<prefix> <N> Tx_ns=<timestamp>" every TEXT_INTERVAL seconds for
# TEXT_DURATION seconds.
#
# Usage:  sudo ./run-gnb-text.sh
# Env (pass AFTER sudo, e.g. `sudo TEXT_DURATION=1800 ./run-gnb-text.sh`):
#   TEXT_PREFIX="testMsg"   message is "<prefix> <N> Tx_ns=<timestamp>"
#   TEXT_INTERVAL=1         seconds between messages
#   TEXT_DURATION=1800      total broadcast duration (s) -- 30 minutes
#   TEXT_START=1            first message number N
#   ACTIVATE_DELAY=2        grace seconds before watching for the AMF connection
#   ACTIVATE_READY_TIMEOUT=90
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUN_GNB="$SCRIPT_DIR/run-gnb.sh"
ACTIVATE_TEXT="$SCRIPT_DIR/activate-text-mbs.sh"
GNB_LOG="/tmp/ocudu_gnb_sdr.log"

[[ -x "$RUN_GNB" ]]      || { echo "ERROR: $RUN_GNB not found/executable" >&2; exit 1; }
[[ -x "$ACTIVATE_TEXT" ]]|| { echo "ERROR: $ACTIVATE_TEXT not found/executable" >&2; exit 1; }

run_user="${SUDO_USER:-$USER}"
ACTIVATE_DELAY="${ACTIVATE_DELAY:-2}"
ACTIVATE_READY_TIMEOUT="${ACTIVATE_READY_TIMEOUT:-90}"
TEXT_PREFIX="${TEXT_PREFIX:-testMsg}"
TEXT_INTERVAL="${TEXT_INTERVAL:-1}"
TEXT_DURATION="${TEXT_DURATION:-1800}"
TEXT_START="${TEXT_START:-1}"

activate_log="/tmp/activate-text-mbs.$(date +%Y%m%d-%H%M%S).log"

# Helper: wait for the freshly-started gNB to connect to the AMF, then run the
# text activation as the invoking user. (run-gnb.sh archives + recreates the log
# below, so this waits on the new log just like run-gnb.sh's own helper.)
helper="$(cat <<HELPER
set -u
echo '==> [auto-activate-text] waiting ${ACTIVATE_DELAY}s for the gNB to spin up...'
sleep ${ACTIVATE_DELAY}
echo '==> [auto-activate-text] waiting (<=${ACTIVATE_READY_TIMEOUT}s) for gNB to connect to the AMF...'
for _i in \$(seq 1 ${ACTIVATE_READY_TIMEOUT}); do
  grep -aqE 'Connected to AMF|Rx PDU: NGSetupResponse' '${GNB_LOG}' 2>/dev/null && break
  sleep 1
done
if ! grep -aqE 'Connected to AMF|Rx PDU: NGSetupResponse' '${GNB_LOG}' 2>/dev/null; then
  echo 'ERROR: [auto-activate-text] gNB did not connect to the AMF in time; not activating.' >&2
  exit 1
fi
echo '==> [auto-activate-text] gNB connected; running activate-text-mbs.sh...'
TEXT_PREFIX='${TEXT_PREFIX}' TEXT_INTERVAL='${TEXT_INTERVAL}' TEXT_DURATION='${TEXT_DURATION}' TEXT_START='${TEXT_START}' '${ACTIVATE_TEXT}'
HELPER
)"

echo "==> Auto-activating TEXT MBS in the background as '$run_user'."
echo "    Broadcast: '$TEXT_PREFIX N' every ${TEXT_INTERVAL}s for ${TEXT_DURATION}s"
echo "    Log:         $activate_log"
echo "    Follow with: tail -f $activate_log"
# Double-fork so the helper is reparented to init (not the gNB's sudo) -- avoids
# the SIGCHLD/pty freeze of the foreground gNB console.
( setsid sudo -u "$run_user" bash -lc "$helper" >"$activate_log" 2>&1 & )

# Hand off to run-gnb.sh for gNB startup + RT tuning, but with the VIDEO
# auto-activation disabled (we do text activation above instead).
echo "==> Starting gNB via run-gnb.sh (video auto-activate disabled; text instead)..."
export AUTO_ACTIVATE=0
exec "$RUN_GNB"

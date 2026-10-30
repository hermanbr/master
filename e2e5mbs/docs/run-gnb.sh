#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GNB_BIN="$(readlink -f "$SCRIPT_DIR/../OCUDU-gNB/build/apps/gnb/gnb")"
GNB_CFG="$SCRIPT_DIR/ocudu-host-gnb-b210.yml"

if [[ ! -x "$GNB_BIN" ]]; then
  echo "ERROR: gnb binary not found at $GNB_BIN" >&2
  exit 1
fi
if [[ ! -f "$GNB_CFG" ]]; then
  echo "ERROR: gnb config not found at $GNB_CFG" >&2
  exit 1
fi

find_existing_gnb_pids() {
  local pid command

  while read -r pid; do
    [[ -n "$pid" ]] || continue
    command="$(tr '\0' '\n' < "/proc/$pid/cmdline" 2>/dev/null | head -n 1 || true)"
    [[ -n "$command" ]] && command="$(readlink -f "$command")"
    [[ "$command" == "$GNB_BIN" ]] && printf '%s\n' "$pid"
  done < <(pgrep -x gnb || true)
}

wait_for_gnb_exit() {
  local timeout=$1
  shift

  for ((i = 0; i < timeout * 10; i++)); do
    local alive=0 pid state
    for pid in "$@"; do
      state="$(awk '{print $3}' "/proc/$pid/stat" 2>/dev/null || true)"
      if [[ -n "$state" && "$state" != "Z" ]]; then
        alive=1
        break
      fi
    done
    (( alive == 0 )) && return 0
    sleep 0.1
  done
  return 1
}

mapfile -t existing_gnb_pids < <(find_existing_gnb_pids)
if (( ${#existing_gnb_pids[@]} > 0 )); then
  echo "==> Stopping existing OCUDU gNB process(es): ${existing_gnb_pids[*]}..."

  # A shell-stopped process cannot handle SIGINT until it is continued.
  sudo kill -CONT "${existing_gnb_pids[@]}" 2>/dev/null || true
  sudo kill -INT "${existing_gnb_pids[@]}" 2>/dev/null || true

  if ! wait_for_gnb_exit 5 "${existing_gnb_pids[@]}"; then
    echo "    Graceful shutdown timed out; sending SIGTERM..."
    sudo kill -TERM "${existing_gnb_pids[@]}" 2>/dev/null || true
  fi

  if ! wait_for_gnb_exit 3 "${existing_gnb_pids[@]}"; then
    echo "    Process still running; sending SIGKILL..."
    sudo kill -KILL "${existing_gnb_pids[@]}" 2>/dev/null || true
    wait_for_gnb_exit 2 "${existing_gnb_pids[@]}" || {
      echo "ERROR: unable to stop the existing OCUDU gNB." >&2
      exit 1
    }
  fi

  echo "    Existing gNB stopped."
fi

# Rotate stale runtime files before the root-launched gNB opens them. On systems
# with fs.protected_regular enabled, reopening a user-owned file in /tmp from a
# privileged process can fail even when its mode bits appear writable.
runtime_timestamp="$(date +%Y%m%d-%H%M%S)"
for runtime_file in \
  /tmp/ocudu_gnb_sdr.log \
  /tmp/ocudu_gnb_sdr_mac.pcap \
  /tmp/ocudu_gnb_sdr_ngap.pcap; do
  if [[ -e "$runtime_file" ]]; then
    archived_file="${runtime_file}.${runtime_timestamp}"
    echo "==> Archiving $runtime_file as $archived_file..."
    sudo mv -- "$runtime_file" "$archived_file"
  fi
done

# Disable deep CPU C-states (C1E and higher) to prevent USB SDR timing jitter.
# Keeps only POLL (state0) and C1 (state1) active.
echo "==> Disabling deep CPU C-states (C1E and higher)..."
for cpu in /sys/devices/system/cpu/cpu[0-9]*; do
  for state in "$cpu"/cpuidle/state[2-9] "$cpu"/cpuidle/state1[0-9]; do
    [[ -f "$state/disable" ]] && echo 1 | sudo tee "$state/disable" >/dev/null
  done
done
echo "    Done."

# Set network buffers (improves USB throughput consistency under load).
echo "==> Tuning network buffers..."
sudo sysctl -qw net.core.wmem_max=33554432
sudo sysctl -qw net.core.rmem_max=33554432
sudo sysctl -qw net.core.wmem_default=33554432
sudo sysctl -qw net.core.rmem_default=33554432
echo "    Done."

# Stop irqbalance and pin all IRQs to CPUs 0-3,8-11.
# This leaves physical cores 4-7 (CPUs 4-7,12-15) free of interrupt handling
# so gNB's RT threads are never preempted by NIC/USB/timer IRQs.
echo "==> Pinning IRQs to CPUs 0-3,8-11 (freeing cores 4-7 for gNB)..."
sudo systemctl stop irqbalance 2>/dev/null || true
for irq_dir in /proc/irq/[0-9]*/; do
  irq=$(basename "$irq_dir")
  [[ "$irq" == "0" ]] && continue
  echo "0-3,8-11" | sudo tee "$irq_dir/smp_affinity_list" >/dev/null 2>&1 || true
done
echo "    Done."

# Auto-run activate-live-mbs.sh after the gNB comes up, so the MBS session and
# media stream do not have to be started by hand. The gNB is launched in the
# foreground via exec below, so this helper is spawned (detached) first; it
# waits ACTIVATE_DELAY seconds, then waits for the gNB to connect to the AMF
# (a flat 2s is too early -- NG Setup lands ~4s after start), then activates.
#   AUTO_ACTIVATE=0            disable this entirely
#   ACTIVATE_DELAY=2           grace seconds before watching for readiness
#   ACTIVATE_READY_TIMEOUT=90  max seconds to wait for the AMF connection
#   BITRATE_KBPS=40            passed through (use `sudo -E` or `sudo VAR=...`)
AUTO_ACTIVATE="${AUTO_ACTIVATE:-1}"
ACTIVATE_DELAY="${ACTIVATE_DELAY:-2}"
ACTIVATE_READY_TIMEOUT="${ACTIVATE_READY_TIMEOUT:-90}"
ACTIVATE_SCRIPT="$SCRIPT_DIR/activate-live-mbs.sh"
ACTIVATE_GNB_LOG="/tmp/ocudu_gnb_sdr.log"

launch_auto_activate() {
  local run_user="${SUDO_USER:-$USER}"
  local activate_log="/tmp/activate-live-mbs.${runtime_timestamp}.log"

  if [[ ! -x "$ACTIVATE_SCRIPT" ]]; then
    echo "==> WARNING: $ACTIVATE_SCRIPT not found/executable; skipping auto-activate." >&2
    return 0
  fi

  # Body that waits for the gNB <-> AMF connection, then runs activation.
  local helper
  helper="$(cat <<HELPER
set -u
echo '==> [auto-activate] waiting ${ACTIVATE_DELAY}s for the gNB to spin up...'
sleep ${ACTIVATE_DELAY}
echo '==> [auto-activate] waiting (<=${ACTIVATE_READY_TIMEOUT}s) for gNB to connect to the AMF...'
for _i in \$(seq 1 ${ACTIVATE_READY_TIMEOUT}); do
  grep -aqE 'Connected to AMF|Rx PDU: NGSetupResponse' '${ACTIVATE_GNB_LOG}' 2>/dev/null && break
  sleep 1
done
if ! grep -aqE 'Connected to AMF|Rx PDU: NGSetupResponse' '${ACTIVATE_GNB_LOG}' 2>/dev/null; then
  echo 'ERROR: [auto-activate] gNB did not connect to the AMF in time; not activating.' >&2
  echo '       Start it by hand once the gNB is up:  BITRATE_KBPS=${BITRATE_KBPS:-40} ${ACTIVATE_SCRIPT}' >&2
  exit 1
fi
echo '==> [auto-activate] gNB connected; running activate-live-mbs.sh...'
BITRATE_KBPS='${BITRATE_KBPS:-40}' '${ACTIVATE_SCRIPT}'
HELPER
)"

  # ALWAYS run activation detached, writing to a log file. This is reliable
  # under sudo, headless, and on a desktop. (A previous version committed to a
  # GUI terminal and returned without falling back, so when the terminal failed
  # to launch -- e.g. gnome-terminal under sudo missing DBus -- activation
  # silently never ran and left no log.)
  echo "==> Auto-activating MBS in the background as '$run_user'."
  echo "    Log:         $activate_log"
  echo "    Follow with: tail -f $activate_log"
  # Double-fork via a throwaway subshell so the helper is reparented to init,
  # not to the gNB's sudo. (We exec into `sudo ... gnb` below; a child left
  # parented to it would, on exit, deliver SIGCHLD to sudo's pty handler --
  # "handle_sigchld_pty: waitpid: No child processes" -- and freeze the gNB
  # console even though the radio keeps running.)
  ( setsid sudo -u "$run_user" bash -lc "$helper" >"$activate_log" 2>&1 & )

  # Best-effort bonus: if a desktop session is reachable, also open a terminal
  # that just tails that log so the output is visible in its own window. This
  # is non-fatal -- if it fails, activation still proceeds in the background
  # above. gnome-terminal needs DBUS_SESSION_BUS_ADDRESS and XDG_RUNTIME_DIR to
  # reach gnome-terminal-server, so preserve those too.
  if [[ -n "${DISPLAY:-}" ]]; then
    local term watch_cmd
    watch_cmd="echo 'Following auto-activation log: $activate_log'; tail -n +1 -f '$activate_log'"
    for term in x-terminal-emulator gnome-terminal konsole xfce4-terminal xterm; do
      command -v "$term" >/dev/null 2>&1 || continue
      echo "==> Also opening a $term window to follow the activation log..."
      case "$term" in
        gnome-terminal)
          ( setsid sudo -u "$run_user" \
            --preserve-env=DISPLAY,XAUTHORITY,DBUS_SESSION_BUS_ADDRESS,XDG_RUNTIME_DIR \
            "$term" --title="activate-live-mbs" -- bash -lc "$watch_cmd" >/dev/null 2>&1 & )
          ;;
        *)
          ( setsid sudo -u "$run_user" \
            --preserve-env=DISPLAY,XAUTHORITY,DBUS_SESSION_BUS_ADDRESS,XDG_RUNTIME_DIR \
            "$term" -e bash -lc "$watch_cmd" >/dev/null 2>&1 & )
          ;;
      esac
      break
    done
  fi
}

if [[ "$AUTO_ACTIVATE" == "1" ]]; then
  launch_auto_activate
else
  echo "==> AUTO_ACTIVATE=0; not auto-running activate-live-mbs.sh."
fi

# Launch gnb with SCHED_FIFO priority 50, restricted to physical cores 4-7
# (logical CPUs 4-7 and their HT siblings 12-15).  This keeps gNB threads off
# the same physical cores as Xorg/IRQ handlers without needing isolcpus.
echo "==> Starting gnb (SCHED_FIFO 50, CPUs 4-7,12-15)..."
exec sudo taskset -c 4-7,12-15 chrt -f 50 "$GNB_BIN" -c "$GNB_CFG"

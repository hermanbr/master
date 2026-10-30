#!/bin/bash
set -euo pipefail

ue_tun="${UE_PDU_TUN:-tun_srsue}"
pdu_gateway="${UE_PDU_GATEWAY:-10.45.0.1}"
check_interval="${UE_PDU_CHECK_INTERVAL:-30}"
max_failures="${UE_PDU_MAX_FAILURES:-3}"

child_pid=""

stop_child()
{
    if [ -n "${child_pid}" ] && kill -0 "${child_pid}" 2>/dev/null; then
        kill -TERM "${child_pid}" 2>/dev/null || true
        wait "${child_pid}" 2>/dev/null || true
    fi
}

trap 'stop_child; exit 143' TERM INT

(
    cd /usr/local/bin
    ./entrypoint.sh "$@"
) &
child_pid="$!"

failures=0
while kill -0 "${child_pid}" 2>/dev/null; do
    if ip -4 addr show dev "${ue_tun}" 2>/dev/null | grep -q 'inet '; then
        if ping -I "${ue_tun}" -c 1 -W 2 "${pdu_gateway}" >/dev/null 2>&1; then
            failures=0
        else
            failures=$((failures + 1))
            echo "UE PDU watchdog: ${pdu_gateway} unreachable via ${ue_tun} (${failures}/${max_failures})" >&2
            if [ "${failures}" -ge "${max_failures}" ]; then
                echo "UE PDU watchdog: stale PDU tunnel detected; restarting UE container" >&2
                stop_child
                exit 1
            fi
        fi
    else
        failures=0
    fi

    sleep "${check_interval}" &
    wait "$!" || true
done

wait "${child_pid}"

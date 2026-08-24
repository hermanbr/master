#!/usr/bin/env bash
# End-to-end health check for the Open5GS + UERANSIM testbed.
# Reads hosts/users/keys straight from ansible/inventory.ini, so it stays
# correct after every `terraform apply` regenerates that file.
#
# Usage: ./scripts/check_5g_system.sh

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INVENTORY="${SCRIPT_DIR}/../ansible/inventory.ini"

SSH_OPTS=(-o StrictHostKeyChecking=no -o ConnectTimeout=8 -o BatchMode=yes -o LogLevel=ERROR)

PASS=0
FAIL=0
WARN=0

ok()   { printf "  [OK]   %s\n" "$1"; PASS=$((PASS+1)); }
bad()  { printf "  [FAIL] %s\n" "$1"; FAIL=$((FAIL+1)); }
warn() { printf "  [WARN] %s\n" "$1"; WARN=$((WARN+1)); }

remote() {
  # remote <ip> <user> <key> <command...>
  local ip="$1" user="$2" key="$3"
  shift 3
  ssh "${SSH_OPTS[@]}" -i "$key" "${user}@${ip}" "$@" 2>/dev/null
}

# --- Parse inventory.ini into per-group entries ---------------------------
# (plain indexed arrays only — macOS ships bash 3.2, no associative arrays)

section=""
CORE_LINE=""
GNB_LINE=""
UE_LINES=()

while IFS= read -r raw_line; do
  line="${raw_line%%#*}"
  line="$(echo "$line" | xargs)"
  [[ -z "$line" ]] && continue
  if [[ "$line" =~ ^\[(.+)\]$ ]]; then
    section="${BASH_REMATCH[1]}"
    continue
  fi
  ip=$(awk '{print $1}' <<<"$line")
  user=$(sed -n 's/.*ansible_user=\([^ ]*\).*/\1/p' <<<"$line")
  key=$(sed -n 's/.*ansible_ssh_private_key_file=\([^ ]*\).*/\1/p' <<<"$line")
  key="${key/#\~/$HOME}"
  [[ -z "$user" ]] && user="ubuntu"
  entry="${ip}|${user}|${key}"
  case "$section" in
    open5gs) CORE_LINE="$entry" ;;
    ueransim_gnb) GNB_LINE="$entry" ;;
    ueransim_ue) UE_LINES+=("$entry") ;;
  esac
done < "$INVENTORY"

if [[ -z "$CORE_LINE" || -z "$GNB_LINE" || ${#UE_LINES[@]} -eq 0 ]]; then
  echo "Could not parse inventory.ini (expected [open5gs], [ueransim_gnb], [ueransim_ue] groups). Aborting."
  exit 1
fi

IFS='|' read -r CORE_IP CORE_USER CORE_KEY <<<"$CORE_LINE"
IFS='|' read -r GNB_IP GNB_USER GNB_KEY <<<"$GNB_LINE"

echo "== 1. Open5GS core ($CORE_IP) =="

amf_state=$(remote "$CORE_IP" "$CORE_USER" "$CORE_KEY" "systemctl is-active open5gs-amfd")
[[ "$amf_state" == "active" ]] && ok "open5gs-amfd is active" || bad "open5gs-amfd is not active ($amf_state)"

upf_state=$(remote "$CORE_IP" "$CORE_USER" "$CORE_KEY" "systemctl is-active open5gs-upfd")
[[ "$upf_state" == "active" ]] && ok "open5gs-upfd is active" || bad "open5gs-upfd is not active ($upf_state)"

ngap_addr=$(remote "$CORE_IP" "$CORE_USER" "$CORE_KEY" "sudo awk '/ngap:/{f=1} f&&/address:/{print \$NF; exit}' /etc/open5gs/amf.yaml")
if [[ "$ngap_addr" == "$CORE_IP" ]]; then
  ok "AMF NGAP bound to real IP ($ngap_addr)"
else
  bad "AMF NGAP bound to '$ngap_addr', expected $CORE_IP — gNB cannot reach it"
fi

gtpu_addr=$(remote "$CORE_IP" "$CORE_USER" "$CORE_KEY" "sudo awk '/gtpu:/{f=1} f&&/address:/{print \$NF; exit}' /etc/open5gs/upf.yaml")
if [[ "$gtpu_addr" == "$CORE_IP" ]]; then
  ok "UPF GTP-U bound to real IP ($gtpu_addr)"
else
  bad "UPF GTP-U bound to '$gtpu_addr', expected $CORE_IP — UPF unreachable for N3"
fi

sub_count=$(remote "$CORE_IP" "$CORE_USER" "$CORE_KEY" "open5gs-dbctl showall 2>/dev/null | grep -c imsi")
[[ "${sub_count:-0}" -ge 1 ]] && ok "$sub_count subscriber(s) present in DB" || bad "No subscribers found in DB"

echo
echo "== 2. UERANSIM gNB ($GNB_IP) =="

gnb_state=$(remote "$GNB_IP" "$GNB_USER" "$GNB_KEY" "systemctl is-active nr-gnb")
[[ "$gnb_state" == "active" ]] && ok "nr-gnb service is active" || bad "nr-gnb service is not active ($gnb_state)"

gnb_name=$(remote "$GNB_IP" "$GNB_USER" "$GNB_KEY" "cd ~/UERANSIM && ./build/nr-cli --dump 2>/dev/null | head -n1")
if [[ -n "$gnb_name" ]]; then
  ok "gNB instance found: $gnb_name"
  amf_list=$(remote "$GNB_IP" "$GNB_USER" "$GNB_KEY" "cd ~/UERANSIM && ./build/nr-cli '$gnb_name' --exec amf-list 2>/dev/null")
  if [[ -n "$amf_list" ]]; then
    ok "gNB has NGAP association with AMF:"
    echo "$amf_list" | sed 's/^/         /'
  else
    bad "gNB reports no AMF association (NG Setup did not complete)"
  fi
else
  bad "nr-cli could not find a running gNB instance"
fi

echo
echo "== 3. UERANSIM UE(s) =="

declare -a UE_IPS_ASSIGNED=()
declare -a UE_SUPIS=()
declare -a UE_TUN_IFS=()

for entry in "${UE_LINES[@]}"; do
  IFS='|' read -r ue_ip ue_user ue_key <<<"$entry"
  echo "-- UE $ue_ip --"

  ue_state=$(remote "$ue_ip" "$ue_user" "$ue_key" "systemctl is-active nr-ue")
  [[ "$ue_state" == "active" ]] && ok "nr-ue service is active" || bad "nr-ue service is not active ($ue_state)"

  supi=$(remote "$ue_ip" "$ue_user" "$ue_key" "grep '^supi:' ~/UERANSIM/config/ue.yaml | awk '{print \$2}'")
  ok "configured SUPI: ${supi:-unknown}"
  UE_SUPIS+=("$supi")

  tun_line=$(remote "$ue_ip" "$ue_user" "$ue_key" "ip -4 -o addr show 2>/dev/null | grep uesimtun")
  if [[ -n "$tun_line" ]]; then
    tun_if=$(awk '{print $2}' <<<"$tun_line")
    tun_ip=$(awk '{print $4}' <<<"$tun_line" | cut -d/ -f1)
    ok "PDU session up: $tun_if = $tun_ip"
    UE_IPS_ASSIGNED+=("$tun_ip")
    UE_TUN_IFS+=("$tun_if")
  else
    bad "No uesimtun* interface — PDU session establishment failed"
    UE_IPS_ASSIGNED+=("")
    UE_TUN_IFS+=("")
  fi
done

# Duplicate-SUPI check: two UEs with the same identity can't both hold a
# session against the core at once.
if [[ ${#UE_SUPIS[@]} -ge 2 && "${UE_SUPIS[0]}" == "${UE_SUPIS[1]}" && -n "${UE_SUPIS[0]}" ]]; then
  warn "Both UEs are configured with the SAME SUPI (${UE_SUPIS[0]}) — the core will not keep two simultaneous sessions for one subscriber. Give each ueransim_ue host its own IMSI/key/opc and matching open5gs-dbctl entry."
fi

echo
echo "== 4. UE <-> UE ping (data plane, via UPF) =="

if [[ ${#UE_LINES[@]} -ge 2 && -n "${UE_IPS_ASSIGNED[0]:-}" && -n "${UE_IPS_ASSIGNED[1]:-}" ]]; then
  IFS='|' read -r ue1_ip ue1_user ue1_key <<<"${UE_LINES[0]}"
  IFS='|' read -r ue2_ip ue2_user ue2_key <<<"${UE_LINES[1]}"

  echo "-- ${ue1_ip} (${UE_IPS_ASSIGNED[0]}) -> ${ue2_ip} (${UE_IPS_ASSIGNED[1]}) --"
  out1=$(remote "$ue1_ip" "$ue1_user" "$ue1_key" "sudo ping -I ${UE_TUN_IFS[0]} -c 4 -W 2 ${UE_IPS_ASSIGNED[1]}")
  if grep -q " 0% packet loss" <<<"$out1"; then
    ok "ping succeeded ($(grep 'packets transmitted' <<<"$out1"))"
  else
    bad "ping failed or had loss"
    echo "$out1" | sed 's/^/         /'
  fi

  echo "-- ${ue2_ip} (${UE_IPS_ASSIGNED[1]}) -> ${ue1_ip} (${UE_IPS_ASSIGNED[0]}) --"
  out2=$(remote "$ue2_ip" "$ue2_user" "$ue2_key" "sudo ping -I ${UE_TUN_IFS[1]} -c 4 -W 2 ${UE_IPS_ASSIGNED[0]}")
  if grep -q " 0% packet loss" <<<"$out2"; then
    ok "ping succeeded ($(grep 'packets transmitted' <<<"$out2"))"
  else
    bad "ping failed or had loss"
    echo "$out2" | sed 's/^/         /'
  fi
else
  warn "Skipping UE<->UE ping — need at least 2 UEs with an active PDU session (see section 3 above)."
fi

echo
echo "================= SUMMARY ================="
echo "  PASS: $PASS   FAIL: $FAIL   WARN: $WARN"
[[ $FAIL -eq 0 ]] && echo "  Overall: system looks healthy." || echo "  Overall: issues found, see [FAIL] lines above."
exit $(( FAIL > 0 ? 1 : 0 ))

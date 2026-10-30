#!/usr/bin/env bash
#
# virtual-mbs.sh -- run the all-software MBS testbed: 5G-MAG core + OCUDU gNB +
# ZMQ broker + N x OAI nrUE (compose-files/internal/docker-compose.virtual-mbs.yaml).
#
#   ./virtual-mbs.sh build       build the OAI nrUE image (oai-nr-ue:mbs-local), gNB and broker
#   ./virtual-mbs.sh up          start core, gNB, broker and UEs; provision subscribers
#   ./virtual-mbs.sh down        stop everything (ZMQ links cannot reconnect: always restart as a whole)
#   ./virtual-mbs.sh activate    create an MBS session and start the text sender (MODE=video for video)
#   ./virtual-mbs.sh logs <svc>  follow a container's logs
#
# Env overrides:
#   MODE=text|video     sender used by "activate" (default text); other vars pass through to
#                       activate-text-mbs.sh / activate-live-mbs.sh (e.g. MEDIA_FILE, BITRATE_KBPS)
#   UE_IMSIS="..."      IMSIs to provision (default: the two testbed UEs)
#   UE_SERVICES="..."   UE containers to start (default: oai_nrue_1 oai_nrue_2)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(readlink -f "$SCRIPT_DIR/..")"
CORE="$ROOT/5G-MAG-Core"
UE_IMSIS="${UE_IMSIS:-999700000168430 999700000168431}"
UE_SERVICES="${UE_SERVICES:-oai_nrue_1 oai_nrue_2}"
GNB_LOG_DIR="$ROOT/logs/ocudu-gnb"

compose() {
  docker compose --env-file "$CORE/.env" \
    -f "$CORE/compose-files/internal/docker-compose.yaml" \
    -f "$CORE/compose-files/internal/docker-compose.ocudu-gnb.yaml" \
    -f "$CORE/compose-files/internal/docker-compose.virtual-mbs.yaml" "$@"
}

build_oai_nrue() {
  # OAI's dependency image, then a lean nrUE + ZMQ build (not OAI's full ran-build with -Werror).
  cd "$ROOT/oai-ue"
  docker build --target ran-base -t ran-base:latest -f docker/Dockerfile.base.ubuntu .
  docker build --build-arg NUM_JOBS="${OAI_NUM_JOBS:-$(nproc)}" \
    -t oai-nr-ue:mbs-local -f docker/Dockerfile.nrUE.zmq.ubuntu .
}

provision_subscribers() {
  local imsi
  for imsi in $UE_IMSIS; do
    echo "==> Provisioning subscriber $imsi"
    docker exec db mongosh open5gs --quiet --eval "
      db.subscribers.updateOne({ imsi: '$imsi' }, { \$set: {
        imsi: '$imsi', msisdn: [],
        security: { k: 'AE161E7AAC4FEB0BE1519C37243A3ADA', op: null,
                    opc: '2E303F9C0D27E0FB8B411BCDCC4DEC3C', amf: '8000', sqn: 0 },
        ambr: { uplink: { value: 1, unit: 3 }, downlink: { value: 1, unit: 3 } },
        slice: [{ sst: 1, sd: 'ffffff', default_indicator: true,
          session: [{ name: 'internet', type: 3, pcc_rule: [],
            ambr: { uplink: { value: 1, unit: 3 }, downlink: { value: 1, unit: 3 } },
            qos: { index: 9, arp: { priority_level: 8, pre_emption_capability: 1,
                                    pre_emption_vulnerability: 1 } } }],
          ambr: { uplink: { value: 1, unit: 3 }, downlink: { value: 1, unit: 3 } } }],
        access_restriction_data: 32, subscriber_status: 0, network_access_mode: 0
      } }, { upsert: true })"
  done
}

wait_for_db() {
  local i
  for i in $(seq 1 30); do
    docker exec db mongosh --quiet --eval 'db.runCommand({ping:1}).ok' >/dev/null 2>&1 && return 0
    sleep 2
  done
  echo "ERROR: MongoDB (db) not ready" >&2; exit 1
}

case "${1:-}" in
  build)
    build_oai_nrue
    compose build ocudu_gnb_with_mbs zmq_broker
    ;;
  up)
    mkdir -p "$GNB_LOG_DIR"
    # Core first so subscribers exist before the UEs attach.
    compose up -d db nrf ausf udm udr nssf bsf pcf webui amf_with_mbs smf_mb-smf upf_mb-upf test_mbs_af_as
    wait_for_db
    provision_subscribers
    # Broker, gNB and UEs together: the ZMQ links are only established once.
    compose up -d zmq_broker ocudu_gnb_with_mbs $UE_SERVICES
    echo "==> Up. Watch: $0 logs zmq_broker | $0 logs oai_nrue_1 | tail -f $GNB_LOG_DIR/ocudu_gnb_mbs.log"
    echo "==> MBS payloads per UE arrive on the host at udp://10.33.33.1:5001, :5002"
    ;;
  down)
    compose down
    ;;
  activate)
    export GNB_LOG="${GNB_LOG:-$GNB_LOG_DIR/ocudu_gnb_mbs.log}"
    case "${MODE:-text}" in
      text)  exec "$SCRIPT_DIR/activate-text-mbs.sh" ;;
      video) exec "$SCRIPT_DIR/activate-live-mbs.sh" ;;
      *) echo "MODE must be text or video" >&2; exit 2 ;;
    esac
    ;;
  logs)
    compose logs -f "${2:?service name}"
    ;;
  *)
    sed -n '3,16p' "$0"; exit 2
    ;;
esac

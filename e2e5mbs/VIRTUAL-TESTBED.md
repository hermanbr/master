# Virtual 5G MBS Testbed

An all-software version of the e2e5Gmbs testbed. A 5G core with MBS support,
an OCUDU gNB and several OAI nrUEs run as Docker containers on one host. Instead
of USRP radios, the gNB and UEs exchange raw radio samples over **ZMQ**, and a
small **broker** lets one gNB serve many UEs. The result is a 5G broadcast (MBS)
that several UEs receive at the same time, without any RF hardware.

---

## 1. The big picture

```
                                   ┌─────────────────────── 5G core (Open5GS + 5G-MAG MBS) ───────────────────────┐
                                   │                                                                               │
   ┌───────────────┐   HTTP/2      │  ┌────────┐   ┌──────────────┐   ┌─────────┐   ┌─────┐ ┌─────┐ ┌─────┐       │
   │ test_mbs_af_as│──────────────────▶ MB-SMF │──▶│ amf_with_mbs │   │  NRF    │   │ UDM │ │ UDR │ │ db  │ ...   │
   │ (AF/AS: MBS   │  create MBS   │  └───┬────┘   └──────┬───────┘   └─────────┘   └─────┘ └─────┘ └─────┘       │
   │  session +    │  session      │      │ N4           │                                                         │
   │  traffic)     │               │  ┌───▼──────┐       │                                                         │
   └──────┬────────┘               │  │ MB-UPF   │       │                                                         │
          │                        │  └──────────┘       │                                                         │
          │                        └─────────────────────┼─────────────────────────────────────────────────────────┘
          │ GTP-U to multicast group (UDP 2152)          │ N2 / NGAP (SCTP 38412)
          ▼                                              ▼
   ┌─────────────────────────────────────────────────────────────┐
   │                 OCUDU gNB  (ocudu_gnb_with_mbs)             │
   │   schedules the broadcast on MTCH with G-RNTI 0xfe01        │
   └──────────────┬───────────────────────────▲──────────────────┘
       DL samples │ ZMQ :2000                 │ ZMQ :2001 UL samples (sum)
                  ▼                           │
   ┌─────────────────────────────────────────────────────────────┐
   │                     zmq_broker                              │
   │   DL: copy each sample block to every UE                    │
   │   UL: add all UEs' samples together                         │
   └──────┬──────────────▲───────────────┬──────────────▲────────┘
     :2100│        :2001 │          :2101│        :2001 │
          ▼              │               ▼              │
   ┌──────────────────────────┐   ┌──────────────────────────┐
   │ oai_nrue_1               │   │ oai_nrue_2               │   ... more UEs
   │ IMSI 999700000168430     │   │ IMSI 999700000168431     │
   └────────────┬─────────────┘   └────────────┬─────────────┘
                │ received MBS payload (UDP)   │
                ▼                              ▼
          host 10.33.33.1:5001           host 10.33.33.1:5002
```

---

## 2. The parts

### 5G core: `5G-MAG-Core/`
The 5G-MAG fork of **Open5GS**, with Multicast/Broadcast Services added. All
images are **pulled ready-made** from `ghcr.io/5g-mag/`; nothing in the core is
compiled locally.

| Container | Role |
|---|---|
| `amf_with_mbs` | AMF with MBS support. The gNB connects to it, and it relays registration and MBS broadcast session setup to the gNB. |
| `smf_mb-smf` | SMF + **MB-SMF**. Handles normal PDU sessions and creates MBS sessions: allocates the TMGI (the broadcast service ID) and sets up the user plane. |
| `upf_mb-upf` | UPF + **MB-UPF**. User-plane function for PDU sessions and multicast. |
| `nrf`, `ausf`, `udm`, `udr`, `nssf`, `pcf`, `bsf` | Standard Open5GS functions: service discovery, authentication, subscriber data, slicing, policy. |
| `db` | MongoDB holding the subscribers (IMSIs and keys). |
| `webui` | Open5GS web UI for subscribers (optional). |
| `test_mbs_af_as` | Test **Application Function / Application Server**: asks the MB-SMF for a broadcast session and sends the broadcast content (text or video). |

### gNB: `OCUDU-gNB/`
An **OCUDU** gNB (derived from srsRAN Project) with added MBS support: it
accepts broadcast sessions from the AMF, joins the multicast tunnel and
schedules the content on the **MTCH** using group radio ID (**G-RNTI**)
`0xfe01`. Its radio driver is set to **ZMQ** instead of a USRP (UHD). Built
locally (only the `gnb` app, ZMQ radio, no DPDK/UHD) into the image
`ocudu/gnb:mbs-local`.

### UEs: `oai-ue/`
**OAI nrUE** (OpenAirInterface) with MBS receive patches. It monitors the
G-RNTI given by `OAI_NRUE_MBS_G_RNTI` in the same search space the gNB uses for
MBS (the paging search space), reassembles the RLC UM segments, strips the
PDCP header and forwards each MBS packet, exactly as the AF/AS sent it, to
the UDP address in `OAI_NRUE_MBS_UDP`. The UE does not decode MCCH, so the MRB
layout comes from `OAI_NRUE_MBS_RLC_SN_BITS` (default 6) and
`OAI_NRUE_MBS_PDCP_SN_BITS` (default 12), matching OCUDU's defaults. It uses OAI's ZMQ radio driver
(`oai_zmqdevif`), which follows the same ZMQ convention as OCUDU, so the two
can exchange samples. Built locally into the image `oai-nr-ue:mbs-local`.

### ZMQ broker: `tools/zmq-broker/`
A ZMQ link is point-to-point: one gNB port can only feed one UE. The broker
plays the role of the air between one gNB and many UEs (details in
[section 4](#4-how-the-zmq-radio-link-works)). It's a short Python script
using `pyzmq` and `numpy`, built into `e2e5gmbs/zmq-broker:local`.

---

## 3. Network and addresses

All containers share one Docker bridge network, **`5g-mag`**
(`10.33.33.0/24`, host bridge `br-5g-mag`), with fixed IPs. Containers reach
each other by IP or by aliases such as `amf-with-mbs.5g-mag.org`, which
Docker's built-in DNS resolves.

| IP | Container | | IP | Container |
|---|---|---|---|---|
| `.1` | **host** (bridge gateway) | | `.16` | `ocudu_gnb_with_mbs` |
| `.2` | `db` | | `.18` | `smf_mb-smf` |
| `.3` | `upf_mb-upf` | | `.19` | `amf_with_mbs` |
| `.4`–`.9` | `nrf`, `ausf`, `udm`, `udr`, `nssf`, `webui` | | `.20`, `.21` | `pcf`, `bsf` |
| `.10` | `test_mbs_af_as` | | `.30` | `zmq_broker` |
| | | | `.31`, `.32` | `oai_nrue_1`, `oai_nrue_2` |

### Links between the parts

| Link | Protocol / port | From → to | Carries |
|---|---|---|---|
| Core service bus (SBI) | HTTP/2 | core functions ↔ each other | registration, authentication, sessions |
| **N2** | NGAP over **SCTP 38412** | gNB → AMF | gNB setup, UE signalling, MBS session setup |
| **N4** | PFCP | MB-SMF → MB-UPF | user-plane rules |
| **N3 / MBS user plane** | GTP-U over UDP 2152 | AF/AS → multicast group → gNB | the broadcast content |
| **Radio** | ZMQ over TCP | gNB ⇄ broker ⇄ UEs | raw IQ samples |
| **UE output** | UDP 5001, 5002, … | UE → host `10.33.33.1` | decoded MBS payload |

SCTP is handled by the host's kernel, so the host needs the `sctp` module
loaded (`sudo modprobe sctp`).

---

## 4. How the ZMQ radio link works

On real hardware, the gNB sends RF through an antenna and every UE in range
hears it. In this testbed, the "RF" is a stream of complex samples (complex
float32, 23.04 million samples per second) sent as ZMQ messages over TCP.

Both OCUDU and OAI use the same convention:
- The **transmitting** side opens a ZMQ **REP** socket and waits.
- The **receiving** side connects a ZMQ **REQ** socket and asks for samples.
- Each request is answered with the next block of samples.

The broker sits between the gNB and every UE:

```
Downlink (gNB → UEs)                         Uplink (UEs → gNB)
gNB REP :2000                                gNB REQ  → broker REP :2001
   ▲ REQ                                         broker REQ → UE_i REP :2001 (each UE)
broker  ─ waits until every UE has asked,        broker adds the UE blocks together
        ─ fetches ONE block from the gNB,        and returns the sum to the gNB
        ─ sends that same block to every UE
   ▼ REP :2100, :2101, ...
UE_i REQ
```

- **Downlink:** each UE gets exactly the same samples, in lockstep. That's what
  a broadcast over the air looks like.
- **Uplink:** UE signals are added together, just as radio signals add up at an
  antenna. UEs are told apart the normal 5G way (different timing, resources
  and identities), so the gNB sees several UEs on one cell.
- **No clock:** a ZMQ link has no hardware clock. If the host is slow, the
  whole simulation runs slower than real time instead of losing samples
  (with gNB + 2 UEs on 16 cores, roughly 1/10 of real time).
- **No reconnects:** OAI cannot reconnect a ZMQ link once it has started.
  Always start and stop the broker, gNB and UEs **together**.

**Cell configuration** (`5G-MAG-Core/configs/internal/ocudu_gnb_mbs_zmq.yml`):
band n78 (TDD), DL ARFCN 632628 (3489.42 MHz), 30 kHz subcarrier spacing,
20 MHz (51 PRB), PLMN 999/70, TAC 1, slice SST 1 / SD `ffffff`. The UE options
(`-r 51 --numerology 1 --band 78 -C 3489420000 --ssb 0 -E`) match this cell.
They come from OAI's own OCUDU + OAI ZMQ test
(`oai-ue/ci-scripts/yaml_files/5g_zmq_radio_1x1_ocudu`).

---

## 5. What happens end to end

**Start-up** (`./virtual-mbs.sh up`)
1. The core functions start; the subscribers (IMSIs …430 and …431) are written to MongoDB.
2. The broker, gNB and UEs start. The gNB connects to the AMF over N2 (*NG Setup*).
3. Each UE finds the cell in the downlink samples (SSB, MIB, SIB1), then
   registers and sets up a PDU session through the uplink. Each UE gets an IP
   address from `10.45.0.0/16`.

**Broadcast** (`./virtual-mbs.sh activate`)
1. `test_mbs_af_as` asks the **MB-SMF** to create a **broadcast** MBS session.
2. The MB-SMF allocates a TMGI and, via the AMF, tells the gNB to set up the
   session. The gNB **joins a multicast group** with a tunnel ID (TEID) and logs
   `Joined multicast group <group>:2152 teid=<teid>`.
3. The script reads the group and TEID from the gNB log and starts a sender in
   `test_mbs_af_as`. The sender wraps the content in GTP-U packets and sends
   them to that multicast group:
   - **text** (`mbs_text_stream.py`): `testMsg <N> Tx_ns=<timestamp>` every second;
   - **video** (`mbs_video_stream.py`): an MPEG-TS file from `5G-MAG-Core/test/media/`.
4. The gNB schedules the content on the **MTCH** with G-RNTI `0xfe01`
   (`MTCH|PDCCH|PDSCH: rnti=0xfe01` in the gNB log). Every UE decodes the same
   transmission.
5. Each UE reassembles the RLC segments, strips the PDCP header and forwards
   the payload to the host (`10.33.33.1:5001`, `:5002`, …). The host gets
   exactly the bytes the AF/AS put in each GTP-U packet.

---

## 6. Files

| Path | What it is |
|---|---|
| `docs/virtual-mbs.sh` | **Entry point**: `build`, `up`, `activate`, `logs`, `down` |
| `OCUDU-gNB/docker/Dockerfile.gnb.zmq` | Lean OCUDU gNB image (only `gnb`, ZMQ radio) |
| `oai-ue/docker/Dockerfile.nrUE.zmq.ubuntu` | Lean OAI nrUE + ZMQ image (built on OAI's `ran-base`) |
| `5G-MAG-Core/compose-files/internal/docker-compose.yaml` | Core network + test AF/AS |
| `5G-MAG-Core/compose-files/internal/docker-compose.ocudu-gnb.yaml` | Adds the OCUDU gNB |
| `5G-MAG-Core/compose-files/internal/docker-compose.virtual-mbs.yaml` | Adds the broker and the OAI UEs |
| `5G-MAG-Core/configs/internal/ocudu_gnb_mbs_zmq.yml` | gNB config (cell, ZMQ ports, AMF) |
| `5G-MAG-Core/configs/internal/oai_nrue_{1,2}.conf` | UE SIM data (IMSI, K, OPc, DNN, slice) |
| `5G-MAG-Core/configs/internal/*.yaml` | Core function configs |
| `5G-MAG-Core/.env` | Image versions |
| `tools/zmq-broker/` | Broker script and Dockerfile |
| `docs/activate-text-mbs.sh`, `docs/activate-live-mbs.sh` | MBS session + sender (called by `activate`) |
| `logs/ocudu-gnb/` | gNB log and packet captures (created at runtime) |

---

## 7. Requirements

- x86_64 CPU with **AVX2**; about **8+ cores** for gNB + 2 UEs, plus 2 per extra
  UE; 16 GB RAM; about 40 GB free disk.
- Ubuntu 22.04/24.04 with Docker Engine + Compose v2, your user in the `docker`
  group, and the `sctp` kernel module.

One-time host setup:
```bash
sudo apt-get update && \
sudo apt-get install -y git curl ca-certificates netcat-openbsd ffmpeg tcpdump && \
curl -fsSL https://get.docker.com | sudo sh && \
sudo usermod -aG docker "$USER" && \
sudo modprobe sctp && echo sctp | sudo tee /etc/modules-load.d/sctp.conf
```
Log out and back in afterwards so the `docker` group applies. With SSH
connection sharing, close the shared connection first: `ssh -O exit <host>`.

---

## 8. Running it

```bash
cd docs
./virtual-mbs.sh build                         # gNB, OAI UE and broker images (long first time)
./virtual-mbs.sh up                            # core + subscribers, then broker, gNB, UEs
./virtual-mbs.sh logs zmq_broker               # DL/UL block counters should increase
./virtual-mbs.sh logs oai_nrue_1               # cell found, registration, PDU session
./virtual-mbs.sh activate                      # text broadcast (MODE=video for video)
nc -ul 5001                                    # what UE1 receives (UE2: 5002)
ffplay udp://0.0.0.0:5001                      # ...when MODE=video
./virtual-mbs.sh down
```

The gNB log is at `logs/ocudu-gnb/ocudu_gnb_mbs.log`.

### Verifying the data paths

| Path | Check | Expected |
|---|---|---|
| Radio (ZMQ) | `./virtual-mbs.sh logs zmq_broker` | DL/UL counters keep increasing |
| Attach + PDU session | `docker logs oai_nrue_1 \| grep 'UE IPv4'` | `Received PDU Session Establishment Accept, UE IPv4: 10.45.0.x` |
| Unicast UL+DL (UE ⇄ UPF) | `sudo nsenter -t $(docker inspect -f '{{.State.Pid}}' oai_nrue_1) -n ping -I oaitun_ue1 -c 5 10.45.0.1` | 0% loss (the UE image has no `ping`, so this uses the host's) |
| MBS on the gNB | `grep 'MTCH: rnti=0xfe01' logs/ocudu-gnb/ocudu_gnb_mbs.log` | one line per scheduled MTCH grant |
| MBS on each UE | `docker logs oai_nrue_1 \| grep 'MTCH SDU'` | one line per received packet, same counts on every UE |
| MBS to the host | `nc -ul 5001` / `sudo tcpdump -ni br-5g-mag udp dst port 5001 or udp dst port 5002 -A` | `testMsg N Tx_ns=…` on both ports, or 1316-byte MPEG-TS packets with `MODE=video` |

---

## 9. Adding more UEs

1. In `docker-compose.virtual-mbs.yaml`, copy an `oai_nrue_N` block and change
   the container name, IP (`.33`), conf file, UDP port (`5003`) and broker port
   (`tcp://10.33.33.30:2102`).
2. Add a matching pair to the `zmq_broker` command:
   `--ue-dl-bind=tcp://*:2102` and `--ue-tx=tcp://10.33.33.33:2001`.
3. Create `oai_nrue_3.conf` with a new IMSI.
4. Run with `UE_IMSIS="… new IMSI"` and `UE_SERVICES="oai_nrue_1 oai_nrue_2 oai_nrue_3"`.

The broker waits for **every** configured UE before it sends any downlink. The
number of `--ue-dl-bind`/`--ue-tx` pairs must therefore match the UEs you
actually start.

---

## 10. Troubleshooting

| Symptom | Likely cause |
|---|---|
| `permission denied ... docker.sock` | Not in the `docker` group yet: log in again, or run `newgrp docker`. |
| gNB never connects to the AMF | `sctp` module not loaded on the host. |
| Broker shows no DL progress | One of the configured UEs is not running (see section 9). |
| UE never finds the cell | Cell settings and UE options don't match; check the gNB's SSB position against `--ssb`. |
| UE aborts with `NTN cell, but SIB19 not configured` | Old gNB image: SIB1 marked the cell as NTN when SIB20 was scheduled. Rebuild the gNB (`./virtual-mbs.sh build`). |
| Radio stalls (gNB log `zmq: Waiting for data`) right after the first MBS packet | Old gNB image with the MTCH bearer deadlock. Rebuild the gNB. |
| gNB logs `MTCH: rnti=0xfe01` but UEs log no `MTCH SDU` | Old UE image that looked for G-RNTI in the wrong search space. Rebuild the UE. |
| UE finds the cell but registration fails | IMSI not in MongoDB, or K/OPc mismatch; rerun `up` or check `docker logs amf_with_mbs`. |
| Registration times out | Host too slow (simulation far below real time): fewer UEs, or more cores. |
| Everything stalls after restarting one container | ZMQ links can't reconnect: run `down` and `up` again. |
| `activate` can't find the multicast group | Check `GNB_LOG` (default `logs/ocudu-gnb/ocudu_gnb_mbs.log`) and the AMF log. |

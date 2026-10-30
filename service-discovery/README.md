# Service discovery over 5G MBS and WiFi

Three discovery protocols, two platforms, and every protocol runs on every platform.

|            | 5G MBS (virtual testbed)                 | WiFi (mininet-wifi)                                         |
|------------|------------------------------------------|-------------------------------------------------------------|
| **SSSD**   | `src/sd/platforms/fiveg/run.sh sssd`     | `sudo python3 src/sd/platforms/wifi/run.py --protocol sssd`  |
| **mDNS**   | `src/sd/platforms/fiveg/run.sh mdns`     | `sudo python3 src/sd/platforms/wifi/run.py --protocol mdns`  |
| **Zenoh**  | `src/sd/platforms/fiveg/run.sh zenoh`    | `sudo python3 src/sd/platforms/wifi/run.py --protocol zenoh` |

Add `--test <seconds>` to any of them to run headless. The run prints `PASS` and exits 0 when the discoverer found exactly the `camera` devices. Run `uv sync` here first.

## Layout

```
src/sd/
  common/      shared by everything: registry (the printed service list), check (PASS/FAIL), cli, netif
  protocols/   sssd/  mdns/  zenoh/   - plain IP multicast, no knowledge of the platform
  platforms/   fiveg/ (bridge, gtpu, run.sh)   wifi/ (run.py)
```

**Protocols** each implement the same command line and export the multicast `GROUP`/`PORT` they use:

```
python -m sd.protocols.<p> advertise --service-type camera --name ue1 --iface IF
python -m sd.protocols.<p> discover  --service-type camera --iface IF [--duration S]
```

- **SSSD** (Super Simple Service Discovery): a JSON query to `239.255.0.1:5400` every 2 s. Matching advertisers answer the sender by unicast.
- **mDNS** (DNS-SD, python-zeroconf): `_<type>._udp.local.` instances. The discoverer queries from an ephemeral port, so answers come back by unicast (legacy unicast, RFC 6762 §6.7).
- **Zenoh**: Zenoh's own scouting on `224.0.0.224:7446`, then a query on `sd/services/<type>/*` over the peer links.

Every protocol replies to the query's source address and reports through `common/registry.py`, so `common/check.py` works for all of them. No protocol imports anything from `platforms/`.

**Platforms** only have to deliver the protocol's multicast from the AS to the devices:

- **WiFi**: does this natively. `h1` (AS) is wired to `ap1`, and `sta1..3` are 802.11g stations. `--far` puts sta3 out of range.
- **5G MBS**: carries payload bytes only, with no inner IP header, so `platforms/fiveg/bridge.py` carries the protocol's datagrams unchanged:

```
AF/AS netns:  discover --multicast GROUP:PORT--> bridge as --GTP-U (MBS group, TEID)--> gNB --MTCH--> UEs
host:         UE forwards payload to udp/500N --> bridge ue --GROUP:PORT on oaitun_ue1--> advertise
UE netns:     advertise --reply--> bridge ue --udp/6100 over the PDU session--> bridge as --> discover
```

`run.sh` needs the testbed up with an active MBS session (`./docs/virtual-mbs.sh up && activate` in `e2e5mbs/`). It reads the tunnel from the gNB log under `E2E_ROOT`, which defaults to `../e2e5mbs`.

## Adding a protocol or platform

- **Protocol**: create `protocols/<p>/` with `GROUP`, `PORT`, `advertise(args)`, `discover(args)` (reporting through `registry`) and a `__main__.py` calling `sd.common.cli.run`. Then add it to `PROTOCOLS`.
- **Platform**: a runner that starts `advertise` on the devices and `discover` on the AS, makes multicast reach them, and calls `sd.common.check`.

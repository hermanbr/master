#!/usr/bin/env python3
"""Run one discovery protocol over an emulated WiFi network (mininet-wifi).

  h1 (AS, wired to ap1)  runs discover
  ap1                    802.11g AP; multicast goes out over the air to every station
  sta1..sta3             run advertise (camera / thermostat / camera)

    h1 --eth-- ap1 ))) sta1 (camera)
                   ))) sta2 (thermostat)
                   ))) sta3 (camera)

Usage (system python3, which has mininet-wifi; the nodes use the project venv):
  sudo python3 src/sd/platforms/wifi/run.py --protocol sssd              # then the mininet-wifi CLI
  sudo python3 src/sd/platforms/wifi/run.py --protocol mdns --test 20    # PASS if h1 found sta1+sta3
  sudo python3 src/sd/platforms/wifi/run.py --protocol zenoh --test 20 --far   # sta3 out of range

Logs go to --log-dir (default /tmp/sd-wifi/<protocol>): discover.log, sta1.log, ...
"""

import argparse
import os
import sys
import time

from mininet.log import info, setLogLevel
from mn_wifi.cli import CLI
from mn_wifi.net import Mininet_wifi

SRC_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
VENV_PYTHON = os.path.join(SRC_DIR, "..", ".venv", "bin", "python")
sys.path.insert(0, SRC_DIR)

from sd.common.check import check  # noqa: E402  (stdlib only, fine under system python3)
from sd.protocols import PROTOCOLS  # noqa: E402

AS_IP = "10.0.0.100"
# (name, service type, position in m; ap1 sits at 50,50 with ~30 m range in mode g)
STATIONS = [
    ("sta1", "camera", "40,50,0"),
    ("sta2", "thermostat", "60,50,0"),
    ("sta3", "camera", "50,60,0"),
]
FAR_POSITION = "150,50,0"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--protocol", choices=PROTOCOLS, default="sssd")
    parser.add_argument("--service-type", default="camera", help="service type h1 discovers")
    parser.add_argument("--test", type=float, metavar="SECONDS", help="run non-interactively and verify discovery")
    parser.add_argument("--far", action="store_true", help="place sta3 out of range of ap1")
    parser.add_argument("--plot", action="store_true", help="show the node positions in a graph")
    parser.add_argument("--log-dir", help="default /tmp/sd-wifi/<protocol>")
    args = parser.parse_args()
    args.log_dir = args.log_dir or f"/tmp/sd-wifi/{args.protocol}"
    if not os.path.exists(VENV_PYTHON):
        parser.error(f"{VENV_PYTHON} not found; run 'uv sync' in {os.path.dirname(SRC_DIR)}")
    return args


def wait_for_association(ap, stations, timeout: float = 20.0) -> None:
    """Retry the association of in-range stations; out-of-range ones stay disconnected.

    mininet-wifi sends a single `iw connect` during build(), which is lost when hostapd
    is not beaconing yet (seen with 3+ stations), so retry it until the link is up.
    """
    ap_intf = ap.wintfs[0]
    deadline = time.monotonic() + timeout
    pending = [s for s in stations if s.get_distance_to(ap) <= ap_intf.range]
    while pending and time.monotonic() < deadline:
        pending = [s for s in pending if "Connected" not in s.cmd(f"iw dev {s.name}-wlan0 link")]
        for sta in pending:
            sta.wintfs[0].iw_connect(ap_intf)
        time.sleep(2)
    pending += [s for s in stations if s.get_distance_to(ap) > ap_intf.range]
    for sta in stations:
        info(f"    {sta.name}: {'not associated' if sta in pending else 'associated'}\n")


def main() -> int:
    args = parse_args()
    os.makedirs(args.log_dir, exist_ok=True)
    proto = f"{VENV_PYTHON} -u -m sd.protocols.{args.protocol}"

    net = Mininet_wifi()

    info("*** Creating nodes\n")
    ap1 = net.addAccessPoint("ap1", ssid="sd", mode="g", channel="1",
                             position="50,50,0", failMode="standalone")
    h1 = net.addHost("h1", ip=f"{AS_IP}/8")
    stations = []
    for name, _, position in STATIONS:
        if args.far and name == "sta3":
            position = FAR_POSITION
        stations.append(net.addStation(name, position=position))

    net.setPropagationModel(model="logDistance", exp=4)
    net.configureNodes()

    net.addLink(h1, ap1)
    if args.plot:
        net.plotGraph(max_x=160, max_y=100)

    info("*** Starting network\n")
    net.build()
    ap1.start([])
    wait_for_association(ap1, stations)

    # Mininet nodes have no default route, so multicast needs an explicit one.
    h1.cmd("ip route add 224.0.0.0/4 dev h1-eth0")
    for sta in stations:
        sta.cmd(f"ip route add 224.0.0.0/4 dev {sta.name}-wlan0")

    info(f"*** Starting {args.protocol} advertise on the stations\n")
    for sta, (name, service_type, _) in zip(stations, STATIONS):
        sta.cmd(f"{proto} advertise --service-type {service_type} --name {name} --iface {name}-wlan0 "
                f"> {args.log_dir}/{name}.log 2>&1 &")
        info(f"    {name} ({sta.IP()}) offers '{service_type}'\n")

    discover_log = f"{args.log_dir}/discover.log"
    info(f"*** Starting {args.protocol} discover on h1 for '{args.service_type}'\n")
    h1.cmd(f"{proto} discover --service-type {args.service_type} --iface h1-eth0 > {discover_log} 2>&1 &")

    rc = 0
    if args.test:
        info(f"*** Running for {args.test:.0f} s\n")
        time.sleep(args.test)
        want = {sta.IP() for sta, (name, service_type, _) in zip(stations, STATIONS)
                if service_type == args.service_type and not (args.far and name == "sta3")}
        rc = 0 if check(discover_log, want) else 1
    else:
        info(f"*** Logs in {args.log_dir}. Try: h1 tail -n 8 {discover_log}\n")
        info("***      or: sta1 iw dev sta1-wlan0 link   /   py sta3.setPosition('150,50,0')\n")
        CLI(net)

    info("*** Stopping network\n")
    for node in [h1, *stations]:
        node.cmd("pkill -f '[s]d.protocols'")
    time.sleep(1)
    for node in [h1, *stations]:
        node.cmd("pkill -9 -f '[s]d.protocols'")  # Zenoh can hang in shutdown
    net.stop()
    return rc


if __name__ == "__main__":
    setLogLevel("info")
    sys.exit(main())

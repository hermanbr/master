import argparse
import socket

from zeroconf import ServiceBrowser, ServiceStateChange, Zeroconf

from sd.common import registry
from sd.common.cli import stop_event
from sd.common.netif import iface_ipv4
from sd.protocols.mdns.advertise import service_type


def discover(args: argparse.Namespace) -> int:
    addr = iface_ipv4(args.iface)
    # unicast=True queries from an ephemeral port, so responders answer by unicast to the sender
    # (legacy unicast, RFC 6762 6.7) instead of multicast - the same reply path as the other protocols.
    zc = Zeroconf(interfaces=[addr], unicast=True)
    type_ = service_type(args.service_type)

    def on_change(zeroconf: Zeroconf, service_type: str, name: str, state_change: ServiceStateChange) -> None:
        if state_change == ServiceStateChange.Removed:
            print(f"gone:  {name}", flush=True)
            registry.remove(name)
            return
        info = zeroconf.get_service_info(service_type, name, timeout=2000)
        if info and info.addresses:
            registry.add(name, socket.inet_ntoa(info.addresses[0]), info.port, args.service_type)

    browser = ServiceBrowser(zc, type_, handlers=[on_change])
    print(f"browsing {type_} on {args.iface} ({addr})", flush=True)
    registry.start_print_thread()

    stop_event(args.duration).wait()
    browser.cancel()
    zc.close()
    return 0

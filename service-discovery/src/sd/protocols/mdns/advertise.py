import argparse
import socket

from zeroconf import ServiceInfo, Zeroconf

from sd.common.cli import stop_event
from sd.common.netif import iface_ipv4

SERVICE_PORT = 8000  # nominal port the service would run on; only carried in the SRV record


def service_type(name: str) -> str:
    return f"_{name}._udp.local."


def advertise(args: argparse.Namespace) -> int:
    addr = iface_ipv4(args.iface)
    type_ = service_type(args.service_type)
    info = ServiceInfo(type_, f"{args.name}.{type_}", port=SERVICE_PORT,
                       addresses=[socket.inet_aton(addr)], server=f"{args.name}.local.")
    zc = Zeroconf(interfaces=[addr])
    zc.register_service(info)
    print(f"{args.name}: {info.name} on {args.iface} ({addr}), port {SERVICE_PORT}", flush=True)

    stop_event().wait()
    zc.unregister_service(info)
    zc.close()
    return 0

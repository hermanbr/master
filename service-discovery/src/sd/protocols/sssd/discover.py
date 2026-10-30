import argparse
import socket
import threading

from sd.common import registry
from sd.common.cli import stop_event
from sd.common.netif import iface_ipv4, multicast_tx
from sd.protocols.sssd import GROUP, PORT
from sd.protocols.sssd.message import Message, MsgType

QUERY_INTERVAL_S = 2.0


def add_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--dest", default=GROUP,
                        help=f"where queries go (default {GROUP}); a broadcast address also works")


def _rx_loop(sock: socket.socket) -> None:
    while True:
        data, src = sock.recvfrom(65535)
        try:
            msg = Message.from_bytes(data)
        except (ValueError, TypeError):
            print(f"rx: ignored {len(data)} bytes from {src[0]}", flush=True)
            continue
        if msg.msg_type == MsgType.RESPONSE:
            registry.add(f"sssd/{msg.service_type}/{msg.name}", msg.addr or src[0], PORT, msg.service_type)


def discover(args: argparse.Namespace) -> int:
    addr = iface_ipv4(args.iface)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    multicast_tx(sock, addr)
    sock.bind((addr, 0))  # responses come back to this socket
    threading.Thread(target=_rx_loop, args=(sock,), daemon=True).start()
    registry.start_print_thread()

    query = Message(MsgType.QUERY, args.service_type).to_bytes()
    print(f"querying '{args.service_type}' to {args.dest}:{PORT} from {addr}:{sock.getsockname()[1]}", flush=True)
    stop = stop_event(args.duration)
    while not stop.is_set():
        sock.sendto(query, (args.dest, PORT))
        stop.wait(QUERY_INTERVAL_S)
    return 0

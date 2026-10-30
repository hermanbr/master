import argparse
import socket

from sd.common.netif import iface_ipv4, join_group
from sd.protocols.sssd import GROUP, PORT
from sd.protocols.sssd.message import Message, MsgType


def advertise(args: argparse.Namespace) -> int:
    addr = iface_ipv4(args.iface)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("0.0.0.0", PORT))
    join_group(sock, GROUP, addr)
    print(f"{args.name}: '{args.service_type}' on {args.iface} ({addr}), listening {GROUP}:{PORT}", flush=True)

    while True:
        data, src = sock.recvfrom(65535)
        try:
            msg = Message.from_bytes(data)
        except (ValueError, TypeError):
            continue
        if msg.msg_type != MsgType.QUERY:
            continue
        match = msg.service_type == args.service_type
        print(f"query '{msg.service_type}' from {src[0]}:{src[1]}{' -> response' if match else ''}", flush=True)
        if match:
            response = Message(MsgType.RESPONSE, args.service_type, name=args.name, addr=addr)
            sock.sendto(response.to_bytes(), src)

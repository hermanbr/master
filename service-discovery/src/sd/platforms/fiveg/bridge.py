"""Carry a discovery protocol's multicast over 5G MBS: down the MBS session, replies up the PDU session.

The MBS path only carries payload bytes (no inner IP header), so a protocol's multicast cannot
cross it as-is. Two relays stand in for the missing IP multicast, passing the bytes unchanged:

  AF/AS netns:  discover --multicast GROUP:PORT--> [bridge as] --GTP-U (MBS group, TEID)--> gNB MBS
  host:         UE forwards the MTCH payload to udp/500N --> [bridge ue] --multicast GROUP:PORT
                on oaitun_ue1 (multicast loop) --> advertise; advertise --reply (unicast)--> [bridge ue]
  UE netns:     [bridge ue] --udp/6100 over the PDU session--> [bridge as] --> discover (unicast)

GROUP:PORT come from the protocol (sd.protocols.<p>.GROUP/PORT). Replies go to every discover
socket that sent within the last SENDER_TTL_S, since the bridge cannot tell which one they answer.

Usage:
  bridge.py as --protocol P --mbs-group G --teid T --interface ADDR   (in the AF/AS netns)
  bridge.py ue --protocol P --port 5001 --ue oai_nrue_1 [--as-addr 10.33.33.10]   (on the host, root)

--no-gtpu (as) and omitting --ue (ue) give a plain-UDP variant for testing without the testbed.
"""

import argparse
import importlib
import select
import socket
import time

from sd.common.netif import container_udp_socket, iface_ipv4, join_group
from sd.platforms.fiveg.gtpu import GTPU_PORT, encap
from sd.protocols import PROTOCOLS

AS_ADDR = "10.33.33.10"  # test_mbs_af_as on the testbed
UE_TUN = "oaitun_ue1"
UPLINK_PORT = 6100
SENDER_TTL_S = 10.0


def log(msg: str) -> None:
    print(f"{time.strftime('%H:%M:%S')} {msg}", flush=True)


def run_as(args: argparse.Namespace, group: str, port: int) -> int:
    tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    tx.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 1)
    tx.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    if args.interface:
        tx.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(args.interface))

    # Share GROUP:PORT with any local listener of the protocol.
    down_rx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    down_rx.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    down_rx.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
    down_rx.bind(("0.0.0.0", port))
    join_group(down_rx, group, args.interface or "0.0.0.0")

    up_rx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    up_rx.bind(("0.0.0.0", args.uplink_port))

    encap_str = "plain UDP" if args.no_gtpu else f"TEID=0x{args.teid:08x}"
    log(f"as: {group}:{port} -> {args.mbs_group}:{args.mbs_port} {encap_str}; replies on udp/{args.uplink_port}")

    senders: dict[tuple[str, int], float] = {}
    while True:
        ready, _, _ = select.select([down_rx, up_rx], [], [])
        now = time.monotonic()
        if down_rx in ready:
            data, src = down_rx.recvfrom(65535)
            if src not in senders:
                log(f"as: new sender {src[0]}:{src[1]}")
            senders[src] = now
            tx.sendto(data if args.no_gtpu else encap(args.teid, data), (args.mbs_group, args.mbs_port))
            log(f"as: {len(data)} B from {src[0]}:{src[1]} -> MBS")
        if up_rx in ready:
            data, src = up_rx.recvfrom(65535)
            live = [s for s, t in senders.items() if now - t < SENDER_TTL_S]
            for s in live:
                up_rx.sendto(data, s)
            log(f"as: reply {len(data)} B from {src[0]} -> {len(live)} sender(s)")


def run_ue(args: argparse.Namespace, group: str, port: int) -> int:
    mbs_rx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    mbs_rx.bind(("0.0.0.0", args.port))

    if args.ue:
        ue = container_udp_socket(args.ue, args.iface)
    else:
        ue = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        ue.setsockopt(socket.SOL_SOCKET, socket.SO_BINDTODEVICE, args.iface.encode())
    addr = iface_ipv4(args.iface, ue)
    ue.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(addr))
    ue.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, 1)
    ue.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 1)
    ue.bind((addr, 0))

    where = f"{args.ue} {args.iface} ({addr})" if args.ue else f"{args.iface} ({addr})"
    log(f"ue: MBS udp/{args.port} -> {group}:{port} on {where}; replies -> {args.as_addr}:{args.uplink_port}")

    while True:
        ready, _, _ = select.select([mbs_rx, ue], [], [])
        if mbs_rx in ready:
            data, _ = mbs_rx.recvfrom(65535)
            ue.sendto(data, (group, port))
            log(f"ue: {len(data)} B from MBS -> {group}:{port}")
        if ue in ready:
            data, src = ue.recvfrom(65535)
            ue.sendto(data, (args.as_addr, args.uplink_port))
            log(f"ue: reply {len(data)} B from {src[0]}:{src[1]} -> {args.as_addr}:{args.uplink_port}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="mode", required=True)

    p_as = sub.add_parser("as", help="AF/AS side: protocol multicast into MBS, replies back to the sender")
    p_as.add_argument("--protocol", choices=PROTOCOLS, required=True)
    p_as.add_argument("--mbs-group", required=True, help="MBS NG-U multicast group (with --no-gtpu: any address)")
    p_as.add_argument("--teid", type=lambda v: int(v, 0), help="MBS tunnel TEID (required unless --no-gtpu)")
    p_as.add_argument("--mbs-port", type=int, default=GTPU_PORT, help="GTP-U destination port")
    p_as.add_argument("--interface", help="local IPv4 to join the protocol group on and send from")
    p_as.add_argument("--no-gtpu", action="store_true", help="send plain UDP instead of GTP-U")
    p_as.add_argument("--uplink-port", type=int, default=UPLINK_PORT)

    p_ue = sub.add_parser("ue", help="UE side: MBS payload to the protocol group, replies uplink")
    p_ue.add_argument("--protocol", choices=PROTOCOLS, required=True)
    p_ue.add_argument("--port", type=int, required=True, help="UDP port the UE forwards the MBS payload to")
    p_ue.add_argument("--ue", help="UE container whose netns to send in (omit: current netns)")
    p_ue.add_argument("--iface", default=UE_TUN, help="advertiser's interface in that netns")
    p_ue.add_argument("--as-addr", default=AS_ADDR, help="address of the AS-side bridge")
    p_ue.add_argument("--uplink-port", type=int, default=UPLINK_PORT)

    args = parser.parse_args()
    proto = importlib.import_module(f"sd.protocols.{args.protocol}")
    if args.mode == "as":
        if not args.no_gtpu and args.teid is None:
            parser.error("--teid is required unless --no-gtpu is given")
        return run_as(args, proto.GROUP, proto.PORT)
    return run_ue(args, proto.GROUP, proto.PORT)


if __name__ == "__main__":
    raise SystemExit(main())

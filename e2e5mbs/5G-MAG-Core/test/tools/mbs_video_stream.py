#!/usr/bin/env python3
"""Stream an MPEG-TS file over the OCUDU MBS multicast GTP-U tunnel."""

import argparse
import ipaddress
import os
import signal
import socket
import struct
import sys
import time

TS_PACKET_SIZE = 188
DEFAULT_TS_PACKETS_PER_DATAGRAM = 7


def parse_int(value: str) -> int:
    return int(value, 0)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Pace an MPEG-TS file into GTP-U G-PDUs for the active OCUDU MBS session."
    )
    parser.add_argument("--input", required=True, help="MPEG-TS file visible inside the AF/AS container")
    parser.add_argument("--group", required=True, help="MBS NG-U IPv4 multicast group")
    parser.add_argument("--teid", required=True, type=parse_int, help="Tunnel TEID, for example 0x32a40000")
    parser.add_argument("--port", type=int, default=2152, help="GTP-U destination port")
    parser.add_argument("--interface", help="Source/interface IPv4 address; default uses the routing table")
    parser.add_argument("--bitrate-kbps", type=float, default=180.0, help="Target MPEG-TS payload bitrate")
    parser.add_argument(
        "--ts-packets-per-datagram",
        type=int,
        default=DEFAULT_TS_PACKETS_PER_DATAGRAM,
        help="Number of 188-byte TS packets in each GTP-U payload",
    )
    parser.add_argument("--loop", action="store_true", help="Loop the video until interrupted")
    parser.add_argument("--duration", type=float, help="Stop after this many seconds")
    return parser.parse_args()


def validate_args(args: argparse.Namespace) -> None:
    if not os.path.isfile(args.input):
        raise ValueError(f"input file does not exist: {args.input}")
    if not ipaddress.ip_address(args.group).is_multicast:
        raise ValueError(f"group is not multicast: {args.group}")
    if not 0 < args.teid <= 0xFFFFFFFF:
        raise ValueError("TEID must be in the range 1..0xffffffff")
    if not 0 < args.port <= 65535:
        raise ValueError("port must be in the range 1..65535")
    if args.bitrate_kbps <= 0:
        raise ValueError("bitrate must be greater than zero")
    if not 1 <= args.ts_packets_per_datagram <= 7:
        raise ValueError("ts-packets-per-datagram must be between 1 and 7")


def validate_mpeg_ts(path: str) -> None:
    with open(path, "rb") as media:
        sample = media.read(TS_PACKET_SIZE * 8)
    if len(sample) < TS_PACKET_SIZE or any(
        sample[offset] != 0x47 for offset in range(0, len(sample) - TS_PACKET_SIZE + 1, TS_PACKET_SIZE)
    ):
        raise ValueError("input is not an aligned MPEG-TS file; convert it with the documented ffmpeg command")


def main() -> int:
    args = parse_args()
    try:
        validate_args(args)
        validate_mpeg_ts(args.input)
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 1)
    if args.interface:
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(args.interface))

    stop = False

    def request_stop(_signum: int, _frame: object) -> None:
        nonlocal stop
        stop = True

    signal.signal(signal.SIGINT, request_stop)
    signal.signal(signal.SIGTERM, request_stop)

    payload_size = args.ts_packets_per_datagram * TS_PACKET_SIZE
    bitrate_bps = args.bitrate_kbps * 1000.0
    started = time.monotonic()
    next_send = started
    packets = 0
    payload_bytes = 0
    loops = 0

    print(
        f"Streaming {args.input} to {args.group}:{args.port} "
        f"TEID=0x{args.teid:08x} bitrate={args.bitrate_kbps:.1f} kbps"
    )

    while not stop:
        with open(args.input, "rb") as media:
            while not stop:
                payload = media.read(payload_size)
                if len(payload) < payload_size:
                    break

                now = time.monotonic()
                if args.duration is not None and now - started >= args.duration:
                    stop = True
                    break
                if next_send > now:
                    time.sleep(next_send - now)

                gtpu = struct.pack("!BBHI", 0x30, 0xFF, len(payload), args.teid) + payload
                sock.sendto(gtpu, (args.group, args.port))
                packets += 1
                payload_bytes += len(payload)
                next_send += len(payload) * 8.0 / bitrate_bps

                if packets % 100 == 0:
                    elapsed = max(time.monotonic() - started, 0.001)
                    actual_kbps = payload_bytes * 8.0 / elapsed / 1000.0
                    print(f"sent={packets} payload={payload_bytes} bytes rate={actual_kbps:.1f} kbps", flush=True)

        if stop or not args.loop:
            break
        loops += 1
        print(f"loop={loops}", flush=True)

    elapsed = max(time.monotonic() - started, 0.001)
    actual_kbps = payload_bytes * 8.0 / elapsed / 1000.0
    print(f"stopped: packets={packets} payload={payload_bytes} bytes average={actual_kbps:.1f} kbps")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

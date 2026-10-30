#!/usr/bin/env python3
"""Broadcast incrementing text messages over the OCUDU MBS multicast GTP-U tunnel.

Text analogue of mbs_video_stream.py: instead of pacing an MPEG-TS file, it sends
one short text message ("<prefix> <N> Tx_ns=<timestamp>") per interval as a
GTP-U G-PDU to the active MBS multicast group/TEID, so the gNB schedules it on
MTCH (G-RNTI) to all interested UEs. The GTP-U encapsulation matches
mbs_video_stream.py exactly.
"""

import argparse
import ipaddress
import signal
import socket
import struct
import sys
import time


def parse_int(value: str) -> int:
    return int(value, 0)


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="Send incrementing text messages as GTP-U G-PDUs over the active OCUDU MBS session."
    )
    p.add_argument("--group", required=True, help="MBS NG-U IPv4 multicast group")
    p.add_argument("--teid", required=True, type=parse_int, help="Tunnel TEID, e.g. 0x32a40000")
    p.add_argument("--port", type=int, default=2152, help="GTP-U destination port")
    p.add_argument("--interface", help="Source/interface IPv4 address; default uses the routing table")
    p.add_argument("--prefix", default="testMsg", help="Message prefix; payload is '<prefix> <N> Tx_ns=<timestamp>'")
    p.add_argument("--interval", type=float, default=1.0, help="Seconds between messages")
    p.add_argument("--duration", type=float, default=1800.0, help="Total send duration in seconds (0 = forever)")
    p.add_argument("--start-index", type=int, default=1, help="First message number N")
    p.add_argument(
        "--log-file",
        default="/tmp/mbs_text_stream.csv",
        help="CSV log for transmitted messages; use empty string to disable",
    )
    p.add_argument(
        "--no-payload-timestamp",
        action="store_true",
        help="Do not append Tx_ns=<timestamp> to the payload",
    )
    return p.parse_args()


def main() -> int:
    a = parse_args()
    if not ipaddress.ip_address(a.group).is_multicast:
        print(f"error: group is not multicast: {a.group}", file=sys.stderr)
        return 2
    if not 0 < a.teid <= 0xFFFFFFFF:
        print("error: TEID must be in the range 1..0xffffffff", file=sys.stderr)
        return 2
    if a.interval <= 0:
        print("error: interval must be greater than zero", file=sys.stderr)
        return 2

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 1)
    if a.interface:
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(a.interface))

    stop = False

    def request_stop(_signum: int, _frame: object) -> None:
        nonlocal stop
        stop = True

    signal.signal(signal.SIGINT, request_stop)
    signal.signal(signal.SIGTERM, request_stop)

    print(
        f"Broadcasting '{a.prefix} N' to {a.group}:{a.port} "
        f"TEID=0x{a.teid:08x} every {a.interval}s for {a.duration}s",
        flush=True,
    )

    n = a.start_index
    sent = 0
    started = time.monotonic()
    next_send = started
    log_fh = None
    if a.log_file:
        log_fh = open(a.log_file, "w", encoding="utf-8")
        print("seq,msg_index,tx_ns,payload_bytes,payload", file=log_fh, flush=True)

    try:
        while not stop:
            now = time.monotonic()
            if a.duration and now - started >= a.duration:
                break
            if next_send > now:
                time.sleep(next_send - now)

            tx_ns = time.time_ns()
            text = f"{a.prefix} {n}"
            if not a.no_payload_timestamp:
                text += f" Tx_ns={tx_ns}"
            msg = f"{text}\n".encode("utf-8")
            gtpu = struct.pack("!BBHI", 0x30, 0xFF, len(msg), a.teid) + msg
            sock.sendto(gtpu, (a.group, a.port))
            sent += 1
            if log_fh is not None:
                print(f'{sent},{n},{tx_ns},{len(msg)},"{text}"', file=log_fh, flush=True)
            print(f"sent #{sent}: {text} ({len(msg)} bytes)", flush=True)
            n += 1
            next_send += a.interval
    finally:
        if log_fh is not None:
            log_fh.close()

    print(f"stopped: sent={sent} messages", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

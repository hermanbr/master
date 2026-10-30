"""The command line every protocol implements:

  python -m sd.protocols.<p> advertise --service-type camera --name ue1 --iface IF
  python -m sd.protocols.<p> discover  --service-type camera --iface IF [--duration S]
"""

import argparse
import signal
import threading
from typing import Callable

Handler = Callable[[argparse.Namespace], int]


def run(prog: str, advertise: Handler, discover: Handler,
        add_discover_args: Callable[[argparse.ArgumentParser], None] | None = None) -> int:
    parser = argparse.ArgumentParser(prog=prog)
    sub = parser.add_subparsers(dest="role", required=True)

    p_adv = sub.add_parser("advertise", help="offer one service and answer discovery")
    p_adv.add_argument("--service-type", default="camera")
    p_adv.add_argument("--name", required=True, help="instance name, e.g. ue1 or sta1")
    p_adv.add_argument("--iface", required=True, help="interface to advertise on")

    p_dis = sub.add_parser("discover", help="find services of one type and print the list every 5 s")
    p_dis.add_argument("--service-type", default="camera")
    p_dis.add_argument("--iface", required=True, help="interface to discover on")
    p_dis.add_argument("--duration", type=float, help="stop after this many seconds")
    if add_discover_args:
        add_discover_args(p_dis)

    args = parser.parse_args()
    return advertise(args) if args.role == "advertise" else discover(args)


def stop_event(duration: float | None = None) -> threading.Event:
    """Event set on SIGINT/SIGTERM, or after duration seconds."""
    stop = threading.Event()
    signal.signal(signal.SIGINT, lambda *_: stop.set())
    signal.signal(signal.SIGTERM, lambda *_: stop.set())
    if duration is not None:
        timer = threading.Timer(duration, stop.set)
        timer.daemon = True
        timer.start()
    return stop

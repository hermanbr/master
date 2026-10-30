"""A Zenoh peer that scouts on --iface, autoconnects to every peer that answers with a Hello,
and queries sd/services/<service_type>/* every 2 s. Liveliness drops devices that leave."""

import argparse
import json

import zenoh

from sd.common import registry
from sd.common.cli import stop_event
from sd.protocols.zenoh.config import KEY_PREFIX, peer_config

QUERY_INTERVAL_S = 2.0


def discover(args: argparse.Namespace) -> int:
    def on_liveliness(sample: zenoh.Sample) -> None:
        key = str(sample.key_expr)
        if sample.kind == zenoh.SampleKind.PUT:
            print(f"alive: {key}", flush=True)
        else:
            print(f"gone:  {key}", flush=True)
            registry.remove(key)

    session = zenoh.open(peer_config(args.iface, autoconnect=True))
    sub = session.liveliness().declare_subscriber(f"{KEY_PREFIX}/**", on_liveliness, history=True)
    print(f"zid {session.zid()} scouting on {args.iface}, querying {KEY_PREFIX}/{args.service_type}/*", flush=True)
    registry.start_print_thread()

    stop = stop_event(args.duration)
    while not stop.is_set():
        for reply in session.get(f"{KEY_PREFIX}/{args.service_type}/*", timeout=QUERY_INTERVAL_S):
            if reply.ok is None:
                continue
            info = json.loads(reply.ok.payload.to_string())
            registry.add(str(reply.ok.key_expr), info["addr"], info["port"], info["service_type"])
        stop.wait(QUERY_INTERVAL_S)

    sub.undeclare()
    session.close()
    return 0

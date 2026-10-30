"""A Zenoh peer that answers scouting and offers one service: a liveliness token plus a
queryable on sd/services/<service_type>/<name> replying {name, service_type, addr, port}."""

import argparse
import json

import zenoh

from sd.common.cli import stop_event
from sd.common.netif import iface_ipv4
from sd.protocols.zenoh.config import KEY_PREFIX, ZENOH_PORT, peer_config


def advertise(args: argparse.Namespace) -> int:
    addr = iface_ipv4(args.iface)
    key = f"{KEY_PREFIX}/{args.service_type}/{args.name}"
    info = json.dumps({"name": args.name, "service_type": args.service_type, "addr": addr, "port": ZENOH_PORT})

    def on_query(query: zenoh.Query) -> None:
        print(f"query {query.selector} -> reply", flush=True)
        query.reply(key, info)

    session = zenoh.open(peer_config(args.iface, autoconnect=False))
    token = session.liveliness().declare_token(key)
    queryable = session.declare_queryable(key, on_query)
    print(f"{args.name}: zid {session.zid()} on {args.iface} ({addr}), listening tcp/{addr}:{ZENOH_PORT}", flush=True)
    print(f"Service: {key}", flush=True)

    stop_event().wait()
    queryable.undeclare()
    token.undeclare()
    session.close()
    return 0

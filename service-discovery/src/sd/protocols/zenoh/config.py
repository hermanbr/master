"""Zenoh peer configuration shared by advertise and discover."""

import json

import zenoh

from sd.common.netif import iface_ipv4
from sd.protocols.zenoh import GROUP, PORT

KEY_PREFIX = "sd/services"
ZENOH_PORT = 7447


def peer_config(iface: str, autoconnect: bool) -> zenoh.Config:
    """Peer that listens on iface's address only and scouts over multicast on iface.

    Listening on one explicit address keeps the Hello locators on that interface: a UE's
    container also has a Docker bridge address, which would let discover bypass 5G.
    """
    addr = iface_ipv4(iface)
    conf = {
        "mode": "peer",
        "listen": {"endpoints": [f"tcp/{addr}:{ZENOH_PORT}"]},
        "scouting": {
            "multicast": {
                "enabled": True,
                "address": f"{GROUP}:{PORT}",
                "interface": iface,
                "listen": True,
                "autoconnect": {"router": [], "peer": ["router", "peer"] if autoconnect else []},
            },
            "gossip": {"enabled": False},
        },
        "transport": {"shared_memory": {"enabled": False}},
    }
    return zenoh.Config.from_json5(json.dumps(conf))

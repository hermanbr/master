"""Minimal GTP-U (TS 29.281) G-PDU encapsulation for the MBS NG-U tunnel."""

import struct

GTPU_PORT = 2152
_G_PDU = 0xFF
_FLAGS = 0x30  # version 1, protocol type GTP, no optional fields


def encap(teid: int, payload: bytes) -> bytes:
    return struct.pack("!BBHI", _FLAGS, _G_PDU, len(payload), teid) + payload

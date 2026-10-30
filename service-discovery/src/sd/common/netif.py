"""Interface addresses, multicast sockets and network-namespace helpers."""

import ctypes
import fcntl
import os
import socket
import struct
import subprocess

SIOCGIFADDR = 0x8915
CLONE_NEWNET = 0x40000000


def iface_ipv4(iface: str, sock: socket.socket | None = None) -> str:
    """IPv4 address of an interface (e.g. oaitun_ue1, sta1-wlan0) in sock's netns (default: ours)."""
    req = struct.pack("256s", iface.encode()[:15])
    if sock is not None:
        return socket.inet_ntoa(fcntl.ioctl(sock.fileno(), SIOCGIFADDR, req)[20:24])
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        return socket.inet_ntoa(fcntl.ioctl(s.fileno(), SIOCGIFADDR, req)[20:24])


def join_group(sock: socket.socket, group: str, local_addr: str = "0.0.0.0") -> None:
    """Join an IPv4 multicast group on the interface with local_addr; broadcast addresses need no join."""
    first_octet = int(group.split(".")[0])
    if 224 <= first_octet <= 239:
        mreq = socket.inet_aton(group) + socket.inet_aton(local_addr)
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)


def multicast_tx(sock: socket.socket, local_addr: str, loop: bool = True) -> None:
    """Send multicast from local_addr's interface, TTL 1; broadcast allowed for --dest overrides.

    Loop stays on so a relay on the same host (the 5G MBS bridge) hears it too.
    """
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(local_addr))
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 1)
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, int(loop))
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)


def _setns(fd: int) -> None:
    """os.setns is Python 3.12+; the venv may be 3.10, so fall back to libc."""
    if hasattr(os, "setns"):
        os.setns(fd, CLONE_NEWNET)
        return
    libc = ctypes.CDLL(None, use_errno=True)
    if libc.setns(fd, CLONE_NEWNET) != 0:
        err = ctypes.get_errno()
        raise OSError(err, os.strerror(err))


def container_udp_socket(container: str, iface: str) -> socket.socket:
    """UDP socket created in a container's netns and bound to iface (e.g. a UE's oaitun_ue1).

    Needs root (setns). The socket stays in the container's netns after we switch back.
    """
    pid = subprocess.check_output(["docker", "inspect", "-f", "{{.State.Pid}}", container], text=True).strip()
    host_ns = os.open("/proc/self/ns/net", os.O_RDONLY)
    ns = os.open(f"/proc/{pid}/ns/net", os.O_RDONLY)
    try:
        _setns(ns)
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_BINDTODEVICE, iface.encode())
    finally:
        _setns(host_ns)
        os.close(ns)
        os.close(host_ns)
    return s

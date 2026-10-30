"""Discovered-services table, printed in one format by every protocol's discover (parsed by check.py)."""

import threading
import time

PRINT_INTERVAL_S = 5.0
LIST_HEADER = "--- {n} service(s) ---"

_services: dict[str, dict] = {}  # key -> {addr, port, service_type, last_update}
_lock = threading.Lock()  # protocol rx threads write while the print loop reads


def add(key: str, addr: str, port: int, service_type: str) -> None:
    with _lock:
        new = key not in _services
        _services[key] = {"addr": addr, "port": port, "service_type": service_type,
                          "last_update": time.monotonic()}
    if new:
        print(f"discovered {key} at {addr}:{port}", flush=True)


def remove(key: str) -> None:
    with _lock:
        _services.pop(key, None)


def print_list() -> None:
    with _lock:
        entries = list(_services.values())
    now = time.monotonic()
    print(LIST_HEADER.format(n=len(entries)), flush=True)
    for e in entries:
        print(f"Addr: {e['addr']}:{e['port']} ({e['service_type']})")
        print(f"---- Last updated: {now - e['last_update']:.1f}s ago", flush=True)


def _print_loop() -> None:
    while True:
        time.sleep(PRINT_INTERVAL_S)
        print_list()


def start_print_thread() -> threading.Thread:
    thread = threading.Thread(target=_print_loop, daemon=True)
    thread.start()
    return thread

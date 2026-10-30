"""Pass/fail for a discover log: the newest service list must hold exactly the expected addresses.

Usage: python -m sd.common.check <discover.log> <expected ip> ...   (exit 0 on match)
"""

import re
import sys

_HEADER = re.compile(r"^--- \d+ service\(s\) ---$", re.M)


def discovered_ips(log_path: str) -> set[str]:
    with open(log_path) as f:
        text = f.read()
    # registry prints the whole list every 5 s; only the newest block counts
    blocks = _HEADER.split(text)
    return set(re.findall(r"^Addr: ([0-9.]+):", blocks[-1], re.M)) if len(blocks) > 1 else set()


def check(log_path: str, expected: set[str]) -> bool:
    got = discovered_ips(log_path)
    print(f"==> Expected {sorted(expected)}, discovered {sorted(got)}", flush=True)
    ok = bool(expected) and got == expected
    print("==> PASS" if ok else f"==> FAIL (see {log_path})", flush=True)
    return ok


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    sys.exit(0 if check(sys.argv[1], set(sys.argv[2:])) else 1)

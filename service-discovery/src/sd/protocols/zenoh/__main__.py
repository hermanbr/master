from sd.common.cli import run
from sd.protocols.zenoh.advertise import advertise
from sd.protocols.zenoh.discover import discover

raise SystemExit(run("sd.protocols.zenoh", advertise, discover))

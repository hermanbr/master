from sd.common.cli import run
from sd.protocols.mdns.advertise import advertise
from sd.protocols.mdns.discover import discover

raise SystemExit(run("sd.protocols.mdns", advertise, discover))

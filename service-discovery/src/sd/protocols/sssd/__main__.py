from sd.common.cli import run
from sd.protocols.sssd.advertise import advertise
from sd.protocols.sssd.discover import add_args, discover

raise SystemExit(run("sd.protocols.sssd", advertise, discover, add_args))

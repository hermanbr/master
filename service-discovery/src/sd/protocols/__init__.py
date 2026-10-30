"""Discovery protocols. Each subpackage exports GROUP and PORT (what the 5G MBS bridge carries)
and implements the advertise/discover command line in sd.common.cli. None of them knows the platform."""

PROTOCOLS = ("sssd", "mdns", "zenoh")

import json
from dataclasses import asdict, dataclass
from enum import Enum


class MsgType(str, Enum):  # str mixin instead of StrEnum (3.11+): keeps Python 3.10 working
    QUERY = "query"
    RESPONSE = "response"


@dataclass
class Message:
    msg_type: str
    service_type: str
    name: str = ""  # responses only
    addr: str = ""  # responses only: the advertiser's own address (the reply may come via a relay)
    protocol: str = "sssd"

    def to_bytes(self) -> bytes:
        # compact separators = fewer bytes on the wire
        return json.dumps(asdict(self), separators=(",", ":")).encode("utf-8")

    @classmethod
    def from_bytes(cls, data: bytes) -> "Message":
        return cls(**json.loads(data.decode("utf-8")))

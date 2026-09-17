"""Pulse v1 binary framing. All multibyte integers are little-endian."""
import binascii
import math
import struct
from dataclasses import dataclass

MAGIC = b'PL'
VERSION = 1
HELLO, SET, STOP = 1, 2, 3
ACK_HELLO, ACK_SET, ACK_STOP = 0x81, 0x82, 0x83
MAX_PAYLOAD = 32


@dataclass(frozen=True)
class Packet:
    kind: int
    sequence: int
    payload: bytes


def encode(kind: int, sequence: int, payload: bytes = b'') -> bytes:
    if len(payload) > MAX_PAYLOAD:
        raise ValueError('Payload exceeds 32 bytes')
    body = struct.pack('<BBHB', VERSION, kind, sequence & 0xFFFF, len(payload)) + payload
    return MAGIC + body + struct.pack('<H', binascii.crc_hqx(body, 0xFFFF))


def metric_payload(metric: int, value: float, brightness: int, speed: int) -> bytes:
    if metric not in (0, 1, 2) or not math.isfinite(value) or not 0 <= value <= 100:
        raise ValueError('Invalid metric')
    if not 0 <= brightness <= 100 or not 50 <= speed <= 200:
        raise ValueError('Invalid effect setting')
    return struct.pack('<BHBB', metric, round(value * 100), brightness, speed)


class Parser:
    def __init__(self):
        self.buffer = bytearray()

    def feed(self, data: bytes) -> list[Packet]:
        self.buffer.extend(data)
        packets = []
        while True:
            offset = self.buffer.find(MAGIC)
            if offset < 0:
                self.buffer[:] = self.buffer[-1:] if self.buffer.endswith(b'P') else b''
                break
            if offset:
                del self.buffer[:offset]
            if len(self.buffer) < 7:
                break
            version, kind, seq, size = struct.unpack_from('<BBHB', self.buffer, 2)
            if version != VERSION or size > MAX_PAYLOAD:
                del self.buffer[0]
                continue
            length = 9 + size
            if len(self.buffer) < length:
                break
            expected = struct.unpack_from('<H', self.buffer, length - 2)[0]
            if binascii.crc_hqx(self.buffer[2:length-2], 0xFFFF) != expected:
                del self.buffer[0]
                continue
            packets.append(Packet(kind, seq, bytes(self.buffer[7:length-2])))
            del self.buffer[:length]
        return packets

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from pulse.device import Device
from pulse.protocol import ACK_HELLO, ACK_SET, HELLO, SET, STOP, Parser, encode


class Wire:
    def __init__(self, **kwargs):
        self.options = kwargs
        self.rx = bytearray()
        self.tx = []
        self.closed = False

    def open(self):
        assert self.dtr is False and self.rts is False

    def close(self):
        self.closed = True

    def write(self, data):
        self.tx.append(Parser().feed(data)[0])
        return len(data)

    @property
    def in_waiting(self):
        return len(self.rx)

    def read(self, count):
        data = bytes(self.rx[:count])
        del self.rx[:count]
        return data


def rig():
    now = [10.0]
    wire = Wire()
    d = Device(factory=lambda **kw: wire, clock=lambda: now[0])
    d.connect('COM99')
    d.poll()
    return d, wire, now


def ready(d, w):
    w.rx.extend(encode(ACK_HELLO, w.tx[-1].sequence, b'\x00'))
    d.poll()


def test_port_open_is_not_device_ready_and_unmatched_ack_is_ignored():
    d, w, now = rig()
    assert d.state == 'handshake'
    d.update(0, 50, 80, 100)
    assert [p.kind for p in w.tx] == [HELLO]
    w.rx.extend(encode(ACK_HELLO, 900, b'\x00'))
    d.poll()
    assert d.state == 'handshake'
    ready(d, w)
    assert d.state == 'ready'
    d.update(0, 50, 80, 100)
    assert w.tx[-1].kind == SET


def test_pause_missing_sensor_and_disconnect_stop_output():
    d, w, now = rig()
    ready(d, w)
    d.update(1, None, 80, 100)
    assert w.tx[-1].kind == STOP
    d.update(0, 50, 80, 100, paused=True)
    assert w.tx[-1].kind == STOP
    d.disconnect()
    assert w.tx[-1].kind == STOP and w.closed
    now[0] += 10
    d.poll()
    assert d.state == 'disconnected'


def test_old_firmware_times_out_without_sending_load_commands():
    d, w, now = rig()
    now[0] += 4
    d.poll()
    assert d.state == 'unsupported'
    d.update(0, 70, 80, 100)
    assert all(p.kind == HELLO for p in w.tx)


def test_ready_device_with_no_ack_stops_streaming():
    d, w, now = rig()
    ready(d, w)
    now[0] += 4
    d.poll()
    assert d.state == 'handshake'


def test_negative_ack_does_not_recover_on_other_success_ack():
    d, w, now = rig()
    ready(d, w)
    d.update(0, 50, 80, 100)
    first = w.tx[-1]
    d.update(0, 50, 80, 100)
    second = w.tx[-1]
    w.rx.extend(encode(ACK_SET, first.sequence, b'\x01'))
    w.rx.extend(encode(ACK_SET, second.sequence, b'\x00'))
    d.poll()
    assert d.state == 'rejected'


def test_disabled_reconnect_does_not_open_later_without_user_action():
    d, w, now = rig()
    d.auto_reconnect = False
    d._failed('unplugged')
    assert d.state == 'error' and not d.desired
    d.auto_reconnect = True
    now[0] += 20
    d.poll()
    assert d.serial is None


def test_enabled_reconnect_observes_backoff_and_restarts_handshake():
    d, w, now = rig()
    d._failed('unplugged')
    assert d.state == 'retry'
    now[0] += 2
    d.poll()
    assert d.serial is None
    now[0] += 2
    d.poll()
    assert d.state == 'handshake'


def test_crc_known_reference():
    assert encode(HELLO, 1).hex() == '504c0101010000d9fa'

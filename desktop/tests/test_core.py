import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))


def test_protocol_roundtrip_and_fragment_recovery():
    from pulse.protocol import Parser, encode, SET
    wire = encode(SET, 65535, b'\x00\x88\x13\x50\x64')
    parser = Parser()
    assert parser.feed(b'boot log\r\n' + wire[:5]) == []
    packets = parser.feed(wire[5:] + wire)
    assert [(p.kind, p.sequence, p.payload) for p in packets] == [
        (SET, 65535, b'\x00\x88\x13\x50\x64')] * 2


def test_protocol_rejects_corruption_and_resynchronizes():
    from pulse.protocol import Parser, encode, HELLO
    good = encode(HELLO, 8)
    damaged = bytearray(good)
    damaged[-1] ^= 1
    assert len(Parser().feed(bytes(damaged) + good)) == 1


def test_protocol_rejects_unbounded_payload_and_metric():
    from pulse.protocol import encode, metric_payload
    with pytest.raises(ValueError):
        encode(1, 0, b'x' * 33)
    with pytest.raises(ValueError):
        metric_payload(0, float('nan'), 80, 100)
    assert metric_payload(2, 50, 80, 100) == b'\x02\x88\x13\x50\x64'


def test_effect_segment_boundaries_and_caps():
    from pulse.effect import duties
    assert duties(600, 0) == [0] * 7
    assert duties(600, 20)[1] == 80
    assert duties(600, 20)[2:] == [0] * 5
    assert duties(1200, 50)[3] == 50
    assert duties(1200, 100) == [0, 80, 100, 100, 100, 100, 0]
    assert duties(1200, None) == [0] * 7
    assert duties(600, 100, brightness=50)[2] == 50


def test_effect_speed_preserves_same_curve():
    from pulse.effect import duties
    assert duties(300, 50, speed=200) == duties(600, 50)


def test_smoothing_does_not_turn_missing_sensor_into_zero():
    from pulse.effect import Smoother
    s = Smoother()
    assert s.update(20) == 20
    assert 20 < s.update(80) < 80
    assert s.update(None) is None
    assert s.update(40) == 40

"""Serial state machine; only matching, fresh ACKs establish device readiness."""
import time

import serial
from serial.tools import list_ports

from .protocol import HELLO, SET, STOP, ACK_HELLO, Parser, encode, metric_payload


def ports():
    return [dict(port=p.device, description=p.description,
                 ch340=p.vid == 0x1A86, identity=(p.serial_number, p.location, p.vid, p.pid))
            for p in list_ports.comports()]


class Device:
    def __init__(self, factory=None, clock=time.monotonic):
        self.factory = factory or serial.Serial
        self.clock = clock
        self.serial = None
        self.port = ''
        self.desired = False
        self.auto_reconnect = True
        self.state = 'disconnected'
        self.detail = '选择 CH340 串口后连接开发板'
        self.parser = Parser()
        self.sequence = 0
        self.pending = {}
        self.last_ack = 0.0
        self.last_hello = -100.0
        self.opened = 0.0
        self.retry_at = 0.0
        self.sent = 0
        self.received = 0
        self.rtt_ms = None
        self.events = []
        self.rejected = False

    def event(self, message):
        self.events.append(message)
        self.events = self.events[-40:]

    def connect(self, port):
        self.disconnect()
        self.port = port
        self.desired = True
        self._open()

    def _open(self):
        candidate = None
        try:
            candidate = self.factory(port=None, baudrate=115200, bytesize=8,
                                     parity='N', stopbits=1, timeout=0, write_timeout=0.15,
                                     xonxoff=False, rtscts=False, dsrdtr=False)
            candidate.dtr = False
            candidate.rts = False
            candidate.port = self.port
            candidate.open()
            self.serial = candidate
            self.parser = Parser()
            self.pending.clear()
            self.last_ack = 0
            self.last_hello = -100
            self.opened = self.clock()
            self.rejected = False
            self.state = 'handshake'
            self.detail = '串口已打开，正在等待 Pulse v1 固件应答'
            self.event(f'{self.port} 已打开 · 115200 / 8N1')
        except (OSError, serial.SerialException) as error:
            if candidate is not None:
                try:
                    candidate.close()
                except Exception:
                    pass
            self._failed(str(error))

    def _failed(self, reason):
        if self.serial is not None:
            try:
                self.serial.close()
            except Exception:
                pass
        self.serial = None
        self.pending.clear()
        if not self.auto_reconnect:
            self.desired = False
        self.state = 'retry' if self.desired and self.auto_reconnect else 'error'
        self.detail = f'串口不可用：{reason}'
        self.retry_at = self.clock() + 3
        self.event(self.detail)

    def _send(self, kind, payload=b''):
        if self.serial is None:
            return
        self.sequence = (self.sequence + 1) & 0xFFFF
        wire = encode(kind, self.sequence, payload)
        try:
            if self.serial.write(wire) != len(wire):
                raise serial.SerialTimeoutException('串口写入不完整')
            self.pending[self.sequence] = (kind, self.clock())
            self.sent += 1
        except (OSError, serial.SerialException) as error:
            self._failed(str(error))

    def poll(self):
        now = self.clock()
        if self.serial is None:
            if self.desired and self.auto_reconnect and now >= self.retry_at:
                self._open()
            return
        try:
            for packet in self.parser.feed(self.serial.read(min(4096, self.serial.in_waiting))):
                expected = self.pending.get(packet.sequence)
                if expected is None or packet.kind != (expected[0] | 0x80):
                    continue
                if now - expected[1] > 3 or len(packet.payload) != 1:
                    continue
                del self.pending[packet.sequence]
                if packet.payload != b'\x00':
                    self.state = 'rejected'
                    self.rejected = True
                    self.detail = f'固件拒绝命令，状态码 {packet.payload[0]}；请重新连接'
                    self.event(self.detail)
                    continue
                if self.rejected:
                    continue
                self.received += 1
                self.rtt_ms = round((now - expected[1]) * 1000)
                self.last_ack = now
                if packet.kind == ACK_HELLO or self.state == 'ready':
                    if self.state != 'ready':
                        self.event('开发板已应答 · Pulse v1 握手完成')
                    self.state = 'ready'
                    self.detail = '开发板已应答，正在同步所选指标'
        except (OSError, serial.SerialException) as error:
            self._failed(str(error))
            return
        self.pending = {seq: item for seq, item in self.pending.items() if now - item[1] < 3}
        if self.state == 'ready' and now - self.last_ack > 3:
            self.state = 'handshake'
            self.detail = '开发板应答超时，正在重新握手'
            self.event(self.detail)
        if self.state in ('handshake', 'unsupported'):
            if now - self.last_hello >= 1:
                self.last_hello = now
                self._send(HELLO)
            if self.state != 'unsupported' and now - self.opened > 3 and not self.last_ack:
                self.state = 'unsupported'
                self.detail = '串口已打开，固件未应答；需接入 Pulse v1 协议'
                self.event(self.detail)

    def update(self, metric, value, brightness, speed, paused=False):
        if self.state != 'ready':
            return
        if paused or value is None:
            self._send(STOP)
        else:
            self._send(SET, metric_payload(metric, value, brightness, speed))

    def disconnect(self):
        self.desired = False
        if self.serial is not None:
            if self.state == 'ready':
                self._send(STOP)
            if self.serial is not None:
                try:
                    self.serial.close()
                except Exception:
                    pass
        self.serial = None
        self.pending.clear()
        self.state = 'disconnected'
        self.detail = '已断开 · 本地监控和预览仍可使用'

    def status(self):
        events, self.events = self.events, []
        return dict(state=self.state, detail=self.detail, port=self.port, sent=self.sent,
                    received=self.received, rtt=self.rtt_ms, events=events)

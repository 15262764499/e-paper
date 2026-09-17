import queue
import threading
import time

from PySide6.QtCore import QThread, Signal

from .device import Device, ports
from .effect import Smoother
from .telemetry import Telemetry


class MonitorWorker(QThread):
    snapshot = Signal(object, object)
    status = Signal(object)
    port_list = Signal(object)
    gpu_list = Signal(object)
    problem = Signal(str)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.commands = queue.Queue()
        self.stopping = threading.Event()

    def command(self, name, value=None):
        self.commands.put((name, value))

    def run(self):
        device = Device()
        monitor = None
        try:
            monitor = Telemetry()
            self.gpu_list.emit([name for _, name in monitor.gpus])
            metric, brightness, speed, paused = 0, 80, 100, False
            smooth = [Smoother() for _ in range(3)]
            last_sample = time.monotonic()
            last_ports = -100.0
            while not self.stopping.is_set():
                while not self.commands.empty():
                    name, value = self.commands.get_nowait()
                    if name == 'connect':
                        device.connect(value)
                    elif name == 'disconnect':
                        device.disconnect()
                    elif name == 'config':
                        metric, brightness, speed, paused = value
                    elif name == 'gpu':
                        monitor.gpu_index = value
                        smooth[1] = Smoother()
                    elif name == 'retry':
                        device.auto_reconnect = value
                    elif name == 'refresh':
                        last_ports = -100.0
                now = time.monotonic()
                if now - last_ports >= 3:
                    self.port_list.emit(ports())
                    last_ports = now
                device.poll()
                if now - last_sample >= 0.5:
                    sample = monitor.sample()
                    values = [smooth[i].update(v) for i, v in enumerate(
                        (sample.cpu, sample.gpu, sample.memory))]
                    self.snapshot.emit(sample, values)
                    device.update(metric, values[metric], brightness, speed, paused)
                    last_sample = now
                self.status.emit(device.status())
                self.stopping.wait(0.05)
        except Exception as error:
            self.problem.emit(f'监控任务出错：{error}')
        finally:
            device.disconnect()
            if monitor:
                monitor.close()

    def stop(self):
        self.stopping.set()

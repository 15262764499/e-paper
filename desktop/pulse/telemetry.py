"""Local-only monitoring. Unavailable GPU values remain None."""
import platform
import time
from dataclasses import dataclass

import psutil


@dataclass
class Snapshot:
    cpu: float | None
    gpu: float | None
    memory: float | None
    memory_used: float
    memory_total: float
    gpu_name: str
    gpu_memory_used: float | None
    gpu_memory_total: float | None
    gpu_error: str
    timestamp: float


class Telemetry:
    def __init__(self):
        self.nvml = None
        self.gpus = []
        self.gpu_index = 0
        self.error = ''
        psutil.cpu_percent(None)  # prime the interval; do not publish this value
        try:
            import pynvml
            pynvml.nvmlInit()
            self.nvml = pynvml
            for i in range(pynvml.nvmlDeviceGetCount()):
                handle = pynvml.nvmlDeviceGetHandleByIndex(i)
                name = pynvml.nvmlDeviceGetName(handle)
                if isinstance(name, bytes):
                    name = name.decode('utf-8', 'replace')
                self.gpus.append((handle, name))
            if not self.gpus:
                self.error = '未检测到 NVIDIA 显卡；CPU 和内存仍可使用'
        except Exception:
            self.error = 'NVIDIA 数据不可用，请检查显卡驱动；CPU 和内存仍可使用'

    def sample(self):
        memory = psutil.virtual_memory()
        gpu, used, total = None, None, None
        name = 'GPU 暂不可用'
        error = self.error
        if self.gpus:
            handle, name = self.gpus[min(self.gpu_index, len(self.gpus) - 1)]
            try:
                gpu = float(self.nvml.nvmlDeviceGetUtilizationRates(handle).gpu)
                info = self.nvml.nvmlDeviceGetMemoryInfo(handle)
                used, total = info.used / 2**30, info.total / 2**30
                error = ''
            except Exception:
                error = 'GPU 暂未返回有效数据（可能处于休眠状态）'
        return Snapshot(float(psutil.cpu_percent(None)), gpu, memory.percent,
                        (memory.total - memory.available) / 2**30, memory.total / 2**30,
                        name, used, total, error, time.monotonic())

    def close(self):
        if self.nvml:
            try:
                self.nvml.nvmlShutdown()
            except Exception:
                pass

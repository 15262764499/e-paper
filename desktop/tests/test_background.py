import os
import sys
from pathlib import Path

os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))


def test_startup_command_handles_spaces_and_hidden_argument():
    from pulse.background import startup_command
    assert startup_command('C:/My Apps/Pulse.exe') == '"C:/My Apps/Pulse.exe" --start-hidden'
    command = startup_command('C:/Python/python.exe', 'D:/My Project/main.py')
    assert command == 'C:/Python/pythonw.exe "D:/My Project/main.py" --start-hidden'


def test_close_to_tray_keeps_worker_running_and_can_restore(tmp_path, monkeypatch):
    from PySide6.QtCore import QSettings
    from PySide6.QtWidgets import QApplication, QSystemTrayIcon
    from pulse.ui import MainWindow
    app = QApplication.instance() or QApplication([])
    monkeypatch.setattr(QSystemTrayIcon, 'isSystemTrayAvailable', lambda: True)
    settings = QSettings(str(tmp_path/'tray.ini'), QSettings.Format.IniFormat)
    window = MainWindow(start_worker=False, settings=settings)
    window.show()
    window.close()
    assert not window.isVisible()
    assert not window.worker.stopping.is_set()
    assert window.tray.isVisible()
    window.restore_window()
    assert window.isVisible()
    window.request_exit()
    assert window.worker.stopping.is_set()
    assert not window.tray.isVisible()


def test_registry_startup_uses_only_own_value(monkeypatch):
    import winreg
    from pulse import background
    values = {'AnotherApp': 'unchanged'}

    class Key:
        def __enter__(self):
            return self

        def __exit__(self, *args):
            pass

    def query(key, name):
        if name not in values:
            raise FileNotFoundError(name)
        return values[name], winreg.REG_SZ

    def delete(key, name):
        if name not in values:
            raise FileNotFoundError(name)
        del values[name]

    monkeypatch.setattr(winreg, 'OpenKey', lambda *args: Key())
    monkeypatch.setattr(winreg, 'CreateKeyEx', lambda *args: Key())
    monkeypatch.setattr(winreg, 'QueryValueEx', query)
    monkeypatch.setattr(winreg, 'SetValueEx', lambda key, name, reserved, kind, value: values.update({name: value}))
    monkeypatch.setattr(winreg, 'DeleteValue', delete)
    assert not background.startup_enabled()
    background.set_startup(True)
    assert background.startup_enabled()
    assert values[background.VALUE_NAME].endswith('--start-hidden')
    values[background.VALUE_NAME] = 'C:/old/Pulse.exe --start-hidden'
    assert background.startup_enabled()
    background.set_startup(False)
    background.set_startup(False)
    assert values == {'AnotherApp': 'unchanged'}


def test_no_tray_falls_back_to_visible_window_and_normal_exit(tmp_path, monkeypatch):
    from PySide6.QtCore import QSettings
    from PySide6.QtWidgets import QApplication, QSystemTrayIcon
    from pulse.ui import MainWindow
    app = QApplication.instance() or QApplication([])
    monkeypatch.setattr(QSystemTrayIcon, 'isSystemTrayAvailable', lambda: False)
    settings = QSettings(str(tmp_path/'fallback.ini'), QSettings.Format.IniFormat)
    window = MainWindow(start_worker=False, settings=settings)
    window.start_visibility(True)
    assert window.isVisible()
    window.close()
    assert window.worker.stopping.is_set()


def test_hidden_worker_continues_device_updates(tmp_path, monkeypatch):
    import time
    from types import SimpleNamespace
    from PySide6.QtCore import QSettings
    from PySide6.QtWidgets import QApplication, QSystemTrayIcon
    from pulse.ui import MainWindow
    from pulse import workers
    from pulse.device import Device
    updates = []
    disconnected = []

    class FakeDevice(Device):
        def update(self, *args):
            updates.append(args)

        def disconnect(self):
            disconnected.append(True)

    class FakeTelemetry:
        gpus = []

        def sample(self):
            return SimpleNamespace(cpu=40, gpu=None, memory=60, gpu_name='Unavailable',
                                   gpu_error='', memory_used=6, memory_total=10)

        def close(self):
            pass

    monkeypatch.setattr(workers, 'Device', FakeDevice)
    monkeypatch.setattr(workers, 'Telemetry', FakeTelemetry)
    monkeypatch.setattr(workers, 'ports', lambda: [])
    monkeypatch.setattr(QSystemTrayIcon, 'isSystemTrayAvailable', lambda: True)
    app = QApplication.instance() or QApplication([])
    window = MainWindow(settings=QSettings(str(tmp_path/'worker.ini'), QSettings.Format.IniFormat))
    window.show()
    window.close()
    try:
        deadline = time.monotonic() + 3
        while len(updates) < 2 and time.monotonic() < deadline:
            app.processEvents()
            time.sleep(0.02)
        assert not window.isVisible()
        assert len(updates) >= 2
        assert updates[-1][1] == 40
    finally:
        window.request_exit()
        window.worker.wait(3000)
        app.processEvents()
    assert disconnected
    assert not window.worker.isRunning()

import os
import sys
from pathlib import Path

os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from PySide6.QtCore import QSettings, Qt
from PySide6.QtWidgets import QApplication
from PySide6.QtTest import QTest
from pulse.ui import MainWindow, STYLE


def test_ui_controls_and_connection_states(tmp_path):
    app = QApplication.instance() or QApplication([])
    app.setStyleSheet(STYLE)
    settings = QSettings(str(tmp_path/'settings.ini'), QSettings.Format.IniFormat)
    window = MainWindow(start_worker=False, settings=settings)
    window.show()
    app.processEvents()
    assert callable(window.metric)  # must not shadow QWidget's paint-device metric()
    QTest.mouseClick(window.cards[2], Qt.MouseButton.LeftButton)
    assert window.selected_metric == 2
    assert window.nav[2].isChecked()
    window.brightness.setValue(34)
    window.speed.setValue(150)
    assert window.brightness.value_label.text() == '34%'
    QTest.mouseClick(window.pause_button, Qt.MouseButton.LeftButton)
    assert window.paused
    window.values = [50, 20, 70]
    window.animate()
    assert window.preview.levels == [0]*7
    window.on_ports([dict(port='COM4', description='USB-SERIAL CH340', ch340=True)])
    QTest.mouseClick(window.connect_button, Qt.MouseButton.LeftButton)
    assert window.connection_requested
    assert not window.port_box.isEnabled()
    window.on_ports([dict(port='COM5', description='Unrelated UART', ch340=False)])
    assert window.port_box.currentData() == 'COM4'
    assert '已拔出' in window.port_box.currentText()
    window.on_status(dict(state='unsupported', detail='固件未应答', sent=4,
                          received=0, rtt=None, events=['probe']))
    assert '固件未应答' in window.connection_badge.text()
    window.animate()
    assert window.preview_badge.text() != '已同步'
    QTest.mouseClick(window.connect_button, Qt.MouseButton.LeftButton)
    assert not window.connection_requested
    window.close()
    assert int(settings.value('brightness')) == 34
    assert int(settings.value('metric')) == 2


def test_close_does_not_block_gui_while_worker_finishes(tmp_path):
    import time
    from PySide6.QtCore import QThread

    class SlowWorker(QThread):
        def run(self):
            self.msleep(250)

        def stop(self):
            pass

    app = QApplication.instance() or QApplication([])
    settings = QSettings(str(tmp_path/'close.ini'), QSettings.Format.IniFormat)
    window = MainWindow(start_worker=False, settings=settings)
    original_worker = window.worker
    window.worker = SlowWorker(window)
    window.show()
    window.worker.start()
    before = time.monotonic()
    window.close()
    elapsed = time.monotonic() - before
    assert elapsed < 0.15
    deadline = time.monotonic() + 2
    while window.isVisible() and time.monotonic() < deadline:
        app.processEvents()
        time.sleep(0.01)  # let the Python QThread.run() acquire the GIL
    assert not window.isVisible()
    assert not window.worker.isRunning()

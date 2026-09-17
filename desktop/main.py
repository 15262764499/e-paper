"""Run Pulse; --smoke-test renders real telemetry without opening a serial port."""
import argparse
import json
import os
from pathlib import Path
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--start-hidden', action='store_true')
    parser.add_argument('--smoke-test', action='store_true')
    parser.add_argument('--screenshot', type=Path)
    parser.add_argument('--size', default='1320x920')
    parser.add_argument('--metric', choices=['cpu', 'gpu', 'memory'])
    parser.add_argument('--export-icon', type=Path)
    args = parser.parse_args()
    if args.smoke_test or args.export_icon:
        os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')
    from PySide6.QtCore import QTimer, QSettings
    from PySide6.QtGui import QFontDatabase
    from PySide6.QtWidgets import QApplication
    from pulse.ui import MainWindow, STYLE
    from pulse.widgets import app_icon, font
    app = QApplication(sys.argv[:1])
    # Qt's offscreen platform does not enumerate Windows fonts automatically.
    if os.environ.get('QT_QPA_PLATFORM') == 'offscreen':
        font_dir = Path(os.environ.get('WINDIR', 'C:/Windows')) / 'Fonts'
        for filename in ('msyh.ttc', 'msyhbd.ttc', 'segoeui.ttf', 'seguisb.ttf'):
            if (font_dir / filename).exists():
                QFontDatabase.addApplicationFont(str(font_dir / filename))
    app.setApplicationName('Pulse')
    app.setOrganizationName('EPaper')
    app.setFont(font(10))
    app.setStyleSheet(STYLE)
    if args.export_icon:
        args.export_icon.parent.mkdir(parents=True, exist_ok=True)
        return 0 if app_icon().pixmap(128, 128).save(str(args.export_icon), 'ICO') else 1
    settings = None
    if args.smoke_test:
        import tempfile
        settings = QSettings(str(Path(tempfile.gettempdir())/'pulse-smoke.ini'), QSettings.Format.IniFormat)
    window = MainWindow(settings=settings)
    if args.metric:
        window.select_metric(['cpu', 'gpu', 'memory'].index(args.metric))
    window.resize(*map(int, args.size.split('x')))
    if not args.smoke_test:
        available = app.primaryScreen().availableGeometry()
        window.resize(min(window.width(), available.width()-24),
                      min(window.height(), available.height()-48))
    window.start_visibility(args.start_hidden or window.settings.value('start_hidden', False, type=bool))
    result = [0]
    def capture():
        if args.screenshot:
            args.screenshot.parent.mkdir(parents=True, exist_ok=True)
            if not window.grab().save(str(args.screenshot)):
                result[0] = 2
        if args.smoke_test:
            report = dict(cpu=window.sample.cpu if window.sample else None,
                          gpu=window.sample.gpu if window.sample else None,
                          memory=window.sample.memory if window.sample else None,
                          window_visible=window.isVisible(), tray_visible=bool(window.tray and window.tray.isVisible()),
                          state=window.state, width=window.width(), height=window.height(),
                          samples=len(window.chart.history[0]))
            if window.sample is None:
                result[0] = 1
            report['exit_code'] = result[0]
            if args.screenshot:
                args.screenshot.with_suffix('.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
            if sys.stdout:
                print(json.dumps(report))
            window.request_exit()
    if args.smoke_test or args.screenshot:
        QTimer.singleShot(7000, capture)
    app.exec()
    # Also release resources if Windows ends the session through Qt directly.
    window.worker.stop()
    window.worker.wait()
    return result[0]


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except Exception:
        import traceback
        import tempfile
        logfile = Path(tempfile.gettempdir())/'pulse-error.log'
        logfile.write_text(traceback.format_exc(), encoding='utf-8')
        if sys.stderr:
            traceback.print_exc()
        else:
            import ctypes
            ctypes.windll.user32.MessageBoxW(0, f'启动失败，详细信息已保存到：\n{logfile}', 'Pulse', 16)
        raise SystemExit(1)

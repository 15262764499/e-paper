import json
import platform
import time
from pathlib import Path

from PySide6.QtCore import Qt, QTimer, QSettings, Signal
from PySide6.QtGui import QDesktopServices, QFont
from PySide6.QtWidgets import (QApplication, QMainWindow, QWidget, QFrame, QLabel,
    QVBoxLayout, QHBoxLayout, QPushButton, QComboBox, QSlider, QCheckBox,
    QScrollArea, QPlainTextEdit, QMessageBox, QDialog, QSizePolicy, QSystemTrayIcon, QMenu)

from .widgets import label, font, MetricCard, LedPreview, HistoryChart, app_icon, COLORS, MINT, MUTED, TEXT
from .effect import duties
from .workers import MonitorWorker

STYLE = '''
QMainWindow, QWidget#Root { background: #10151c; color: #edf1f3; }
QWidget { color: #edf1f3; }
QFrame#Sidebar { background: #131920; border-right: 1px solid #27303a; }
QFrame#Panel { background: #191f27; border: 1px solid #2b323c; border-radius: 14px; }
QFrame#Rule { background: #2b333e; max-height: 1px; }
QPushButton { border: 1px solid #35404d; background: #222b35; border-radius: 8px;
    padding: 9px 14px; color: #dbe3e9; }
QPushButton:hover { background: #2c3844; border-color: #617385; }
QPushButton:pressed { background: #344451; }
QPushButton:disabled { color: #566170; background: #1b222b; border-color: #29313b; }
QPushButton#Primary { background: #8ee4c6; color: #122920; border-color: #8ee4c6; font-weight: bold; }
QPushButton#Primary:hover { background: #b1f4dd; }
QPushButton#Primary:disabled { background: #293d37; color: #648074; border-color: #293d37; }
QPushButton#Nav { background: transparent; border-color: transparent; text-align: left;
    padding: 12px 14px; color: #95a1ae; }
QPushButton#Nav:checked { background: #23352f; color: #a6edcf; border: 1px solid #355046; }
QPushButton#Nav:hover { background: #202b34; }
QComboBox { background: #121820; border: 1px solid #36404d; border-radius: 7px;
    padding: 9px 10px; min-height: 18px; }
QComboBox::drop-down { border: none; width: 22px; }
QComboBox QAbstractItemView { background: #222b35; color: #edf1f3; selection-background-color: #35584b; padding: 5px; }
QSlider::groove:horizontal { height: 5px; background: #36404c; border-radius: 2px; }
QSlider::sub-page:horizontal { background: #8ee4c6; border-radius: 2px; }
QSlider::handle:horizontal { width: 14px; height: 14px; margin: -5px 0; background: #dbfff0;
    border-radius: 7px; border: 1px solid #8ee4c6; }
QCheckBox { spacing: 8px; color: #aab6c2; }
QCheckBox::indicator { width: 15px; height: 15px; border: 1px solid #485766; border-radius: 4px; background: #141a22; }
QCheckBox::indicator:checked { background: #8ee4c6; border-color: #8ee4c6; image: none; }
QScrollArea { background: transparent; border: none; }
QScrollBar:vertical { width: 6px; background: transparent; }
QScrollBar::handle:vertical { background: #3b4653; border-radius: 3px; min-height: 30px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QPlainTextEdit { background: #111720; color: #aebdcc; border: 1px solid #313c49;
    border-radius: 8px; padding: 12px; font-family: Consolas; font-size: 12px; }
QDialog, QMessageBox { background: #191f27; }
QToolTip { background: #27333f; color: #f1f5f8; border: 1px solid #475769; padding: 7px; }
'''


def panel():
    frame = QFrame()
    frame.setObjectName('Panel')
    layout = QVBoxLayout(frame)
    layout.setContentsMargins(22, 19, 22, 18)
    layout.setSpacing(12)
    return frame, layout


class MainWindow(QMainWindow):
    def __init__(self, start_worker=True, settings=None):
        super().__init__()
        self.setWindowTitle('Pulse · 电脑负载灯效')
        self.setWindowIcon(app_icon())
        self.resize(1320, 920)
        self.setMinimumSize(1120, 760)
        self.settings = settings or QSettings('EPaper', 'Pulse')
        self.selected_metric = self._int_setting('metric', 0, 0, 2)
        self.values = [None, None, None]
        self.sample = None
        self.paused = False
        self.connection_requested = False
        self.requested_port = ''
        self._closing = False
        self._quit_requested = False
        self.tray = None
        self.state = 'disconnected'
        self.events = []
        self.started = time.monotonic()
        self.last_animation = time.monotonic()
        self.animation_ms = 0.0
        self.worker = MonitorWorker(self)
        self.build_ui()
        self.select_metric(self.selected_metric)
        self.worker.snapshot.connect(self.on_snapshot)
        self.worker.status.connect(self.on_status)
        self.worker.port_list.connect(self.on_ports)
        self.worker.gpu_list.connect(self.on_gpus)
        self.worker.problem.connect(self.on_problem)
        self.timer = QTimer(self)
        self.timer.timeout.connect(self.animate)
        self.timer.start(33)
        self.setup_tray()
        self.push_config()
        self.worker.command('retry', self.reconnect.isChecked())
        if start_worker:
            self.worker.start()

    def _int_setting(self, key, default, low, high):
        try:
            return max(low, min(high, int(self.settings.value(key, default))))
        except (TypeError, ValueError):
            return default

    def build_ui(self):
        root = QWidget()
        root.setObjectName('Root')
        self.setCentralWidget(root)
        shell = QHBoxLayout(root)
        shell.setContentsMargins(0, 0, 0, 0)
        shell.setSpacing(0)
        sidebar = QFrame()
        sidebar.setObjectName('Sidebar')
        sidebar.setFixedWidth(182)
        side = QVBoxLayout(sidebar)
        side.setContentsMargins(18, 30, 18, 24)
        logo_row = QHBoxLayout()
        logo = QLabel()
        logo.setPixmap(app_icon().pixmap(36, 36))
        logo_row.addWidget(logo)
        logo_row.addWidget(label('pulse', 23, TEXT, True))
        logo_row.addStretch()
        side.addLayout(logo_row)
        side.addWidget(label('电脑负载 · 光的节奏', 8, MUTED))
        side.addSpacing(36)
        side.addWidget(label('监控工作台', 8, MUTED))
        self.nav = []
        for i, name in enumerate(['C   CPU 处理器', 'G   GPU 显卡', 'M   内存占用']):
            button = QPushButton(name)
            button.setObjectName('Nav')
            button.setCheckable(True)
            button.clicked.connect(lambda checked=False, index=i: self.select_metric(index))
            side.addWidget(button)
            self.nav.append(button)
        side.addSpacing(18)
        rule = QFrame()
        rule.setObjectName('Rule')
        side.addWidget(rule)
        side.addSpacing(12)
        for text, callback in [('后台设置', self.show_background_settings), ('连接记录', self.show_log), ('使用说明', self.show_help)]:
            button = QPushButton(text)
            button.setObjectName('Nav')
            button.clicked.connect(callback)
            side.addWidget(button)
        side.addStretch()
        self.side_status = label('●  本地监控', 9, MINT)
        side.addWidget(self.side_status)
        side.addWidget(label('PULSE FOR WINDOWS\nVERSION 1.1', 7, MUTED))
        shell.addWidget(sidebar)

        area = QScrollArea()
        area.setWidgetResizable(True)
        area.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)
        content = QWidget()
        content.setObjectName('Root')
        area.setWidget(content)
        shell.addWidget(area, 1)
        main = QVBoxLayout(content)
        main.setContentsMargins(28, 24, 28, 18)
        main.setSpacing(19)
        header = QHBoxLayout()
        heading = QVBoxLayout()
        heading.setSpacing(5)
        heading.addWidget(label('SYSTEM MONITOR  /  LIGHT CONTROL', 8, MINT, True))
        heading.addWidget(label('让电脑的节奏，亮起来。', 23, TEXT, True))
        heading.addWidget(label('把实时性能，变成桌面上的一束流光。', 10, MUTED))
        header.addLayout(heading)
        header.addStretch()
        self.live_badge = label('●  实时采集  ·  500 ms', 9, MINT)
        header.addWidget(self.live_badge, 0, Qt.AlignmentFlag.AlignTop)
        main.addLayout(header)

        cards_row = QHBoxLayout()
        cards_row.setSpacing(14)
        self.cards = []
        for i, (name, subtitle) in enumerate([
            ('CPU  /  处理器', '总体使用率'), ('GPU  /  显卡', 'NVIDIA 核心使用率'),
            ('RAM  /  内存', '物理内存使用率')]):
            card = MetricCard(i, name, subtitle, COLORS[i])
            card.clicked.connect(lambda index=i: self.select_metric(index))
            cards_row.addWidget(card, 1)
            self.cards.append(card)
        main.addLayout(cards_row)

        workspace = QHBoxLayout()
        workspace.setSpacing(18)
        left = QVBoxLayout()
        left.setSpacing(18)
        preview_frame, pv = panel()
        preview_header = QHBoxLayout()
        title = QVBoxLayout()
        title.setSpacing(5)
        title.addWidget(label('流水呼吸', 14, TEXT, True))
        self.preview_subtitle = label('CPU → Q2–Q6 · 每颗灯代表 20%', 9, MUTED)
        title.addWidget(self.preview_subtitle)
        preview_header.addLayout(title)
        preview_header.addStretch()
        self.preview_badge = label('本地预览', 8, MINT)
        preview_header.addWidget(self.preview_badge)
        pv.addLayout(preview_header)
        self.preview = LedPreview()
        pv.addWidget(self.preview)
        value_row = QHBoxLayout()
        self.mapping_label = label('映射负载  —', 10, TEXT, True)
        value_row.addWidget(self.mapping_label)
        value_row.addStretch()
        value_row.addWidget(label('平滑过渡 · 连续动画', 8, MUTED))
        pv.addLayout(value_row)
        foot = label('预览按现有 7 颗单色灯布局绘制，仅调亮度与时序。', 8, MUTED)
        foot.setWordWrap(True)
        pv.addWidget(foot)
        left.addWidget(preview_frame)

        history_frame, history_layout = panel()
        history_header = QHBoxLayout()
        history_header.addWidget(label('负载趋势', 12, TEXT, True))
        history_header.addStretch()
        for name, color in zip(['CPU', 'GPU', 'RAM'], COLORS):
            history_header.addWidget(label('● ' + name, 8, color))
        history_layout.addLayout(history_header)
        self.chart = HistoryChart()
        history_layout.addWidget(self.chart, 1)
        left.addWidget(history_frame, 1)
        workspace.addLayout(left, 1)

        right_frame, right = panel()
        right.setSpacing(9)
        right_frame.setFixedWidth(306)
        right.addWidget(label('开发板连接', 13, TEXT, True))
        self.connection_badge = label('●  未连接', 10, MUTED, True)
        right.addWidget(self.connection_badge)
        row = QHBoxLayout()
        self.port_box = QComboBox()
        self.port_box.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed)
        self.port_box.setMinimumWidth(0)
        self.port_box.setToolTip('只枚举串口；点击连接后才打开设备。')
        self.port_box.addItem('正在查找串口…', '')
        row.addWidget(self.port_box, 1)
        self.refresh_button = QPushButton('刷新')
        self.refresh_button.setFixedWidth(57)
        self.refresh_button.setStyleSheet('padding: 8px 5px;')
        self.refresh_button.clicked.connect(lambda: self.worker.command('refresh'))
        row.addWidget(self.refresh_button)
        right.addLayout(row)
        self.connect_button = QPushButton('连接开发板')
        self.connect_button.setObjectName('Primary')
        self.connect_button.setEnabled(False)
        self.connect_button.clicked.connect(self.toggle_connection)
        right.addWidget(self.connect_button)
        self.reconnect = QCheckBox('连接中断后自动重试')
        self.reconnect.setChecked(self.settings.value('reconnect', 'true') in (True, 'true', 'True'))
        self.reconnect.toggled.connect(lambda value: self.worker.command('retry', value))
        right.addWidget(self.reconnect)
        self.connection_detail = label('选择 CH340 串口后连接。未连接时仍可使用实时监控与本地预览。', 8, MUTED)
        self.connection_detail.setWordWrap(True)
        self.connection_detail.setMinimumHeight(40)
        right.addWidget(self.connection_detail)
        line = QFrame()
        line.setObjectName('Rule')
        right.addWidget(line)
        right.addWidget(label('灯效设置', 12, TEXT, True))
        self.brightness = self.slider(right, '整体亮度', '80%', 0, 100,
            self._int_setting('brightness', 80, 0, 100))
        self.speed = self.slider(right, '呼吸速度', '1.0×', 50, 200,
            self._int_setting('speed', 100, 50, 200))
        right.addWidget(label('监控显卡', 8, MUTED))
        self.gpu_box = QComboBox()
        self.gpu_box.setMinimumWidth(0)
        self.gpu_box.setSizeAdjustPolicy(QComboBox.SizeAdjustPolicy.AdjustToMinimumContentsLengthWithIcon)
        self.gpu_box.setMinimumContentsLength(15)
        self.gpu_box.addItem('正在读取显卡…')
        self.gpu_box.currentIndexChanged.connect(self.change_gpu)
        right.addWidget(self.gpu_box)
        self.pause_button = QPushButton('暂停灯效输出')
        self.pause_button.clicked.connect(self.toggle_pause)
        right.addWidget(self.pause_button)
        right.addStretch()
        self.packet_label = label('TX  0     ACK  0     115200 · 8N1', 8, MUTED)
        right.addWidget(self.packet_label)
        workspace.addWidget(right_frame)
        main.addLayout(workspace, 1)
        footer = QHBoxLayout()
        self.footer = label('仅在本机采集 · 数据不上传', 8, MUTED)
        footer.addWidget(self.footer)
        footer.addStretch()
        self.uptime = label('运行 00:00', 8, MUTED)
        footer.addWidget(self.uptime)
        main.addLayout(footer)
        self.update_slider_labels()

    def slider(self, layout, text, value, low, high, initial):
        row = QHBoxLayout()
        row.addWidget(label(text, 9, MUTED))
        row.addStretch()
        value_label = label(value, 9, TEXT, True)
        row.addWidget(value_label)
        layout.addLayout(row)
        slider = QSlider(Qt.Orientation.Horizontal)
        slider.value_label = value_label
        slider.setRange(low, high)
        slider.setValue(initial)
        slider.setFixedHeight(22)
        slider.valueChanged.connect(self.update_slider_labels)
        layout.addWidget(slider)
        return slider

    def update_slider_labels(self):
        if not hasattr(self, 'speed'):
            return
        self.brightness.value_label.setText(f'{self.brightness.value()}%')
        self.speed.value_label.setText(f'{self.speed.value()/100:.2g}×')
        self.push_config()

    def push_config(self):
        if hasattr(self, 'speed'):
            self.worker.command('config', (self.selected_metric, self.brightness.value(), self.speed.value(), self.paused))

    def select_metric(self, index):
        self.selected_metric = index
        self.animation_ms = 0
        for i, card in enumerate(self.cards):
            card.select(i == index)
            self.nav[i].setChecked(i == index)
        self.preview_subtitle.setText(f'{["CPU", "GPU", "RAM"][index]} → Q2–Q6 · 每颗灯代表 20%')
        self.push_config()

    def on_snapshot(self, sample, values):
        self.sample, self.values = sample, values
        count = __import__('psutil').cpu_count() or 0
        gpu_short = sample.gpu_name.replace('NVIDIA GeForce ', '').replace(' Laptop GPU', ' Laptop')
        self.cards[0].set_value(sample.cpu, f'{count} 逻辑处理器 · 总体使用率')
        self.cards[1].set_value(sample.gpu, gpu_short)
        self.cards[1].setToolTip(sample.gpu_name + ('\n' + sample.gpu_error if sample.gpu_error else ''))
        self.cards[2].set_value(sample.memory, f'{sample.memory_used:.1f} / {sample.memory_total:.1f} GB')
        self.chart.push([sample.cpu, sample.gpu, sample.memory])
        self.footer.setText(sample.gpu_error or '仅在本机采集 · 数据不上传')

    def on_ports(self, items):
        previous = self.requested_port if self.connection_requested else (self.port_box.currentData() or self.settings.value('port', ''))
        self.port_box.blockSignals(True)
        self.port_box.clear()
        for item in sorted(items, key=lambda p: (not p['ch340'], p['port'])):
            caption = item['port'] + (' · CH340 / WCH' if item['ch340'] else ' · ' + item['description'])
            self.port_box.addItem(caption, item['port'])
            self.port_box.setItemData(self.port_box.count()-1, item['description'], Qt.ItemDataRole.ToolTipRole)
        if self.connection_requested and previous and self.port_box.findData(previous) < 0:
            self.port_box.addItem(previous + ' · 设备已拔出', previous)
        if self.port_box.count() == 0:
            self.port_box.addItem('未发现串口', '')
        index = self.port_box.findData(previous)
        if index >= 0:
            self.port_box.setCurrentIndex(index)
        self.port_box.blockSignals(False)
        self.connect_button.setEnabled(self.connection_requested or bool(self.port_box.currentData()))

    def on_gpus(self, names):
        self.gpu_box.blockSignals(True)
        self.gpu_box.clear()
        if names:
            for name in names:
                short_name = name.replace('NVIDIA GeForce ', '').replace(' Laptop GPU', ' Laptop')
                self.gpu_box.addItem(short_name)
                self.gpu_box.setItemData(self.gpu_box.count()-1, name, Qt.ItemDataRole.ToolTipRole)
        else:
            self.gpu_box.addItem('无可用 NVIDIA 显卡')
        self.gpu_box.setEnabled(bool(names))
        self.gpu_box.setCurrentIndex(min(self._int_setting('gpu', 0, 0, 64), max(0, len(names)-1)))
        self.gpu_box.blockSignals(False)
        self.change_gpu(self.gpu_box.currentIndex())

    def change_gpu(self, index):
        if index >= 0:
            self.worker.command('gpu', index)
            if hasattr(self, 'cards'):
                self.values[1] = None
                self.cards[1].spark.values.clear()
                self.chart.history[1].clear()
            self.gpu_box.setToolTip(self.gpu_box.currentText())

    def toggle_connection(self):
        if self.connection_requested:
            self.worker.command('disconnect')
            self.connection_requested = False
        else:
            port = self.port_box.currentData()
            if not port:
                return
            self.settings.setValue('port', port)
            self.requested_port = port
            self.worker.command('connect', port)
            self.connection_requested = True
        self.port_box.setEnabled(not self.connection_requested)
        self.connect_button.setText('断开连接' if self.connection_requested else '连接开发板')

    def on_status(self, status):
        self.state = status['state']
        names = {'disconnected': '未连接', 'handshake': '等待固件应答', 'ready': '开发板已连接',
                 'unsupported': '固件未应答', 'retry': '等待自动重连', 'error': '连接失败', 'rejected': '固件拒绝命令'}
        color = MINT if self.state == 'ready' else ('#edbf7a' if self.state != 'disconnected' else MUTED)
        self.connection_badge.setText('●  ' + names.get(self.state, self.state))
        self.connection_badge.setStyleSheet(f'color: {color}; background: transparent; border: none;')
        self.connection_detail.setText(status['detail'])
        self.side_status.setText('●  开发板已连接' if self.state == 'ready' else '●  本地监控')
        if self.state == 'error' and self.connection_requested and not self.reconnect.isChecked():
            self.connection_requested = False
            self.connect_button.setText('重新连接')
            self.port_box.setEnabled(True)
        self.packet_label.setText(f'TX  {status["sent"]}     ACK  {status["received"]}     115200 · 8N1')
        for event in status['events']:
            self.events.append(time.strftime('%H:%M:%S') + '  ' + event)
        self.events = self.events[-200:]

    def on_problem(self, message):
        self.state = 'error'
        self.connection_requested = False
        self.connection_detail.setText(message)
        self.connection_badge.setText('●  后台任务已停止')
        self.connect_button.setEnabled(False)
        self.pause_button.setEnabled(False)
        self.side_status.setText('●  采集已停止')
        self.live_badge.setText('●  采集已停止')
        self.values = [None, None, None]
        self.events.append(message)

    def toggle_pause(self):
        self.paused = not self.paused
        self.pause_button.setText('恢复灯效输出' if self.paused else '暂停灯效输出')
        if self.tray:
            self.tray_pause.setText(self.pause_button.text())
        self.push_config()

    def animate(self):
        now = time.monotonic()
        if not self.paused:
            self.animation_ms += (now - self.last_animation) * 1000 * self.speed.value() / 100
        self.last_animation = now
        value = self.values[self.selected_metric]
        self.preview.set_levels(duties(self.animation_ms, None if self.paused else value, self.brightness.value()))
        self.mapping_label.setText('映射负载  ' + ('—' if value is None else f'{value:.1f}%'))
        badge = '已暂停' if self.paused else ('无有效数据' if value is None else ('已同步' if self.state == 'ready' else '本地预览'))
        self.preview_badge.setText(badge)
        seconds = int(now - self.started)
        self.uptime.setText(f'运行 {seconds//60:02d}:{seconds%60:02d}')

    def show_log(self):
        dialog = QDialog(self)
        dialog.setWindowTitle('Pulse · 连接记录')
        dialog.resize(690, 430)
        layout = QVBoxLayout(dialog)
        layout.addWidget(label('连接记录', 16, TEXT, True))
        log = QPlainTextEdit()
        log.setReadOnly(True)
        log.setPlainText('\n'.join(self.events) or '暂无连接记录。选择串口并点击“连接开发板”开始。')
        layout.addWidget(log)
        copy = QPushButton('复制记录')
        copy.clicked.connect(lambda: QApplication.clipboard().setText(log.toPlainText()))
        layout.addWidget(copy)
        dialog.exec()

    def show_help(self):
        QMessageBox.information(self, '关于 Pulse',
            'Pulse 1.1 · 电脑负载灯效\n\n'
            '1. 点击 CPU、GPU 或内存卡片，选择映射的指标。\n'
            '2. 调节亮度和速度，本地预览会立即更新。\n'
            '3. 选择 CH340 串口，连接支持 Pulse v1 的固件。\n\n'
            '本版不修改、不烧录 STM32 固件。现有旧固件没有 Pulse 协议时，'
            '会显示“固件未应答”，这是预期状态。\n\n'
            'GPU 首版支持 NVIDIA / NVML；Intel、AMD 暂不支持。'
            '使用率口径可能与任务管理器不同。\n\n'
            '暂停会停止灯效，监控仍继续。默认关闭窗口后驻留托盘；从托盘退出会释放串口。'
            'USB 拔出后每 3 秒尝试重连原串口；端口号改变时请重新选择。\n\n'
            '协议：115200 / 8N1 · Pulse v1 · CRC16\n'
            '本地采集，无账号，无云服务。')

    def setup_tray(self):
        if not QSystemTrayIcon.isSystemTrayAvailable():
            return
        QApplication.instance().setQuitOnLastWindowClosed(False)
        self.tray = QSystemTrayIcon(app_icon(), self)
        self.tray.setToolTip('Pulse · 电脑负载灯效')
        menu = QMenu(self)
        menu.addAction('显示主窗口', self.restore_window)
        self.tray_pause = menu.addAction('暂停灯效输出', self.toggle_pause)
        menu.addAction('后台设置', self.show_background_settings)
        menu.addSeparator()
        menu.addAction('退出 Pulse', self.request_exit)
        self.tray.setContextMenu(menu)
        self.tray.activated.connect(self.on_tray_activated)
        self.tray.show()

    def on_tray_activated(self, reason):
        if reason in (QSystemTrayIcon.ActivationReason.Trigger,
                      QSystemTrayIcon.ActivationReason.DoubleClick):
            self.restore_window()

    def start_visibility(self, hidden=False):
        if self.tray and hidden:
            self.hide()
            self.timer.setInterval(250)
        else:
            self.restore_window()

    def restore_window(self):
        if self._closing:
            return
        self.showNormal()
        self.raise_()
        self.activateWindow()
        self.timer.setInterval(33)

    def request_exit(self):
        self._quit_requested = True
        self.close()

    def show_background_settings(self):
        from .background import startup_enabled, set_startup
        dialog = QDialog(self)
        dialog.setWindowTitle('Pulse · 后台设置')
        dialog.setMinimumWidth(440)
        layout = QVBoxLayout(dialog)
        layout.setContentsMargins(28, 24, 28, 24)
        layout.setSpacing(18)
        layout.addWidget(label('让灯效在后台继续', 17, TEXT, True))
        hint = label('隐藏窗口后仍持续采集和发送。通过托盘图标恢复或退出。', 9, MUTED)
        hint.setWordWrap(True)
        layout.addWidget(hint)
        for key, title, default in [('close_to_tray', '关闭窗口后驻留系统托盘', True),
                                    ('start_hidden', '手动启动时也隐藏主窗口', False)]:
            checkbox = QCheckBox(title)
            checkbox.setChecked(self.settings.value(key, default, type=bool))
            checkbox.setEnabled(self.tray is not None)
            checkbox.toggled.connect(lambda value, k=key: self.settings.setValue(k, value))
            layout.addWidget(checkbox)
        startup = QCheckBox('登录 Windows 后静默启动')
        try:
            startup.setChecked(startup_enabled())
        except OSError:
            startup.setEnabled(False)
        def change_startup(value):
            try:
                set_startup(value)
            except OSError as error:
                startup.blockSignals(True)
                startup.setChecked(not value)
                startup.blockSignals(False)
                QMessageBox.warning(dialog, '无法保存启动设置', str(error))
        startup.toggled.connect(change_startup)
        layout.addWidget(startup)
        note = label('自启动使用当前程序路径；移动程序后请重新开启。\n启动后仍需手动连接串口。没有系统托盘时会显示窗口。', 9, MUTED)
        note.setWordWrap(True)
        layout.addWidget(note)
        done = QPushButton('完成')
        done.setObjectName('Primary')
        done.clicked.connect(dialog.accept)
        layout.addWidget(done)
        dialog.exec()
        self.settings.sync()

    def closeEvent(self, event):
        for key, value in [('metric', self.selected_metric), ('brightness', self.brightness.value()),
                           ('speed', self.speed.value()), ('gpu', self.gpu_box.currentIndex()),
                           ('reconnect', self.reconnect.isChecked())]:
            self.settings.setValue(key, value)
        self.settings.sync()
        if (self.tray and not self._quit_requested
                and self.settings.value('close_to_tray', True, type=bool)):
            self.hide()
            self.timer.setInterval(250)
            event.ignore()
            return
        self.timer.stop()
        if not self._closing:
            self._closing = True
            self.worker.finished.connect(self.close, Qt.ConnectionType.QueuedConnection)
        self.worker.stop()
        if self.worker.isRunning():
            self.connection_detail.setText('正在释放设备，请稍候…')
            self.centralWidget().setEnabled(False)
            event.ignore()
            return
        if self.tray:
            self.tray.hide()
        event.accept()
        QApplication.instance().quit()

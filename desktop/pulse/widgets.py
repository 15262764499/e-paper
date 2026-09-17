"""Small native-painted widgets. No browser, web service, or chart dependency."""
from collections import deque
import math

from PySide6.QtCore import Qt, QRectF, QPointF, Signal
from PySide6.QtGui import QColor, QPainter, QPen, QBrush, QFont, QPainterPath, QLinearGradient, QRadialGradient
from PySide6.QtWidgets import QWidget, QFrame, QLabel, QVBoxLayout, QHBoxLayout

MINT = '#8ee4c6'
VIOLET = '#b7a3ff'
AMBER = '#edbf7a'
TEXT = '#edf1f3'
MUTED = '#7e8998'
COLORS = [MINT, VIOLET, AMBER]


def font(size=10, weight=QFont.Weight.Normal, family='Microsoft YaHei UI'):
    f = QFont(family)
    f.setPointSize(size)
    f.setWeight(weight)
    return f


def label(text, size=10, color=TEXT, bold=False):
    w = QLabel(text)
    w.setFont(font(size, QFont.Weight.DemiBold if bold else QFont.Weight.Normal))
    w.setStyleSheet(f'color: {color}; background: transparent; border: none;')
    return w


class Sparkline(QWidget):
    def __init__(self, color=MINT, parent=None):
        super().__init__(parent)
        self.color = color
        self.values = deque(maxlen=60)
        self.setFixedHeight(40)

    def push(self, value):
        self.values.append(value)
        self.update()

    def paintEvent(self, event):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        p.setPen(QPen(QColor('#29303a'), 1))
        p.drawLine(0, self.height() - 2, self.width(), self.height() - 2)
        if len(self.values) < 2:
            return
        path = QPainterPath()
        active = False
        for i, value in enumerate(self.values):
            if value is None:
                active = False
                continue
            x = self.width() * (60 - len(self.values) + i) / 59
            y = (self.height() - 6) * (1 - value / 100) + 2
            if not active:
                path.moveTo(x, y)
                active = True
            else:
                path.lineTo(x, y)
        p.setPen(QPen(QColor(self.color), 1.7))
        p.drawPath(path)


class MetricCard(QFrame):
    clicked = Signal()

    def __init__(self, index, title, subtitle, color, parent=None):
        super().__init__(parent)
        self.index, self.color = index, color
        self.setCursor(Qt.CursorShape.PointingHandCursor)
        self.setMinimumWidth(180)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(19, 17, 19, 12)
        layout.setSpacing(6)
        row = QHBoxLayout()
        row.addWidget(label(title, 10, color, True))
        row.addStretch()
        self.tag = label('', 8, color)
        row.addWidget(self.tag)
        layout.addLayout(row)
        value_row = QHBoxLayout()
        value_row.setSpacing(4)
        self.value = label('—', 32, TEXT, True)
        value_row.addWidget(self.value)
        value_row.addWidget(label('%', 13, MUTED), 0, Qt.AlignmentFlag.AlignBottom)
        value_row.addStretch()
        layout.addLayout(value_row)
        self.subtitle = label(subtitle, 8, MUTED)
        layout.addWidget(self.subtitle)
        self.spark = Sparkline(color)
        layout.addWidget(self.spark)
        self.select(False)

    def select(self, selected):
        self.tag.setText('● 正在映射' if selected else '点击映射')
        border = self.color if selected else '#2b323c'
        self.setStyleSheet(f'MetricCard {{background: #191f27; border: 1px solid {border}; border-radius: 14px;}}')

    def set_value(self, value, subtitle):
        self.value.setText('—' if value is None else f'{value:.0f}')
        self.subtitle.setText(subtitle)
        self.spark.push(value)

    def mouseReleaseEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton:
            self.clicked.emit()
        super().mouseReleaseEvent(event)


class LedPreview(QWidget):
    LAMP_COLORS = ['#fc7491', '#fb9072', '#f5b465', '#e4d778', '#8adeab', '#69cbdc', '#ad9beb']

    def __init__(self, parent=None):
        super().__init__(parent)
        self.levels = [0] * 7
        self.setMinimumHeight(156)

    def set_levels(self, values):
        self.levels = values
        self.update()

    def paintEvent(self, event):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        w, h = self.width(), self.height()
        margin = max(38, w * 0.065)
        step = (w - margin * 2) / 6
        cy = h * 0.48
        p.setPen(QPen(QColor('#323b47'), 1))
        p.drawLine(QPointF(margin, cy), QPointF(w - margin, cy))
        for i, level in enumerate(self.levels):
            x = margin + step * i
            c = QColor(self.LAMP_COLORS[i])
            if level:
                glow = QRadialGradient(QPointF(x, cy), 48)
                glow.setColorAt(0, QColor(c.red(), c.green(), c.blue(), int(level * 1.6)))
                glow.setColorAt(0.5, QColor(c.red(), c.green(), c.blue(), int(level * 0.35)))
                glow.setColorAt(1, QColor(c.red(), c.green(), c.blue(), 0))
                p.setPen(Qt.PenStyle.NoPen)
                p.setBrush(QBrush(glow))
                p.drawEllipse(QPointF(x, cy), 48, 48)
            p.setPen(QPen(QColor('#343e4b'), 1))
            p.setBrush(QColor('#11161d'))
            p.drawRoundedRect(QRectF(x - 21, cy - 27, 42, 54), 12, 12)
            base = QColor(c)
            base.setAlpha(30 + round(level * 2.1))
            p.setPen(Qt.PenStyle.NoPen)
            p.setBrush(base)
            p.drawRoundedRect(QRectF(x - 13, cy - 18, 26, 36), 7, 7)
            if level > 5:
                p.setBrush(QColor(255, 255, 255, int(level * 1.5)))
                p.drawRoundedRect(QRectF(x - 7, cy - 13, 14, 6), 3, 3)
            p.setFont(font(8))
            p.setPen(QColor(MUTED))
            p.drawText(QRectF(x - 25, cy + 37, 50, 20), Qt.AlignmentFlag.AlignCenter, f'Q{i+1}')
            p.setFont(font(7))
            p.setPen(QColor('#546070'))
            text = '保留' if i in (0, 6) else f'{(i-1)*20}–{i*20}%'
            p.drawText(QRectF(x - 37, cy + 59, 74, 17), Qt.AlignmentFlag.AlignCenter, text)


class HistoryChart(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.history = [deque(maxlen=120) for _ in range(3)]
        self.setMinimumHeight(132)

    def push(self, values):
        for history, value in zip(self.history, values):
            history.append(value)
        self.update()

    def paintEvent(self, event):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        rect = QRectF(32, 8, self.width() - 45, self.height() - 32)
        p.setFont(font(7))
        for value in (0, 50, 100):
            y = rect.bottom() - value / 100 * rect.height()
            p.setPen(QPen(QColor('#29313b'), 1, Qt.PenStyle.DotLine))
            p.drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y))
            p.setPen(QColor(MUTED))
            p.drawText(QRectF(0, y-8, 25, 16), Qt.AlignmentFlag.AlignRight, str(value))
        for values, color in zip(self.history, COLORS):
            path = QPainterPath()
            active = False
            for i, v in enumerate(values):
                if v is None:
                    active = False
                    continue
                x = rect.left() + (120 - len(values) + i) / 119 * rect.width()
                y = rect.bottom() - v / 100 * rect.height()
                if active:
                    path.lineTo(x, y)
                else:
                    path.moveTo(x, y)
                    active = True
            p.setPen(QPen(QColor(color), 1.8))
            p.drawPath(path)
        p.setPen(QColor(MUTED))
        p.drawText(QRectF(rect.left(), rect.bottom()+9, 90, 16), '60 秒前')
        p.drawText(QRectF(rect.right()-70, rect.bottom()+9, 70, 16), Qt.AlignmentFlag.AlignRight, '现在')


def app_icon():
    from PySide6.QtGui import QIcon, QPixmap
    pix = QPixmap(128, 128)
    pix.fill(Qt.GlobalColor.transparent)
    p = QPainter(pix)
    p.setRenderHint(QPainter.RenderHint.Antialiasing)
    p.setBrush(QColor('#182c29'))
    p.setPen(Qt.PenStyle.NoPen)
    p.drawRoundedRect(QRectF(4, 4, 120, 120), 28, 28)
    for x, y, height in [(29, 48, 32), (49, 28, 72), (69, 39, 50), (89, 54, 24)]:
        p.setBrush(QColor(MINT))
        p.drawRoundedRect(QRectF(x, y, 10, height), 5, 5)
    p.end()
    return QIcon(pix)

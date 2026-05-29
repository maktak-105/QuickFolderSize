from __future__ import annotations
from PyQt6.QtCore import Qt, QModelIndex, QRect
from PyQt6.QtGui import QPainter, QColor, QPen
from PyQt6.QtWidgets import QStyledItemDelegate, QStyleOptionViewItem, QApplication

from model import COL_BAR, ROLE_RATIO

_BAR_COLOR = QColor(70, 130, 200, 180)
_BG_COLOR  = QColor(220, 230, 240, 100)
_MARGIN    = 4


class SizeBarDelegate(QStyledItemDelegate):
    """Draws a horizontal proportional bar in the COL_BAR column."""

    def paint(self, painter: QPainter, option: QStyleOptionViewItem, index: QModelIndex):
        if index.column() != COL_BAR:
            super().paint(painter, option, index)
            return

        # Draw selection background
        QApplication.style().drawControl(
            QApplication.style().ControlElement.CE_ItemViewItem, option, painter
        )

        ratio: float = index.data(ROLE_RATIO) or 0.0
        ratio = max(0.0, min(1.0, ratio))

        rect: QRect = option.rect.adjusted(_MARGIN, _MARGIN, -_MARGIN, -_MARGIN)

        # Background track
        painter.save()
        painter.setPen(Qt.PenStyle.NoPen)
        painter.setBrush(_BG_COLOR)
        painter.drawRoundedRect(rect, 2, 2)

        # Filled bar
        if ratio > 0:
            bar_rect = QRect(rect.x(), rect.y(), int(rect.width() * ratio), rect.height())
            painter.setBrush(_BAR_COLOR)
            painter.drawRoundedRect(bar_rect, 2, 2)

        # Percentage label
        pct = f"{ratio * 100:.1f}%"
        painter.setPen(QPen(QColor(30, 30, 30)))
        painter.drawText(rect, Qt.AlignmentFlag.AlignCenter, pct)
        painter.restore()

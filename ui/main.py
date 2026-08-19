#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# SPDX-License-Identifier: Apache-2.0

"""
FPS Touch Keymap Editor (PyQt6 / PyQt5 / PySide6 / PySide2)
Professional Desktop Edition with Visual Type Cards and Strict Image Boundary Clipping.
"""

import sys
import os
import json
import math

# Universal PyQt / PySide import wrapper
QT_BINDING = None
try:
    from PyQt6.QtWidgets import (
        QApplication, QMainWindow, QWidget, QHBoxLayout, QVBoxLayout,
        QGridLayout, QLabel, QPushButton, QLineEdit, QSpinBox, QComboBox,
        QCheckBox, QListWidget, QListWidgetItem, QFileDialog, QMessageBox,
        QScrollArea, QGroupBox, QFrame, QSplitter, QSizePolicy, QButtonGroup,
        QRadioButton, QStatusBar, QToolButton
    )
    from PyQt6.QtGui import (
        QPainter, QPen, QBrush, QColor, QFont, QPixmap, QImage,
        QPainterPath, QIcon, QKeySequence, QCursor, QLinearGradient
    )
    from PyQt6.QtCore import Qt, QPoint, QRect, QRectF, QPointF, pyqtSignal as Signal, QSize
    QT_BINDING = "PyQt6"
except ImportError:
    try:
        from PyQt5.QtWidgets import (
            QApplication, QMainWindow, QWidget, QHBoxLayout, QVBoxLayout,
            QGridLayout, QLabel, QPushButton, QLineEdit, QSpinBox, QComboBox,
            QCheckBox, QListWidget, QListWidgetItem, QFileDialog, QMessageBox,
            QScrollArea, QGroupBox, QFrame, QSplitter, QSizePolicy, QButtonGroup,
            QRadioButton, QStatusBar, QToolButton
        )
        from PyQt5.QtGui import (
            QPainter, QPen, QBrush, QColor, QFont, QPixmap, QImage,
            QPainterPath, QIcon, QKeySequence, QCursor, QLinearGradient
        )
        from PyQt5.QtCore import Qt, QPoint, QRect, QRectF, QPointF, pyqtSignal as Signal, QSize
        QT_BINDING = "PyQt5"
    except ImportError:
        try:
            from PySide6.QtWidgets import (
                QApplication, QMainWindow, QWidget, QHBoxLayout, QVBoxLayout,
                QGridLayout, QLabel, QPushButton, QLineEdit, QSpinBox, QComboBox,
                QCheckBox, QListWidget, QListWidgetItem, QFileDialog, QMessageBox,
                QScrollArea, QGroupBox, QFrame, QSplitter, QSizePolicy, QButtonGroup,
                QRadioButton, QStatusBar, QToolButton
            )
            from PySide6.QtGui import (
                QPainter, QPen, QBrush, QColor, QFont, QPixmap, QImage,
                QPainterPath, QIcon, QKeySequence, QCursor, QLinearGradient
            )
            from PySide6.QtCore import Qt, QPoint, QRect, QRectF, QPointF, Signal, QSize
            QT_BINDING = "PySide6"
        except ImportError:
            try:
                from PySide2.QtWidgets import (
                    QApplication, QMainWindow, QWidget, QHBoxLayout, QVBoxLayout,
                    QGridLayout, QLabel, QPushButton, QLineEdit, QSpinBox, QComboBox,
                    QCheckBox, QListWidget, QListWidgetItem, QFileDialog, QMessageBox,
                    QScrollArea, QGroupBox, QFrame, QSplitter, QSizePolicy, QButtonGroup,
                    QRadioButton, QStatusBar, QToolButton
                )
                from PySide2.QtGui import (
                    QPainter, QPen, QBrush, QColor, QFont, QPixmap, QImage,
                    QPainterPath, QIcon, QKeySequence, QCursor, QLinearGradient
                )
                from PySide2.QtCore import Qt, QPoint, QRect, QRectF, QPointF, Signal, QSize
                QT_BINDING = "PySide2"
            except ImportError:
                print("Error: No Qt binding found. Please install PyQt6 in the virtual environment.", file=sys.stderr)
                sys.exit(1)


def get_qt_enum(cls, name):
    if hasattr(cls, name):
        return getattr(cls, name)
    parent_cls = getattr(Qt, cls.__name__, None)
    if parent_cls and hasattr(parent_cls, name):
        return getattr(parent_cls, name)
    return getattr(Qt, name)


COLOR_MAP = {
    "movement_joystick": QColor(56, 189, 248),  # Sky Blue
    "look_touch": QColor(167, 139, 250),        # Violet
    "look_start_left": QColor(244, 114, 182),
    "look_start_right": QColor(244, 114, 182),
    "look_start_up": QColor(244, 114, 182),
    "look_start_down": QColor(244, 114, 182),
    "key": QColor(251, 146, 60),                # Orange
}

LABEL_MAP = {
    "movement_joystick": "移动摇杆 (WASD)",
    "look_touch": "视角触摸区 T",
    "look_start_left": "S-Left",
    "look_start_right": "S-Right",
    "look_start_up": "S-Up",
    "look_start_down": "S-Down",
    "key": "按键触点",
}

ICON_MAP = {
    "movement_joystick": "🎮",
    "look_touch": "👁️",
    "look_start_left": "⬅️",
    "look_start_right": "➡️",
    "look_start_up": "⬆️",
    "look_start_down": "⬇️",
    "key": "🎯",
}

QUICK_KEYS = [
    ("BTN_LEFT", "开火/射击"),
    ("BTN_RIGHT", "开镜/瞄准"),
    ("KEY_SPACE", "跳跃"),
    ("KEY_LEFTSHIFT", "冲刺"),
    ("KEY_TAB", "背包/地图"),
    ("KEY_M", "地图"),
    ("KEY_C", "蹲下"),
    ("KEY_Z", "趴下"),
    ("KEY_R", "换弹"),
    ("KEY_F", "交互"),
    ("KEY_1", "主武1"),
    ("KEY_2", "主武2"),
    ("KEY_3", "投掷物"),
    ("KEY_4", "药品"),
]


class TouchZone:
    def __init__(self, kind="key", shape="circle", x=0, y=0, radius=50, width=100, height=100,
                 key="KEY_SPACE", look_gain=8,
                 random_percent=60, start_radius=30, enter_min_delay_us=800,
                 enter_max_delay_us=2200, turn_min_delay_us=400, turn_max_delay_us=1400,
                 release_movement=False, release_look=False):
        self.kind = kind
        self.shape = shape
        self.x = int(x)
        self.y = int(y)
        self.radius = int(radius)
        self.width = int(width)
        self.height = int(height)
        self.key = str(key)
        self.look_gain = max(1, min(int(look_gain), 512))
        self.random_percent = int(random_percent)
        self.start_radius = max(0, min(int(start_radius), self.radius - 1)) if self.shape == "circle" else 0
        self.enter_min_delay_us = int(enter_min_delay_us)
        self.enter_max_delay_us = int(enter_max_delay_us)
        self.turn_min_delay_us = int(turn_min_delay_us)
        self.turn_max_delay_us = int(turn_max_delay_us)
        self.release_movement = bool(release_movement)
        self.release_look = bool(release_look)

    def hit_test(self, px, py):
        if self.shape == "circle":
            dx = px - self.x
            dy = py - self.y
            return (dx * dx + dy * dy) <= (self.radius * self.radius)
        else:
            return (self.x <= px <= self.x + self.width and
                    self.y <= py <= self.y + self.height)

    def clamp_to_screen(self, max_w, max_h):
        if self.shape == "circle":
            self.radius = max(5, min(self.radius, int(min(max_w, max_h) / 2)))
            self.start_radius = max(0, min(self.start_radius, self.radius - 1))
            self.x = max(self.radius, min(self.x, max_w - self.radius))
            self.y = max(self.radius, min(self.y, max_h - self.radius))
        else:
            self.width = max(5, min(self.width, max_w))
            self.height = max(5, min(self.height, max_h))
            self.x = max(0, min(self.x, max_w - self.width))
            self.y = max(0, min(self.y, max_h - self.height))


class KeyCaptureLineEdit(QLineEdit):
    """An editable evdev key-code field that also captures physical key presses."""

    key_captured = Signal(str)

    SPECIAL_KEYS = {
        "Key_Space": "KEY_SPACE",
        "Key_Return": "KEY_ENTER",
        "Key_Enter": "KEY_ENTER",
        "Key_Escape": "KEY_ESC",
        "Key_Tab": "KEY_TAB",
        "Key_Backspace": "KEY_BACKSPACE",
        "Key_Left": "KEY_LEFT",
        "Key_Right": "KEY_RIGHT",
        "Key_Up": "KEY_UP",
        "Key_Down": "KEY_DOWN",
        "Key_Shift": "KEY_LEFTSHIFT",
        "Key_Control": "KEY_LEFTCTRL",
        "Key_Alt": "KEY_LEFTALT",
    }

    def _key_name(self, event):
        key_value = event.key()
        for qt_name, evdev_name in self.SPECIAL_KEYS.items():
            if key_value == get_qt_enum(Qt.Key if hasattr(Qt, "Key") else Qt, qt_name):
                return evdev_name

        key_text = event.text().upper()
        if len(key_text) == 1 and (key_text.isalpha() or key_text.isdigit()):
            return f"KEY_{key_text}"

        for number in range(1, 13):
            qt_name = f"Key_F{number}"
            if key_value == get_qt_enum(Qt.Key if hasattr(Qt, "Key") else Qt, qt_name):
                return f"KEY_F{number}"
        return None

    def keyPressEvent(self, event):
        control_modifier = get_qt_enum(
            Qt.KeyboardModifier if hasattr(Qt, "KeyboardModifier") else Qt,
            "ControlModifier",
        )
        if event.modifiers() & control_modifier:
            super().keyPressEvent(event)
            return
        key_name = self._key_name(event)
        if key_name:
            self.setText(key_name)
            self.selectAll()
            self.key_captured.emit(key_name)
            event.accept()
            return
        super().keyPressEvent(event)


class CanvasWidget(QWidget):
    zone_selected = Signal(int)
    zones_changed = Signal()
    cursor_moved = Signal(int, int, bool)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setFocusPolicy(get_qt_enum(Qt.FocusPolicy if hasattr(Qt, 'FocusPolicy') else Qt, 'StrongFocus'))
        self.setMouseTracking(True)
        self.setAcceptDrops(True)
        self.image = None
        self.image_path = None
        self.screen_width = 1080
        self.screen_height = 2400
        self.zones = []
        self.selected_index = -1

        # Interactive state
        self.drag_start = None
        self.preview_zone = None
        self.drag_mode = None  # None, "move", "resize_circle", "resize_nw", etc.
        self.drag_data = None

        # Defaults for creation
        self.new_kind = "movement_joystick"
        self.new_shape = "circle"
        self.new_key = "BTN_LEFT"
        self.new_look_gain = 8
        self.new_start_radius = 30
        self.new_enter_min_delay_us = 800
        self.new_enter_max_delay_us = 2200
        self.new_turn_min_delay_us = 400
        self.new_turn_max_delay_us = 1400
        self.new_random_percent = 60
        self.new_rel_movement = False
        self.new_rel_look = False

        self.setStyleSheet("background-color: #0f1318;")

    def set_screen_size(self, width, height):
        self.screen_width = max(1, width)
        self.screen_height = max(1, height)
        for z in self.zones:
            z.clamp_to_screen(self.screen_width, self.screen_height)
        self.update()

    def load_image(self, path):
        if not path or not os.path.exists(path):
            return False
        pixmap = QPixmap(path)
        if pixmap.isNull():
            return False
        self.image = pixmap
        self.image_path = path
        self.update()
        return True

    def dragEnterEvent(self, event):
        if event.mimeData().hasUrls():
            event.acceptProposedAction()

    def dropEvent(self, event):
        urls = event.mimeData().urls()
        if urls:
            file_path = urls[0].toLocalFile()
            if self.load_image(file_path):
                win = self.window()
                if hasattr(win, 'lbl_image_info'):
                    win.lbl_image_info.setText(f"已加载: {os.path.basename(file_path)} ({self.image.width()}x{self.image.height()})")
                if hasattr(win, 'spin_width') and hasattr(win, 'spin_height'):
                    win.spin_width.setValue(self.image.width())
                    win.spin_height.setValue(self.image.height())

    def get_transform(self):
        w = self.width()
        h = self.height()
        if w <= 0 or h <= 0 or self.screen_width <= 0 or self.screen_height <= 0:
            return 0, 0, 1.0

        scale_x = w / self.screen_width
        scale_y = h / self.screen_height
        scale = min(scale_x, scale_y) * 0.95

        draw_w = self.screen_width * scale
        draw_h = self.screen_height * scale
        offset_x = (w - draw_w) / 2
        offset_y = (h - draw_h) / 2
        return offset_x, offset_y, scale

    def screen_to_canvas(self, sx, sy):
        ox, oy, s = self.get_transform()
        return ox + sx * s, oy + sy * s

    def canvas_to_screen(self, cx, cy):
        ox, oy, s = self.get_transform()
        if s <= 0:
            return 0, 0
        sx = (cx - ox) / s
        sy = (cy - oy) / s
        return sx, sy

    def is_inside_image(self, sx, sy):
        return 0 <= sx <= self.screen_width and 0 <= sy <= self.screen_height

    def clamp_point(self, sx, sy):
        return max(0, min(sx, self.screen_width)), max(0, min(sy, self.screen_height))

    def get_zone_handle(self, zone, sx, sy):
        ox, oy, s = self.get_transform()
        tol = max(10.0 / s, 8.0)

        if zone.shape == "circle":
            dist = math.hypot(sx - zone.x, sy - zone.y)
            if math.hypot(sx - (zone.x + zone.radius), sy - zone.y) <= tol * 1.5:
                return "resize_circle"
            if abs(dist - zone.radius) <= tol:
                return "resize_circle"
        else:
            x, y, w, h = zone.x, zone.y, zone.width, zone.height
            if math.hypot(sx - x, sy - y) <= tol: return "resize_nw"
            if math.hypot(sx - (x + w), sy - y) <= tol: return "resize_ne"
            if math.hypot(sx - x, sy - (y + h)) <= tol: return "resize_sw"
            if math.hypot(sx - (x + w), sy - (y + h)) <= tol: return "resize_se"
            if x <= sx <= x + w:
                if abs(sy - y) <= tol: return "resize_n"
                if abs(sy - (y + h)) <= tol: return "resize_s"
            if y <= sy <= y + h:
                if abs(sx - x) <= tol: return "resize_w"
                if abs(sx - (x + w)) <= tol: return "resize_e"

        return None

    def keyPressEvent(self, event):
        key = event.key()
        del_keys = (
            get_qt_enum(Qt.Key, 'Key_Delete') if hasattr(Qt.Key, 'Key_Delete') else Qt.Key_Delete,
            get_qt_enum(Qt.Key, 'Key_Backspace') if hasattr(Qt.Key, 'Key_Backspace') else Qt.Key_Backspace
        )
        if key in del_keys:
            if 0 <= self.selected_index < len(self.zones):
                del self.zones[self.selected_index]
                if self.selected_index >= len(self.zones):
                    self.selected_index = len(self.zones) - 1
                self.zone_selected.emit(self.selected_index)
                self.zones_changed.emit()
                self.update()
                event.accept()
                return
        super().keyPressEvent(event)

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(get_qt_enum(QPainter.RenderHint if hasattr(QPainter, 'RenderHint') else QPainter, 'Antialiasing'))

        ox, oy, s = self.get_transform()
        draw_w = self.screen_width * s
        draw_h = self.screen_height * s
        canvas_rect = QRectF(ox, oy, draw_w, draw_h)

        # Draw outer viewport background
        painter.fillRect(self.rect(), QColor("#0b0e14"))

        # Clip all drawing strictly inside the image rectangle
        painter.save()
        painter.setClipRect(canvas_rect)

        # Draw image or checkered canvas
        if self.image and not self.image.isNull():
            painter.drawPixmap(canvas_rect.toRect(), self.image)
        else:
            painter.fillRect(canvas_rect, QColor("#1c212a"))
            grid_pen = QPen(QColor("#28303e"), 1)
            painter.setPen(grid_pen)
            step = max(24, int(48 * s))
            x = ox
            while x < ox + draw_w:
                painter.drawLine(int(x), int(oy), int(x), int(oy + draw_h))
                x += step
            y = oy
            while y < oy + draw_h:
                painter.drawLine(int(ox), int(y), int(ox + draw_w), int(y))
                y += step

            # Watermark guide
            font = QFont("sans-serif", 13, QFont.Weight.Bold if hasattr(QFont, 'Weight') else QFont.Bold)
            painter.setFont(font)
            painter.setPen(QColor("#475569"))
            painter.drawText(canvas_rect, get_qt_enum(Qt.AlignmentFlag if hasattr(Qt, 'AlignmentFlag') else Qt, 'AlignCenter'), "在此区域内拖拽创建触控区域\n(支持拖入游戏截图)")

        # Draw all zones
        for idx, zone in enumerate(self.zones):
            is_selected = (idx == self.selected_index)
            self.draw_zone(painter, zone, is_selected, False)

        # Draw interactive preview zone
        if self.preview_zone:
            self.draw_zone(painter, self.preview_zone, True, True)

        painter.restore()

        # Canvas boundary glow border
        border_pen = QPen(QColor("#38bdf8"), 2)
        painter.setPen(border_pen)
        painter.setBrush(get_qt_enum(Qt.BrushStyle if hasattr(Qt, 'BrushStyle') else Qt, 'NoBrush'))
        painter.drawRect(canvas_rect)

        painter.end()

    def draw_zone(self, painter, zone, is_selected, is_preview):
        color = COLOR_MAP.get(zone.kind, QColor(250, 204, 21))
        pen_width = 3 if is_selected else 2
        pen = QPen(color, pen_width)
        if is_preview:
            pen.setStyle(get_qt_enum(Qt.PenStyle if hasattr(Qt, 'PenStyle') else Qt, 'DashLine'))

        fill_color = QColor(color)
        fill_color.setAlpha(70 if is_selected else 40)

        painter.setPen(pen)
        painter.setBrush(QBrush(fill_color))

        ox, oy, s = self.get_transform()
        if zone.shape == "circle":
            cx, cy = self.screen_to_canvas(zone.x, zone.y)
            r = zone.radius * s
            painter.drawEllipse(QPointF(cx, cy), r, r)

            if zone.kind == "movement_joystick":
                start_r = zone.start_radius * s
                start_pen = QPen(QColor(255, 255, 255, 185), 1,
                                 get_qt_enum(Qt.PenStyle if hasattr(Qt, 'PenStyle') else Qt, 'DashLine'))
                painter.setPen(start_pen)
                painter.setBrush(get_qt_enum(Qt.BrushStyle if hasattr(Qt, 'BrushStyle') else Qt, 'NoBrush'))
                painter.drawEllipse(QPointF(cx, cy), start_r, start_r)
            # Center dot
            painter.setPen(get_qt_enum(Qt.PenStyle if hasattr(Qt, 'PenStyle') else Qt, 'NoPen'))
            painter.setBrush(QBrush(color))
            painter.drawEllipse(QPointF(cx, cy), 3.5, 3.5)

            # Perimeter resize handle
            if is_selected and not is_preview:
                hx, hy = self.screen_to_canvas(zone.x + zone.radius, zone.y)
                painter.setPen(QPen(QColor("#ffffff"), 2))
                painter.setBrush(QBrush(color))
                painter.drawEllipse(QPointF(hx, hy), 6, 6)

            text_rect = QRectF(cx - r * 0.9, cy - r * 0.9, r * 1.8, r * 1.8)
        else:
            rx, ry = self.screen_to_canvas(zone.x, zone.y)
            rw = zone.width * s
            rh = zone.height * s
            painter.drawRect(QRectF(rx, ry, rw, rh))

            if is_selected and not is_preview:
                painter.setPen(QPen(QColor("#ffffff"), 1.5))
                painter.setBrush(QBrush(color))
                hs = 7.5
                for hx, hy in [(rx, ry), (rx + rw, ry), (rx, ry + rh), (rx + rw, ry + rh),
                               (rx + rw / 2, ry), (rx + rw / 2, ry + rh),
                               (rx, ry + rh / 2), (rx + rw, ry + rh / 2)]:
                    painter.drawRect(QRectF(hx - hs / 2, hy - hs / 2, hs, hs))

            text_rect = QRectF(rx + 4, ry + 4, rw - 8, rh - 8)

        # Render Label Inside the Shape
        label_str = LABEL_MAP.get(zone.kind, zone.kind)
        if zone.kind == "movement_joystick":
            label_str = "S · 落点\nE · 方向"
        elif zone.kind == "look_touch":
            label_str = "T · 视角触摸区"
        elif zone.kind == "key":
            label_str = f"{zone.key}"
            if zone.release_movement or zone.release_look:
                extra = []
                if zone.release_movement: extra.append("松左")
                if zone.release_look: extra.append("松右")
                label_str += f"\n[{','.join(extra)}]"

        font_size = max(10, min(18, int(12 * min(2.0, max(0.8, s)))))
        font = QFont("sans-serif", font_size, QFont.Weight.Bold if hasattr(QFont, 'Weight') else QFont.Bold)
        painter.setFont(font)

        # High-contrast outline text
        painter.setPen(QColor(0, 0, 0, 210))
        for dx, dy in [(-1, 0), (1, 0), (0, -1), (0, 1)]:
            shifted = text_rect.translated(dx, dy)
            painter.drawText(shifted, get_qt_enum(Qt.AlignmentFlag if hasattr(Qt, 'AlignmentFlag') else Qt, 'AlignCenter'), label_str)

        painter.setPen(QColor("#ffffff"))
        painter.drawText(text_rect, get_qt_enum(Qt.AlignmentFlag if hasattr(Qt, 'AlignmentFlag') else Qt, 'AlignCenter'), label_str)

    def mousePressEvent(self, event):
        btn = event.button()
        left_btn = get_qt_enum(Qt.MouseButton if hasattr(Qt, 'MouseButton') else Qt, 'LeftButton')
        if btn == left_btn:
            self.setFocus()
            pos = event.pos() if hasattr(event, 'pos') else event.position().toPoint()
            sx, sy = self.canvas_to_screen(pos.x(), pos.y())

            # Strictly ignore clicks that start outside the active screenshot area
            if not self.is_inside_image(sx, sy):
                return

            # 1. Handle selection resize
            if 0 <= self.selected_index < len(self.zones):
                sel_zone = self.zones[self.selected_index]
                handle = self.get_zone_handle(sel_zone, sx, sy)
                if handle:
                    self.drag_mode = handle
                    self.drag_start = (sx, sy)
                    self.drag_data = (sel_zone.x, sel_zone.y, sel_zone.radius, sel_zone.width, sel_zone.height)
                    self.update()
                    return

            # 2. Check hit on existing zones
            hit_index = -1
            for idx in reversed(range(len(self.zones))):
                if self.zones[idx].hit_test(sx, sy):
                    hit_index = idx
                    break

            if hit_index != -1:
                self.selected_index = hit_index
                zone = self.zones[hit_index]
                handle = self.get_zone_handle(zone, sx, sy)
                self.drag_mode = handle if handle else "move"
                self.drag_start = (sx, sy)
                self.drag_data = (zone.x, zone.y, zone.radius, zone.width, zone.height)
                self.zone_selected.emit(self.selected_index)
                self.update()
            else:
                # 3. Create new zone strictly inside image
                self.selected_index = -1
                self.drag_mode = "create"
                self.drag_start = (sx, sy)
                self.preview_zone = self.create_zone_from_drag((sx, sy), (sx, sy))
                self.zone_selected.emit(-1)
                self.update()

    def mouseMoveEvent(self, event):
        pos = event.pos() if hasattr(event, 'pos') else event.position().toPoint()
        sx, sy = self.canvas_to_screen(pos.x(), pos.y())
        inside = self.is_inside_image(sx, sy)

        # Emit cursor coordinates for status bar
        clamped_sx, clamped_sy = self.clamp_point(sx, sy)
        self.cursor_moved.emit(int(clamped_sx), int(clamped_sy), inside)

        # Update cursor shape
        if not self.drag_mode:
            if inside:
                cursor = get_qt_enum(Qt.CursorShape if hasattr(Qt, 'CursorShape') else Qt, 'CrossCursor')
                if 0 <= self.selected_index < len(self.zones):
                    handle = self.get_zone_handle(self.zones[self.selected_index], sx, sy)
                    if handle in ("resize_nw", "resize_se"):
                        cursor = get_qt_enum(Qt.CursorShape if hasattr(Qt, 'CursorShape') else Qt, 'SizeFDiagCursor')
                    elif handle in ("resize_ne", "resize_sw"):
                        cursor = get_qt_enum(Qt.CursorShape if hasattr(Qt, 'CursorShape') else Qt, 'SizeBDiagCursor')
                    elif handle in ("resize_n", "resize_s"):
                        cursor = get_qt_enum(Qt.CursorShape if hasattr(Qt, 'CursorShape') else Qt, 'SizeVerCursor')
                    elif handle in ("resize_w", "resize_e", "resize_circle"):
                        cursor = get_qt_enum(Qt.CursorShape if hasattr(Qt, 'CursorShape') else Qt, 'SizeHorCursor')
                    elif self.zones[self.selected_index].hit_test(sx, sy):
                        cursor = get_qt_enum(Qt.CursorShape if hasattr(Qt, 'CursorShape') else Qt, 'SizeAllCursor')
                else:
                    for zone in self.zones:
                        if zone.hit_test(sx, sy):
                            cursor = get_qt_enum(Qt.CursorShape if hasattr(Qt, 'CursorShape') else Qt, 'PointingHandCursor')
                            break
            else:
                cursor = get_qt_enum(Qt.CursorShape if hasattr(Qt, 'CursorShape') else Qt, 'ForbiddenCursor')
            self.setCursor(QCursor(cursor))

        # Handle active drag operations bounded by image boundaries
        if self.drag_mode and self.drag_start and self.drag_data:
            orig_x, orig_y, orig_r, orig_w, orig_h = self.drag_data
            start_sx, start_sy = self.drag_start
            dx = sx - start_sx
            dy = sy - start_sy

            zone = self.zones[self.selected_index]

            if self.drag_mode == "move":
                zone.x = int(orig_x + dx)
                zone.y = int(orig_y + dy)
                zone.clamp_to_screen(self.screen_width, self.screen_height)
                self.zones_changed.emit()
                self.update()
            elif self.drag_mode == "resize_circle":
                # Maximum allowed radius within image bounds from circle center
                max_allow_r = min(zone.x, self.screen_width - zone.x, zone.y, self.screen_height - zone.y)
                req_r = int(math.hypot(sx - zone.x, sy - zone.y))
                zone.radius = max(5, min(req_r, max_allow_r))
                zone.start_radius = min(zone.start_radius, zone.radius - 1)
                self.zones_changed.emit()
                self.update()
            elif self.drag_mode.startswith("resize_"):
                mode = self.drag_mode
                cur_x, cur_y, cur_w, cur_h = orig_x, orig_y, orig_w, orig_h

                if "e" in mode:
                    cur_w = max(5, min(int(orig_w + dx), self.screen_width - orig_x))
                if "s" in mode:
                    cur_h = max(5, min(int(orig_h + dy), self.screen_height - orig_y))
                if "w" in mode:
                    new_w = max(5, min(int(orig_w - dx), orig_x + orig_w))
                    cur_x = int(orig_x + (orig_w - new_w))
                    cur_w = new_w
                if "n" in mode:
                    new_h = max(5, min(int(orig_h - dy), orig_y + orig_h))
                    cur_y = int(orig_y + (orig_h - new_h))
                    cur_h = new_h

                zone.x, zone.y, zone.width, zone.height = cur_x, cur_y, cur_w, cur_h
                zone.clamp_to_screen(self.screen_width, self.screen_height)
                self.zones_changed.emit()
                self.update()
        elif self.drag_mode == "create" and self.drag_start:
            clamped_end = self.clamp_point(sx, sy)
            self.preview_zone = self.create_zone_from_drag(self.drag_start, clamped_end)
            self.update()

    def mouseReleaseEvent(self, event):
        btn = event.button()
        left_btn = get_qt_enum(Qt.MouseButton if hasattr(Qt, 'MouseButton') else Qt, 'LeftButton')
        if btn == left_btn:
            pos = event.pos() if hasattr(event, 'pos') else event.position().toPoint()
            sx, sy = self.canvas_to_screen(pos.x(), pos.y())

            if self.drag_mode == "create" and self.drag_start:
                clamped_end = self.clamp_point(sx, sy)
                final_zone = self.create_zone_from_drag(self.drag_start, clamped_end)
                self.preview_zone = None

                if (final_zone.shape == "circle" and final_zone.radius >= 5) or \
                   (final_zone.shape == "rect" and final_zone.width >= 5 and final_zone.height >= 5):
                    final_zone.clamp_to_screen(self.screen_width, self.screen_height)
                    unique_kinds = {
                        "movement_joystick", "look_touch", "look_start_left",
                        "look_start_right", "look_start_up", "look_start_down",
                    }
                    if final_zone.kind in unique_kinds:
                        self.zones = [zone for zone in self.zones if zone.kind != final_zone.kind]
                    self.zones.append(final_zone)
                    self.selected_index = len(self.zones) - 1
                    self.zone_selected.emit(self.selected_index)
                    self.zones_changed.emit()

            self.drag_mode = None
            self.drag_start = None
            self.drag_data = None
            self.update()

    def create_zone_from_drag(self, start, end):
        sx, sy = start
        ex, ey = end

        if self.new_shape == "circle":
            max_r = min(sx, self.screen_width - sx, sy, self.screen_height - sy)
            req_r = int(math.hypot(ex - sx, ey - sy))
            radius = max(5, min(req_r, max_r if max_r > 5 else 5))
            return TouchZone(
                kind=self.new_kind,
                shape="circle",
                x=int(sx),
                y=int(sy),
                radius=radius,
                key=self.new_key,
                look_gain=self.new_look_gain,
                start_radius=self.new_start_radius,
                enter_min_delay_us=self.new_enter_min_delay_us,
                enter_max_delay_us=self.new_enter_max_delay_us,
                turn_min_delay_us=self.new_turn_min_delay_us,
                turn_max_delay_us=self.new_turn_max_delay_us,
                random_percent=self.new_random_percent,
                release_movement=self.new_rel_movement,
                release_look=self.new_rel_look,
            )
        else:
            x = int(min(sx, ex))
            y = int(min(sy, ey))
            width = max(5, int(abs(ex - sx)))
            height = max(5, int(abs(ey - sy)))
            zone = TouchZone(
                kind=self.new_kind,
                shape="rect",
                x=x,
                y=y,
                width=width,
                height=height,
                key=self.new_key,
                look_gain=self.new_look_gain,
                start_radius=self.new_start_radius,
                enter_min_delay_us=self.new_enter_min_delay_us,
                enter_max_delay_us=self.new_enter_max_delay_us,
                turn_min_delay_us=self.new_turn_min_delay_us,
                turn_max_delay_us=self.new_turn_max_delay_us,
                random_percent=self.new_random_percent,
                release_movement=self.new_rel_movement,
                release_look=self.new_rel_look,
            )
            zone.clamp_to_screen(self.screen_width, self.screen_height)
            return zone


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("FPS 触摸键位编辑器 (PyQt Desktop Studio)")
        self.resize(1380, 880)
        self.setMinimumSize(1024, 680)
        self.setup_ui()
        self.apply_dark_theme()

    def setup_ui(self):
        central = QWidget(self)
        self.setCentralWidget(central)
        main_layout = QHBoxLayout(central)
        main_layout.setContentsMargins(0, 0, 0, 0)
        main_layout.setSpacing(0)

        # Left Sidebar (Professional Tool & Parameter Inspector)
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFixedWidth(360)
        scroll.setFrameShape(get_qt_enum(QFrame.Shape if hasattr(QFrame, 'Shape') else QFrame, 'NoFrame'))

        sidebar = QWidget()
        side_layout = QVBoxLayout(sidebar)
        side_layout.setContentsMargins(10, 10, 10, 10)
        side_layout.setSpacing(8)

        # 1. Quick Toolbar Card (Open, Auto-res, Import, Export, Clear)
        bar_card = QFrame()
        bar_card.setObjectName("card")
        bar_layout = QGridLayout(bar_card)
        bar_layout.setContentsMargins(12, 12, 12, 12)
        bar_layout.setSpacing(8)

        self.btn_load_image = QPushButton("📁 打开游戏截图")
        self.btn_load_image.clicked.connect(self.on_load_image_clicked)
        bar_layout.addWidget(self.btn_load_image, 0, 0)

        self.btn_fit_image_res = QPushButton("📐 贴合截图尺寸")
        self.btn_fit_image_res.clicked.connect(self.on_fit_image_res_clicked)
        bar_layout.addWidget(self.btn_fit_image_res, 0, 1)

        self.btn_import = QPushButton("📥 导入配置")
        self.btn_import.clicked.connect(self.on_import_clicked)
        bar_layout.addWidget(self.btn_import, 1, 0)

        self.btn_export = QPushButton("💾 导出 map.json")
        self.btn_export.setObjectName("btnPrimary")
        self.btn_export.clicked.connect(self.on_export_clicked)
        bar_layout.addWidget(self.btn_export, 1, 1)

        self.lbl_image_info = QLabel("未加载截图 (拖拽图片至右侧)")
        self.lbl_image_info.setStyleSheet("color: #64748b; font-size: 11px;")
        bar_layout.addWidget(self.lbl_image_info, 2, 0, 1, 2)

        side_layout.addWidget(bar_card)

        # 2. Type selection: T plus four directional re-entry zones.
        type_card = QFrame()
        type_card.setObjectName("card")
        type_layout = QGridLayout(type_card)
        type_layout.setContentsMargins(10, 10, 10, 10)
        type_layout.setSpacing(6)

        self.btn_group_type = QButtonGroup(self)
        self.type_cards = []

        types_meta = [
            ("movement_joystick", "🎮 左摇杆", "WASD 方向控制 · 槽位 0", "#38bdf8"),
            ("look_touch", "👁️ 视角 T", "鼠标连续拖动区域 · 槽位 1", "#a78bfa"),
            ("look_start_left", "⬅️ S-Left", "左移从此区随机按下，应放在 T 右侧", "#f472b6"),
            ("look_start_right", "➡️ S-Right", "右移从此区随机按下，应放在 T 左侧", "#f472b6"),
            ("look_start_up", "⬆️ S-Up", "上移从此区随机按下，应放在 T 下侧", "#f472b6"),
            ("look_start_down", "⬇️ S-Down", "下移从此区随机按下，应放在 T 上侧", "#f472b6"),
            ("key", "🎯 按键触点", "射击/技能/跳跃 · 槽位 2", "#fb923c"),
        ]

        for idx, (kind, title, desc, col) in enumerate(types_meta):
            btn = QPushButton()
            btn.setCheckable(True)
            btn.setObjectName(f"typeCard_{kind}")
            btn.setStyleSheet(f"""
                QPushButton {{
                    text-align: left;
                    padding: 7px 5px;
                    border: 1.5px solid #1e293b;
                    border-radius: 8px;
                    background-color: #0f172a;
                }}
                QPushButton:hover {{
                    border-color: {col};
                    background-color: #1e293b;
                }}
                QPushButton:checked {{
                    border-color: {col};
                    background-color: #172554;
                    font-weight: bold;
                }}
            """)
            btn.setText(title)
            btn.setToolTip(desc)
            if idx == 0:
                btn.setChecked(True)
            self.btn_group_type.addButton(btn, idx)
            type_layout.addWidget(btn, idx // 3, idx % 3)
            self.type_cards.append((btn, kind))

        self.btn_group_type.buttonClicked.connect(self.on_type_card_clicked)
        side_layout.addWidget(type_card)

        # 3. Shape selector
        shape_card = QFrame()
        shape_card.setObjectName("card")
        shape_vbox = QVBoxLayout(shape_card)
        shape_vbox.setContentsMargins(10, 8, 10, 8)
        shape_vbox.setSpacing(4)

        shape_hbox = QHBoxLayout()
        shape_hbox.setSpacing(8)
        self.btn_group_shape = QButtonGroup(self)

        self.btn_shape_circle = QPushButton("⚪ 圆形")
        self.btn_shape_circle.setCheckable(True)
        self.btn_shape_circle.setChecked(True)
        self.btn_shape_circle.setObjectName("shapeBtn")

        self.btn_shape_rect = QPushButton("⬛ 矩形")
        self.btn_shape_rect.setCheckable(True)
        self.btn_shape_rect.setObjectName("shapeBtn")

        self.btn_group_shape.addButton(self.btn_shape_circle, 0)
        self.btn_group_shape.addButton(self.btn_shape_rect, 1)
        self.btn_group_shape.buttonClicked.connect(self.on_shape_btn_clicked)

        shape_hbox.addWidget(self.btn_shape_circle)
        shape_hbox.addWidget(self.btn_shape_rect)
        shape_vbox.addLayout(shape_hbox)
        side_layout.addWidget(shape_card)

        # 4. New Zone Parameters Card
        self.param_card = QFrame()
        self.param_card.setObjectName("card")
        param_vbox = QVBoxLayout(self.param_card)
        param_vbox.setContentsMargins(10, 10, 10, 10)
        param_vbox.setSpacing(6)

        lbl_param_title = QLabel("参数配置")
        lbl_param_title.setObjectName("sectionTitle")
        param_vbox.addWidget(lbl_param_title)

        # Key selection widget (preset dropdown + physical-key capture)
        self.widget_key_selector = QWidget()
        k_layout = QGridLayout(self.widget_key_selector)
        k_layout.setContentsMargins(0, 0, 0, 0)
        k_layout.setHorizontalSpacing(6)
        k_layout.setVerticalSpacing(5)

        k_layout.addWidget(QLabel("预设"), 0, 0)
        self.combo_key_preset = QComboBox()
        self.combo_key_preset.addItem("选择快捷预设…", "")
        for key_code, description in QUICK_KEYS:
            self.combo_key_preset.addItem(f"{description}  ·  {key_code}", key_code)
        self.combo_key_preset.currentIndexChanged.connect(self.on_key_preset_changed)
        k_layout.addWidget(self.combo_key_preset, 0, 1, 1, 3)

        k_layout.addWidget(QLabel("绑定"), 1, 0)
        self.edit_key_input = KeyCaptureLineEdit("BTN_LEFT")
        self.edit_key_input.setPlaceholderText("点击后直接按键，或粘贴 KEY_…")
        self.edit_key_input.setToolTip("点击输入框后按键将自动识别为 evdev 代码；鼠标按键请从预设选择。")
        self.edit_key_input.textChanged.connect(self.on_key_text_changed)
        self.edit_key_input.key_captured.connect(self.on_key_captured)
        k_layout.addWidget(self.edit_key_input, 1, 1, 1, 3)

        k_layout.addWidget(QLabel("随机"), 2, 0)
        self.spin_random = QSpinBox()
        self.spin_random.setRange(0, 100)
        self.spin_random.setSuffix(" %")
        self.spin_random.setValue(60)
        self.spin_random.valueChanged.connect(self.on_random_changed)
        k_layout.addWidget(self.spin_random, 2, 1)

        self.chk_rel_mov = QCheckBox("松左摇杆")
        self.chk_rel_mov.setToolTip("按下此键时立即松开 WASD 左侧移动摇杆")
        self.chk_rel_mov.toggled.connect(self.on_rel_mov_toggled)
        k_layout.addWidget(self.chk_rel_mov, 2, 2)

        self.chk_rel_look = QCheckBox("松右摇杆")
        self.chk_rel_look.setToolTip("按下此键时立即松开鼠标控制的右侧视角摇杆")
        self.chk_rel_look.toggled.connect(self.on_rel_look_toggled)
        k_layout.addWidget(self.chk_rel_look, 2, 3)
        param_vbox.addWidget(self.widget_key_selector)

        # Movement joystick: concentric S (random landing) and E (direction limit) circles.
        self.widget_movement_params = QWidget()
        movement_layout = QGridLayout(self.widget_movement_params)
        movement_layout.setContentsMargins(0, 0, 0, 0)
        movement_layout.setHorizontalSpacing(6)
        movement_layout.setVerticalSpacing(5)

        movement_layout.addWidget(QLabel("S 半径"), 0, 0)
        self.spin_start_radius = QSpinBox()
        self.spin_start_radius.setRange(0, 5000)
        self.spin_start_radius.setSuffix(" px")
        self.spin_start_radius.setValue(30)
        self.spin_start_radius.valueChanged.connect(self.on_start_radius_changed)
        movement_layout.addWidget(self.spin_start_radius, 0, 1)

        movement_layout.addWidget(QLabel("S→E"), 1, 0)
        self.spin_enter_delay = QSpinBox()
        self.spin_enter_delay.setRange(0, 100000)
        self.spin_enter_delay.setSuffix(" μs")
        self.spin_enter_delay.setValue(800)
        self.spin_enter_delay.valueChanged.connect(self.on_enter_delay_changed)
        movement_layout.addWidget(self.spin_enter_delay, 1, 1)
        movement_layout.addWidget(QLabel("至"), 1, 2)
        self.spin_enter_delay_max = QSpinBox()
        self.spin_enter_delay_max.setRange(0, 100000)
        self.spin_enter_delay_max.setSuffix(" μs")
        self.spin_enter_delay_max.setValue(2200)
        self.spin_enter_delay_max.valueChanged.connect(self.on_enter_delay_max_changed)
        movement_layout.addWidget(self.spin_enter_delay_max, 1, 3)

        movement_layout.addWidget(QLabel("转向"), 2, 0)
        self.spin_turn_delay = QSpinBox()
        self.spin_turn_delay.setRange(0, 100000)
        self.spin_turn_delay.setSuffix(" μs")
        self.spin_turn_delay.setValue(400)
        self.spin_turn_delay.valueChanged.connect(self.on_turn_delay_changed)
        movement_layout.addWidget(self.spin_turn_delay, 2, 1)
        movement_layout.addWidget(QLabel("至"), 2, 2)
        self.spin_turn_delay_max = QSpinBox()
        self.spin_turn_delay_max.setRange(0, 100000)
        self.spin_turn_delay_max.setSuffix(" μs")
        self.spin_turn_delay_max.setValue(1400)
        self.spin_turn_delay_max.valueChanged.connect(self.on_turn_delay_max_changed)
        movement_layout.addWidget(self.spin_turn_delay_max, 2, 3)

        movement_hint = QLabel("S 内随机落点；方向长度随机落在 S 外、E 内。E 半径直接拖动外圈修改。")
        movement_hint.setStyleSheet("color: #94a3b8; font-size: 10px;")
        movement_hint.setWordWrap(True)
        movement_layout.addWidget(movement_hint, 3, 0, 1, 4)
        param_vbox.addWidget(self.widget_movement_params)

        # Look touch uses the canvas geometry directly: T bounds and four start zones.
        self.widget_look_params = QWidget()
        j_layout = QGridLayout(self.widget_look_params)
        j_layout.setContentsMargins(0, 0, 0, 0)
        j_layout.setHorizontalSpacing(6)
        j_layout.setVerticalSpacing(5)

        j_layout.addWidget(QLabel("灵敏度"), 0, 0)
        self.spin_look_gain = QSpinBox()
        self.spin_look_gain.setRange(1, 512)
        self.spin_look_gain.setSuffix(" ×")
        self.spin_look_gain.setValue(8)
        self.spin_look_gain.setToolTip("每个鼠标相对位移转换为的屏幕触摸像素；数值越小越慢。")
        self.spin_look_gain.valueChanged.connect(self.on_look_gain_changed)
        j_layout.addWidget(self.spin_look_gain, 0, 1)

        look_hint = QLabel("先绘制 T，再在 T 内放置四个 S 区。S-Left 放右侧、S-Right 放左侧、S-Up 放下侧、S-Down 放上侧；直接在画布拖拽修改位置和大小。")
        look_hint.setStyleSheet("color: #94a3b8; font-size: 10px;")
        look_hint.setWordWrap(True)
        j_layout.addWidget(look_hint, 1, 0, 1, 4)
        param_vbox.addWidget(self.widget_look_params)

        side_layout.addWidget(self.param_card)

        # 5. Global Joystick Release Keys
        glob_card = QFrame()
        glob_card.setObjectName("card")
        glob_vbox = QGridLayout(glob_card)
        glob_vbox.setContentsMargins(10, 10, 10, 10)
        glob_vbox.setHorizontalSpacing(6)
        glob_vbox.setVerticalSpacing(5)

        lbl_glob_title = QLabel("全局摇杆释放快捷键")
        lbl_glob_title.setObjectName("sectionTitle")
        glob_vbox.addWidget(lbl_glob_title, 0, 0, 1, 2)

        glob_vbox.addWidget(QLabel("左摇杆"), 1, 0)
        self.edit_global_rel_mov = QLineEdit("KEY_TAB, KEY_M")
        self.edit_global_rel_mov.setPlaceholderText("KEY_TAB, KEY_M")
        glob_vbox.addWidget(self.edit_global_rel_mov, 1, 1)

        glob_vbox.addWidget(QLabel("右摇杆"), 2, 0)
        self.edit_global_rel_look = QLineEdit("KEY_TAB, KEY_M")
        self.edit_global_rel_look.setPlaceholderText("KEY_TAB, KEY_M")
        glob_vbox.addWidget(self.edit_global_rel_look, 2, 1)
        side_layout.addWidget(glob_card)

        # 6. Configured Zones List
        list_card = QFrame()
        list_card.setObjectName("card")
        list_vbox = QVBoxLayout(list_card)
        list_vbox.setContentsMargins(12, 12, 12, 12)
        list_vbox.setSpacing(8)

        lbl_list_title = QLabel("已配置区域图表")
        lbl_list_title.setObjectName("sectionTitle")
        list_vbox.addWidget(lbl_list_title)

        self.zone_list = QListWidget()
        self.zone_list.setMinimumHeight(160)
        self.zone_list.currentRowChanged.connect(self.on_zone_list_row_changed)
        list_vbox.addWidget(self.zone_list)

        del_btn_box = QHBoxLayout()
        self.btn_delete_zone = QPushButton("🗑️ 删除选中 (Delete)")
        self.btn_delete_zone.setObjectName("btnDanger")
        self.btn_delete_zone.clicked.connect(self.on_delete_zone_clicked)
        self.btn_clear = QPushButton("💥 清空全部")
        self.btn_clear.setObjectName("btnDarkDanger")
        self.btn_clear.clicked.connect(self.on_clear_clicked)
        del_btn_box.addWidget(self.btn_delete_zone)
        del_btn_box.addWidget(self.btn_clear)
        list_vbox.addLayout(del_btn_box)

        side_layout.addWidget(list_card)

        # 7. Screen Resolution Fine-tuning
        res_card = QFrame()
        res_card.setObjectName("card")
        res_grid = QGridLayout(res_card)
        res_grid.setContentsMargins(10, 8, 10, 8)
        res_grid.setSpacing(6)

        lbl_res_title = QLabel("分辨率基准")
        lbl_res_title.setObjectName("sectionTitle")
        res_grid.addWidget(lbl_res_title, 0, 0, 1, 4)

        res_grid.addWidget(QLabel("W:"), 1, 0)
        self.spin_width = QSpinBox()
        self.spin_width.setRange(100, 10000)
        self.spin_width.setValue(1080)
        self.spin_width.valueChanged.connect(self.on_screen_size_changed)
        res_grid.addWidget(self.spin_width, 1, 1)

        res_grid.addWidget(QLabel("H:"), 1, 2)
        self.spin_height = QSpinBox()
        self.spin_height.setRange(100, 10000)
        self.spin_height.setValue(2400)
        self.spin_height.valueChanged.connect(self.on_screen_size_changed)
        res_grid.addWidget(self.spin_height, 1, 3)

        side_layout.addWidget(res_card)

        scroll.setWidget(sidebar)
        main_layout.addWidget(scroll)

        # Right Viewport: Canvas & Status Bar
        canvas_container = QWidget()
        canvas_vbox = QVBoxLayout(canvas_container)
        canvas_vbox.setContentsMargins(0, 0, 0, 0)
        canvas_vbox.setSpacing(0)

        self.canvas = CanvasWidget()
        self.canvas.zone_selected.connect(self.on_canvas_zone_selected)
        self.canvas.zones_changed.connect(self.on_canvas_zones_changed)
        self.canvas.cursor_moved.connect(self.on_cursor_moved)
        canvas_vbox.addWidget(self.canvas, 1)

        # Status Bar
        self.status_bar = QStatusBar()
        self.status_bar.setStyleSheet("background-color: #0b0e14; color: #64748b; font-size: 11px; padding: 2px 10px;")
        self.lbl_status_pos = QLabel("坐标: (-, -)")
        self.lbl_status_res = QLabel("分辨率: 1080 x 2400")
        self.lbl_status_zones = QLabel("触点总数: 0")
        self.status_bar.addWidget(self.lbl_status_pos)
        self.status_bar.addPermanentWidget(self.lbl_status_res)
        self.status_bar.addPermanentWidget(self.lbl_status_zones)
        canvas_vbox.addWidget(self.status_bar)

        main_layout.addWidget(canvas_container, 1)

        # Update initial UI state
        self.update_type_ui_state()

    def apply_dark_theme(self):
        self.setStyleSheet("""
            QMainWindow, QWidget {
                background-color: #090c10;
                color: #e2e8f0;
                font-family: system-ui, -apple-system, sans-serif;
            }
            QFrame#card {
                background-color: #131922;
                border: 1px solid #1e293b;
                border-radius: 10px;
            }
            QLabel#sectionTitle {
                font-weight: bold;
                font-size: 12px;
                color: #38bdf8;
                margin-bottom: 2px;
            }
            QLineEdit, QSpinBox, QComboBox {
                background-color: #0b0e14;
                border: 1px solid #334155;
                border-radius: 6px;
                padding: 5px 8px;
                color: #f8fafc;
                font-size: 12px;
            }
            QLineEdit:focus, QSpinBox:focus, QComboBox:focus {
                border-color: #38bdf8;
            }
            QComboBox::drop-down {
                border: none;
                width: 24px;
            }
            QComboBox QAbstractItemView {
                background-color: #131922;
                border: 1px solid #334155;
                selection-background-color: #0369a1;
            }
            QPushButton {
                background-color: #1e293b;
                border: 1px solid #334155;
                border-radius: 6px;
                padding: 7px 12px;
                color: #f1f5f9;
                font-size: 12px;
            }
            QPushButton:hover {
                background-color: #334155;
                border-color: #475569;
            }
            QPushButton:pressed {
                background-color: #0f172a;
            }
            QPushButton#btnPrimary {
                background-color: #0284c7;
                border: none;
                font-weight: bold;
            }
            QPushButton#btnPrimary:hover {
                background-color: #0369a1;
            }
            QPushButton#btnDanger {
                background-color: #991b1b;
                border: none;
            }
            QPushButton#btnDanger:hover {
                background-color: #b91c1c;
            }
            QPushButton#btnDarkDanger {
                background-color: #450a0a;
                border: 1px solid #7f1d1d;
                color: #fca5a5;
            }
            QPushButton#chipBtn {
                background-color: #1e293b;
                border: 1px solid #334155;
                border-radius: 4px;
                padding: 4px 6px;
                font-size: 10px;
                color: #cbd5e1;
            }
            QPushButton#chipBtn:hover {
                background-color: #0284c7;
                color: #ffffff;
                border-color: #38bdf8;
            }
            QPushButton#shapeBtn {
                padding: 8px 12px;
                border-radius: 6px;
            }
            QPushButton#shapeBtn:checked {
                background-color: #0369a1;
                border-color: #38bdf8;
                font-weight: bold;
            }
            QPushButton[typeCard_movement_joystick], QPushButton[typeCard_look_touch], QPushButton[typeCard_key] {
                padding: 7px 6px;
                font-size: 11px;
            }
            QListWidget {
                background-color: #0b0e14;
                border: 1px solid #1e293b;
                border-radius: 6px;
                padding: 4px;
            }
            QListWidget::item {
                padding: 6px 8px;
                border-radius: 4px;
                margin-bottom: 3px;
            }
            QListWidget::item:selected {
                background-color: #1e293b;
                border-left: 3px solid #38bdf8;
                color: #ffffff;
            }
            QCheckBox {
                font-size: 12px;
                color: #cbd5e1;
            }
            QCheckBox::indicator {
                width: 16px;
                height: 16px;
                border-radius: 4px;
                border: 1px solid #475569;
                background-color: #0b0e14;
            }
            QCheckBox::indicator:checked {
                background-color: #0284c7;
                border-color: #38bdf8;
            }
        """)

    def on_load_image_clicked(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "选择游戏截图", "", "图片文件 (*.png *.jpg *.jpeg *.bmp *.webp)"
        )
        if path:
            if self.canvas.load_image(path):
                self.lbl_image_info.setText(f"已加载: {os.path.basename(path)} ({self.canvas.image.width()}x{self.canvas.image.height()})")
                self.spin_width.setValue(self.canvas.image.width())
                self.spin_height.setValue(self.canvas.image.height())
            else:
                QMessageBox.warning(self, "加载失败", "无法读取选择的图片文件。")

    def on_fit_image_res_clicked(self):
        if self.canvas.image and not self.canvas.image.isNull():
            self.spin_width.setValue(self.canvas.image.width())
            self.spin_height.setValue(self.canvas.image.height())

    def on_screen_size_changed(self):
        w = self.spin_width.value()
        h = self.spin_height.value()
        self.canvas.set_screen_size(w, h)
        self.lbl_status_res.setText(f"分辨率: {w} x {h}")

    def on_type_card_clicked(self, btn):
        for b, kind in self.type_cards:
            if b == btn:
                self.canvas.new_kind = kind
                break
        if self.canvas.new_kind == "movement_joystick":
            self.canvas.new_shape = "circle"
            self.btn_shape_circle.setChecked(True)
        self.update_type_ui_state()

    def update_type_ui_state(self):
        is_key = (self.canvas.new_kind == "key")
        self.widget_key_selector.setVisible(is_key)
        self.widget_movement_params.setVisible(self.canvas.new_kind == "movement_joystick")
        self.widget_look_params.setVisible(self.canvas.new_kind == "look_touch" or
                                           self.canvas.new_kind.startswith("look_start_"))
        self.btn_shape_rect.setEnabled(self.canvas.new_kind != "movement_joystick")

    def on_shape_btn_clicked(self, btn):
        if btn == self.btn_shape_circle:
            self.canvas.new_shape = "circle"
        else:
            self.canvas.new_shape = "rect"

    def set_active_key(self, code):
        self.edit_key_input.setText(code)

    def on_key_preset_changed(self, index):
        code = self.combo_key_preset.itemData(index)
        if code:
            self.set_active_key(code)

    def on_key_captured(self, code):
        for index in range(1, self.combo_key_preset.count()):
            if self.combo_key_preset.itemData(index) == code:
                self.combo_key_preset.setCurrentIndex(index)
                return
        self.combo_key_preset.setCurrentIndex(0)

    def on_key_text_changed(self, text):
        self.canvas.new_key = text.strip()
        self.apply_to_selected_if_any()

    def on_random_changed(self, val):
        self.canvas.new_random_percent = val
        self.apply_to_selected_if_any()

    def on_look_gain_changed(self, val):
        self.canvas.new_look_gain = val
        self.apply_to_selected_if_any()

    def on_start_radius_changed(self, val):
        self.canvas.new_start_radius = val
        self.apply_to_selected_if_any()

    def on_enter_delay_changed(self, val):
        self.canvas.new_enter_min_delay_us = val
        self.apply_to_selected_if_any()

    def on_enter_delay_max_changed(self, val):
        self.canvas.new_enter_max_delay_us = val
        self.apply_to_selected_if_any()

    def on_turn_delay_changed(self, val):
        self.canvas.new_turn_min_delay_us = val
        self.apply_to_selected_if_any()

    def on_turn_delay_max_changed(self, val):
        self.canvas.new_turn_max_delay_us = val
        self.apply_to_selected_if_any()

    def on_rel_mov_toggled(self, checked):
        self.canvas.new_rel_movement = checked
        self.apply_to_selected_if_any()

    def on_rel_look_toggled(self, checked):
        self.canvas.new_rel_look = checked
        self.apply_to_selected_if_any()

    def apply_to_selected_if_any(self):
        idx = self.canvas.selected_index
        if 0 <= idx < len(self.canvas.zones):
            zone = self.canvas.zones[idx]
            if zone.kind == "key":
                zone.key = self.edit_key_input.text().strip()
                zone.random_percent = self.spin_random.value()
                zone.release_movement = self.chk_rel_mov.isChecked()
                zone.release_look = self.chk_rel_look.isChecked()
            elif zone.kind == "movement_joystick":
                zone.start_radius = min(self.spin_start_radius.value(), zone.radius - 1)
                zone.enter_min_delay_us = self.spin_enter_delay.value()
                zone.enter_max_delay_us = max(zone.enter_min_delay_us, self.spin_enter_delay_max.value())
                zone.turn_min_delay_us = self.spin_turn_delay.value()
                zone.turn_max_delay_us = max(zone.turn_min_delay_us, self.spin_turn_delay_max.value())
            elif zone.kind == "look_touch":
                zone.look_gain = self.spin_look_gain.value()
            self.sync_zone_list()
            self.canvas.update()

    def on_canvas_zone_selected(self, index):
        self.zone_list.blockSignals(True)
        self.zone_list.setCurrentRow(index)
        self.zone_list.blockSignals(False)
        self.sync_form_from_selection()

    def on_canvas_zones_changed(self):
        self.sync_zone_list()
        self.sync_form_from_selection()

    def on_zone_list_row_changed(self, row):
        self.canvas.selected_index = row
        self.canvas.update()
        self.sync_form_from_selection()

    def sync_form_from_selection(self):
        idx = self.canvas.selected_index
        if 0 <= idx < len(self.canvas.zones):
            zone = self.canvas.zones[idx]
            # Select corresponding type card
            for b, kind in self.type_cards:
                b.setChecked(kind == zone.kind)
            self.canvas.new_kind = zone.kind
            self.update_type_ui_state()

            # Select shape
            self.btn_shape_circle.setChecked(zone.shape == "circle")
            self.btn_shape_rect.setChecked(zone.shape == "rect")
            self.canvas.new_shape = zone.shape

            # Sync fields
            if zone.kind == "key":
                self.edit_key_input.blockSignals(True)
                self.spin_random.blockSignals(True)
                self.chk_rel_mov.blockSignals(True)
                self.chk_rel_look.blockSignals(True)

                self.edit_key_input.setText(zone.key)
                preset_index = 0
                for index in range(1, self.combo_key_preset.count()):
                    if self.combo_key_preset.itemData(index) == zone.key:
                        preset_index = index
                        break
                self.combo_key_preset.blockSignals(True)
                self.combo_key_preset.setCurrentIndex(preset_index)
                self.combo_key_preset.blockSignals(False)
                self.spin_random.setValue(zone.random_percent)
                self.chk_rel_mov.setChecked(zone.release_movement)
                self.chk_rel_look.setChecked(zone.release_look)

                self.edit_key_input.blockSignals(False)
                self.spin_random.blockSignals(False)
                self.chk_rel_mov.blockSignals(False)
                self.chk_rel_look.blockSignals(False)
            elif zone.kind == "movement_joystick":
                self.spin_start_radius.blockSignals(True)
                self.spin_enter_delay.blockSignals(True)
                self.spin_enter_delay_max.blockSignals(True)
                self.spin_turn_delay.blockSignals(True)
                self.spin_turn_delay_max.blockSignals(True)

                self.spin_start_radius.setValue(zone.start_radius)
                self.spin_enter_delay.setValue(zone.enter_min_delay_us)
                self.spin_enter_delay_max.setValue(zone.enter_max_delay_us)
                self.spin_turn_delay.setValue(zone.turn_min_delay_us)
                self.spin_turn_delay_max.setValue(zone.turn_max_delay_us)

                self.spin_start_radius.blockSignals(False)
                self.spin_enter_delay.blockSignals(False)
                self.spin_enter_delay_max.blockSignals(False)
                self.spin_turn_delay.blockSignals(False)
                self.spin_turn_delay_max.blockSignals(False)
            elif zone.kind == "look_touch":
                self.spin_look_gain.blockSignals(True)
                self.spin_look_gain.setValue(zone.look_gain)
                self.spin_look_gain.blockSignals(False)

    def sync_zone_list(self):
        self.zone_list.blockSignals(True)
        self.zone_list.clear()
        for idx, zone in enumerate(self.canvas.zones):
            icon = ICON_MAP.get(zone.kind, "📍")
            if zone.kind == "key":
                info = f"{icon} 按键: {zone.key} ({zone.shape})"
            elif zone.kind == "movement_joystick":
                info = f"{icon} 左摇杆 (WASD)"
            elif zone.kind == "look_touch":
                info = f"{icon} 右视角触摸区 T"
            elif zone.kind.startswith("look_start_"):
                info = f"{icon} {LABEL_MAP[zone.kind]} 起始区"
            else:
                info = f"{icon} {LABEL_MAP.get(zone.kind, zone.kind)}"

            item = QListWidgetItem(info)
            item.setToolTip(f"X:{zone.x}, Y:{zone.y}")
            self.zone_list.addItem(item)

        if 0 <= self.canvas.selected_index < len(self.canvas.zones):
            self.zone_list.setCurrentRow(self.canvas.selected_index)
        self.zone_list.blockSignals(False)
        self.lbl_status_zones.setText(f"触点总数: {len(self.canvas.zones)}")

    def on_cursor_moved(self, x, y, inside):
        if inside:
            self.lbl_status_pos.setText(f"坐标: ({x}, {y})")
        else:
            self.lbl_status_pos.setText(f"坐标: [图片外禁止放置]")

    def on_delete_zone_clicked(self):
        idx = self.canvas.selected_index
        if 0 <= idx < len(self.canvas.zones):
            del self.canvas.zones[idx]
            if self.canvas.selected_index >= len(self.canvas.zones):
                self.canvas.selected_index = len(self.canvas.zones) - 1
            self.sync_zone_list()
            self.sync_form_from_selection()
            self.canvas.update()

    def on_clear_clicked(self):
        if self.canvas.zones:
            std_btn = getattr(QMessageBox, 'StandardButton', QMessageBox)
            ret = QMessageBox.question(
                self, "确认清空", "是否确定清空所有已配置的区域？",
                get_qt_enum(std_btn, 'Yes') | get_qt_enum(std_btn, 'No')
            )
            yes_val = get_qt_enum(std_btn, 'Yes')
            if ret == yes_val:
                self.canvas.zones.clear()
                self.canvas.selected_index = -1
                self.sync_zone_list()
                self.sync_form_from_selection()
                self.canvas.update()

    def on_import_clicked(self):
        path, _ = QFileDialog.getOpenFileName(self, "导入 map.json", "", "JSON 文件 (*.json)")
        if not path or not os.path.exists(path):
            return

        try:
            with open(path, "r", encoding="utf-8") as f:
                data = json.load(f)

            if data.get("version") != 3:
                raise ValueError("仅支持新版 map.json（version: 3，look_touch + 四个 start 区）")

            screen = data.get("screen", {})
            if "width" in screen and "height" in screen:
                self.spin_width.setValue(screen["width"])
                self.spin_height.setValue(screen["height"])

            rel_mov = data.get("release_movement_keys", [])
            rel_look = data.get("release_look_keys", [])
            self.edit_global_rel_mov.setText(", ".join(rel_mov))
            self.edit_global_rel_look.setText(", ".join(rel_look))

            new_zones = []

            mov = data.get("movement_joystick")
            if mov:
                new_zones.append(TouchZone(
                    kind="movement_joystick",
                    shape="circle",
                    x=mov.get("x", 0),
                    y=mov.get("y", 0),
                    radius=mov.get("radius", 100),
                    width=mov.get("width", 100),
                    height=mov.get("height", 100),
                    start_radius=mov["start_radius"],
                    enter_min_delay_us=mov["enter_min_delay_us"],
                    enter_max_delay_us=mov["enter_max_delay_us"],
                    turn_min_delay_us=mov["turn_min_delay_us"],
                    turn_max_delay_us=mov["turn_max_delay_us"],
                ))

            look = data.get("look_touch")
            if look:
                new_zones.append(TouchZone(
                    kind="look_touch",
                    shape=look.get("shape", "circle"),
                    x=look.get("x", 0),
                    y=look.get("y", 0),
                    radius=look.get("radius", 100),
                    width=look.get("width", 100),
                    height=look.get("height", 100),
                    look_gain=look.get("gain", 8),
                ))
                for direction, kind in [
                    ("left", "look_start_left"),
                    ("right", "look_start_right"),
                    ("up", "look_start_up"),
                    ("down", "look_start_down"),
                ]:
                    start = look.get(f"start_{direction}")
                    if start:
                        new_zones.append(TouchZone(
                            kind=kind,
                            shape=start.get("shape", "circle"),
                            x=start.get("x", 0), y=start.get("y", 0),
                            radius=start.get("radius", 50),
                            width=start.get("width", 100), height=start.get("height", 100),
                        ))

            keys = data.get("keys", [])
            for k in keys:
                new_zones.append(TouchZone(
                    kind="key",
                    shape=k.get("shape", "circle"),
                    x=k.get("x", 0),
                    y=k.get("y", 0),
                    radius=k.get("radius", 50),
                    width=k.get("width", 100),
                    height=k.get("height", 100),
                    key=k.get("key", "KEY_SPACE"),
                    random_percent=k.get("random_percent", 60),
                    release_movement=k.get("release_movement", False),
                    release_look=k.get("release_look", False),
                ))

            self.canvas.zones = new_zones
            self.canvas.selected_index = 0 if new_zones else -1
            self.sync_zone_list()
            self.sync_form_from_selection()
            self.canvas.update()
            QMessageBox.information(self, "导入成功", f"已成功加载 {len(new_zones)} 个配置区域。")

        except Exception as e:
            QMessageBox.critical(self, "导入失败", f"解析 map.json 失败: {str(e)}")

    def on_export_clicked(self):
        def parse_key_list(text):
            return [x.strip() for x in text.replace("，", ",").split(",") if x.strip()]

        data = {
            "version": 3,
            "screen": {
                "width": self.spin_width.value(),
                "height": self.spin_height.value(),
            },
            "release_movement_keys": parse_key_list(self.edit_global_rel_mov.text()),
            "release_look_keys": parse_key_list(self.edit_global_rel_look.text()),
            "movement_joystick": None,
            "look_touch": None,
            "keys": []
        }
        look_starts = {}

        for zone in self.canvas.zones:
            region = {
                "shape": zone.shape,
                "x": zone.x,
                "y": zone.y,
            }
            if zone.shape == "circle":
                region["radius"] = zone.radius
            else:
                region["width"] = zone.width
                region["height"] = zone.height

            if zone.kind == "movement_joystick":
                data["movement_joystick"] = {
                    "slot": 0,
                    "start_radius": zone.start_radius,
                    "enter_min_delay_us": zone.enter_min_delay_us,
                    "enter_max_delay_us": zone.enter_max_delay_us,
                    "turn_min_delay_us": zone.turn_min_delay_us,
                    "turn_max_delay_us": zone.turn_max_delay_us,
                    **region
                }
            elif zone.kind == "look_touch":
                data["look_touch"] = {"slot": 1, "gain": zone.look_gain, **region}
            elif zone.kind.startswith("look_start_"):
                direction = zone.kind.removeprefix("look_start_")
                look_starts[f"start_{direction}"] = region
            elif zone.kind == "key":
                key_dict = {
                    "slot": 2,
                    "key": zone.key,
                    "random_percent": zone.random_percent,
                    **region
                }
                if zone.release_movement:
                    key_dict["release_movement"] = True
                if zone.release_look:
                    key_dict["release_look"] = True
                data["keys"].append(key_dict)

        if data["look_touch"] is not None:
            required_starts = {"start_left", "start_right", "start_up", "start_down"}
            if set(look_starts) != required_starts:
                QMessageBox.warning(self, "视角配置不完整", "视角 T 必须配置 S-Left、S-Right、S-Up、S-Down 四个起始区。")
                return
            data["look_touch"].update(look_starts)
        elif look_starts:
            QMessageBox.warning(self, "视角配置不完整", "已配置视角起始区，但缺少视角触摸区 T。")
            return

        path, _ = QFileDialog.getSaveFileName(self, "导出 map.json", "map.json", "JSON 文件 (*.json)")
        if path:
            try:
                with open(path, "w", encoding="utf-8") as f:
                    json.dump(data, f, indent=2, ensure_ascii=False)
                QMessageBox.information(self, "导出成功", f"已成功保存配置文件到:\n{path}")
            except Exception as e:
                QMessageBox.critical(self, "导出失败", f"保存失败: {str(e)}")


def main():
    app = QApplication(sys.argv)
    window = MainWindow()
    window.show()
    sys.exit(app.exec() if hasattr(app, 'exec') else app.exec_())


if __name__ == "__main__":
    main()

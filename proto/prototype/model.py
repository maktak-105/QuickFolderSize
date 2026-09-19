from __future__ import annotations
from PyQt6.QtCore import (
    QAbstractItemModel, QModelIndex, Qt, QSortFilterProxyModel,
)
from PyQt6.QtGui import QColor, QIcon
from PyQt6.QtWidgets import QFileIconProvider
from PyQt6.QtCore import QFileInfo

from scanner import FolderEntry
from utils import format_size
from i18n import tr

COL_NAME  = 0
COL_SIZE  = 1
COL_BAR   = 2
COL_FILES = 3
COL_MTIME = 4
COLUMN_COUNT = 5


def get_headers() -> list[str]:
    return [tr("col_name"), tr("col_size"), tr("col_ratio"), tr("col_files"), tr("col_mtime")]

# Plain int custom roles (avoids PyQt6 enum arithmetic issues)
ROLE_SORT  = Qt.ItemDataRole.UserRole.value       # 256
ROLE_RATIO = Qt.ItemDataRole.UserRole.value + 1   # 257

_icon_provider = QFileIconProvider()


def _entry_icon(entry: FolderEntry) -> QIcon:
    return _icon_provider.icon(QFileInfo(entry.path))


class FolderModel(QAbstractItemModel):
    def __init__(self, root: FolderEntry, parent=None):
        super().__init__(parent)
        self._root = root

    def update_root(self, root: FolderEntry):
        self.beginResetModel()
        self._root = root
        self.endResetModel()

    def replace_root_child(self, new_child: FolderEntry) -> None:
        """Replace the matching top-level child (by path) with a scanned result.

        Updates root.size / root.file_count so ratio bars reflect partial totals,
        then emits dataChanged for all top-level rows so ratios repaint correctly.
        """
        children = self._root.children
        for i, child in enumerate(children):
            if child.path == new_child.path:
                new_child.parent = self._root
                self._root.size       += new_child.size
                self._root.file_count += new_child.file_count
                children[i] = new_child
                # Repaint all top-level rows — ratios depend on root.size which changed
                if children:
                    self.dataChanged.emit(
                        self.index(0, 0),
                        self.index(len(children) - 1, COLUMN_COUNT - 1),
                    )
                return

    # ── QAbstractItemModel interface ──────────────────────────────────────

    def rowCount(self, parent: QModelIndex = QModelIndex()) -> int:
        node = self._node(parent)
        if not node.is_dir:
            return 0
        return len(node.children)

    def columnCount(self, parent: QModelIndex = QModelIndex()) -> int:
        return COLUMN_COUNT

    def index(self, row: int, col: int, parent: QModelIndex = QModelIndex()) -> QModelIndex:
        if not self.hasIndex(row, col, parent):
            return QModelIndex()
        parent_node = self._node(parent)
        if row < len(parent_node.children):
            return self.createIndex(row, col, parent_node.children[row])
        return QModelIndex()

    def parent(self, index: QModelIndex) -> QModelIndex:  # type: ignore[override]
        if not index.isValid():
            return QModelIndex()
        node: FolderEntry = index.internalPointer()
        p = node.parent
        if p is None or p is self._root:
            return QModelIndex()
        gp = p.parent
        if gp is None:
            return QModelIndex()
        try:
            row = gp.children.index(p)
        except ValueError:
            return QModelIndex()
        return self.createIndex(row, 0, p)

    def hasChildren(self, parent: QModelIndex = QModelIndex()) -> bool:
        node = self._node(parent)
        if not node.is_dir:
            return False
        return bool(node.children)

    def data(self, index: QModelIndex, role: int = Qt.ItemDataRole.DisplayRole):
        if not index.isValid():
            return None
        node: FolderEntry = index.internalPointer()
        col = index.column()

        if role == Qt.ItemDataRole.DisplayRole:
            return self._display(node, col)

        if role == Qt.ItemDataRole.DecorationRole and col == COL_NAME:
            return _entry_icon(node)

        if role == Qt.ItemDataRole.ForegroundRole and not node.is_accessible:
            return QColor(150, 150, 150)

        if role == ROLE_SORT:
            if col == COL_SIZE:  return node.size
            if col == COL_FILES: return node.file_count if node.is_dir else -1
            if col == COL_MTIME: return node.modified.timestamp() if node.modified else 0.0
            if col == COL_BAR:   return self._ratio(node)

        if role == ROLE_RATIO and col == COL_BAR:
            return self._ratio(node)

        return None

    def headerData(self, section: int, orientation: Qt.Orientation, role: int = Qt.ItemDataRole.DisplayRole):
        if orientation == Qt.Orientation.Horizontal and role == Qt.ItemDataRole.DisplayRole:
            return get_headers()[section]
        return None

    # ── helpers ───────────────────────────────────────────────────────────

    def _node(self, index: QModelIndex) -> FolderEntry:
        if index.isValid():
            return index.internalPointer()
        return self._root

    def _ratio(self, node: FolderEntry) -> float:
        p = node.parent
        if p is not None and p.size > 0:
            return node.size / p.size
        return 0.0

    def _display(self, node: FolderEntry, col: int) -> str:
        if col == COL_NAME:
            return node.name
        if col == COL_SIZE:
            return format_size(node.size)
        if col == COL_BAR:
            return ""
        if col == COL_FILES:
            if not node.is_dir:
                return "—"
            return f"{node.file_count:,}"
        if col == COL_MTIME:
            if node.modified:
                return node.modified.strftime("%Y/%m/%d %H:%M")
        return ""


class SortProxyModel(QSortFilterProxyModel):
    def lessThan(self, left: QModelIndex, right: QModelIndex) -> bool:
        col = left.column()
        if col in (COL_SIZE, COL_FILES, COL_MTIME, COL_BAR):
            lv = self.sourceModel().data(left,  ROLE_SORT)
            rv = self.sourceModel().data(right, ROLE_SORT)
            try:
                return float(lv) < float(rv)
            except (TypeError, ValueError):
                pass
        lv = self.sourceModel().data(left,  Qt.ItemDataRole.DisplayRole) or ""
        rv = self.sourceModel().data(right, Qt.ItemDataRole.DisplayRole) or ""
        return str(lv).lower() < str(rv).lower()

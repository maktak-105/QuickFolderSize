from __future__ import annotations
import os
import sys
from dataclasses import dataclass, field

from PyQt6.QtCore import QAbstractItemModel, QModelIndex, Qt
from PyQt6.QtGui import QIcon
from PyQt6.QtWidgets import QFileIconProvider
from PyQt6.QtCore import QFileInfo

_icon_provider = QFileIconProvider()


@dataclass
class NavNode:
    name: str
    path: str
    parent: "NavNode | None" = field(default=None, repr=False, compare=False)
    children: list["NavNode"] = field(default_factory=list)
    is_loaded: bool = False


class NavModel(QAbstractItemModel):
    """Drive + folder tree for the left navigation pane. Uses lazy loading."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self._root = NavNode(name="[root]", path="")
        self._load_drives()

    # ── drive enumeration ─────────────────────────────────────────────────

    def _load_drives(self):
        if sys.platform == "win32":
            import string
            from ctypes import windll
            bitmask = windll.kernel32.GetLogicalDrives()
            for letter in string.ascii_uppercase:
                if bitmask & 1:
                    drive = letter + ":\\"
                    node = NavNode(name=drive, path=drive, parent=self._root)
                    self._root.children.append(node)
                bitmask >>= 1
        else:
            node = NavNode(name="/", path="/", parent=self._root)
            self._root.children.append(node)
        self._root.is_loaded = True

    # ── QAbstractItemModel interface ──────────────────────────────────────

    def rowCount(self, parent: QModelIndex = QModelIndex()) -> int:
        return len(self._node(parent).children)

    def columnCount(self, parent: QModelIndex = QModelIndex()) -> int:
        return 1

    def index(self, row: int, col: int, parent: QModelIndex = QModelIndex()) -> QModelIndex:
        if not self.hasIndex(row, col, parent):
            return QModelIndex()
        p = self._node(parent)
        if row < len(p.children):
            return self.createIndex(row, col, p.children[row])
        return QModelIndex()

    def parent(self, index: QModelIndex) -> QModelIndex:  # type: ignore[override]
        if not index.isValid():
            return QModelIndex()
        node: NavNode = index.internalPointer()
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

    def data(self, index: QModelIndex, role: int = Qt.ItemDataRole.DisplayRole):
        if not index.isValid():
            return None
        node: NavNode = index.internalPointer()
        if role == Qt.ItemDataRole.DisplayRole:
            return node.name
        if role == Qt.ItemDataRole.DecorationRole:
            return _icon_provider.icon(QFileInfo(node.path))
        return None

    def hasChildren(self, parent: QModelIndex = QModelIndex()) -> bool:
        node = self._node(parent)
        if not node.is_loaded:
            return True  # assume has children until loaded
        return bool(node.children)

    def canFetchMore(self, parent: QModelIndex = QModelIndex()) -> bool:
        return not self._node(parent).is_loaded

    def fetchMore(self, parent: QModelIndex = QModelIndex()):
        node = self._node(parent)
        if node.is_loaded:
            return
        children: list[NavNode] = []
        try:
            with os.scandir(node.path) as it:
                for entry in it:
                    try:
                        if entry.is_dir(follow_symlinks=False) and not entry.is_symlink():
                            children.append(NavNode(name=entry.name, path=entry.path, parent=node))
                    except OSError:
                        pass
            children.sort(key=lambda n: n.name.lower())
        except (PermissionError, OSError):
            pass

        if children:
            self.beginInsertRows(parent, 0, len(children) - 1)
            node.children = children
            node.is_loaded = True
            self.endInsertRows()
        else:
            node.is_loaded = True

    # ── helper ────────────────────────────────────────────────────────────

    def _node(self, index: QModelIndex) -> NavNode:
        if index.isValid():
            return index.internalPointer()
        return self._root

    def path_for(self, index: QModelIndex) -> str:
        if index.isValid():
            return index.internalPointer().path
        return ""

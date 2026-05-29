from __future__ import annotations
import os
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass, field
from datetime import datetime

from PyQt6.QtCore import QThread, pyqtSignal


@dataclass
class FolderEntry:
    name: str
    path: str
    size: int = 0
    file_count: int = 0          # recursive total
    modified: datetime = field(default_factory=datetime.now)
    is_accessible: bool = True
    is_dir: bool = True          # False for file leaf nodes
    children: list["FolderEntry"] = field(default_factory=list)
    parent: "FolderEntry | None" = field(default=None, repr=False, compare=False)
    loaded: bool = False
    # Raw st_mtime float stored for cache comparison — avoids datetime roundtrip
    # precision loss (NTFS has 100 ns resolution; datetime truncates to microseconds).
    _mtime_raw: float = field(default=0.0, repr=False, compare=False)


def build_cache(root: FolderEntry) -> dict[str, FolderEntry]:
    """Walk scan result and return path → FolderEntry for directory nodes."""
    cache: dict[str, FolderEntry] = {}

    def _walk(node: FolderEntry) -> None:
        if node.is_dir:
            cache[node.path] = node
            for child in node.children:
                _walk(child)

    _walk(root)
    return cache


class ScanWorker(QThread):
    """Recursively scans a directory in a background thread.

    Pass a cache dict (from a previous build_cache() call) to skip unchanged
    subdirectories — only directories whose mtime has changed are re-scanned.
    """

    scan_finished = pyqtSignal(object) # root FolderEntry
    scan_error    = pyqtSignal(str)

    # Main thread polls this attribute via QTimer instead of using cross-thread
    # signals.  Signal emissions from 8 parallel threads caused Qt event-queue
    # lock contention that tripled scan time even with throttling.
    current_path: str = ""

    def __init__(self, root_path: str, parent=None,
                 cache: dict[str, FolderEntry] | None = None):
        super().__init__(parent)
        self._root_path = root_path
        self._cancelled = False
        self._cache: dict[str, FolderEntry] = cache or {}
        self.current_path = ""

    def cancel(self) -> None:
        self._cancelled = True

    def run(self) -> None:
        try:
            root = self._scan_root(self._root_path)
            if not self._cancelled:
                self.scan_finished.emit(root)
        except Exception as exc:
            self.scan_error.emit(str(exc))

    # ── root: always re-read direct entries, dispatch subdirs in parallel ──

    def _scan_root(self, path: str) -> FolderEntry:
        self.current_path = path
        node = FolderEntry(name=os.path.basename(path) or path, path=path)

        try:
            mtime_ts = os.stat(path).st_mtime
            node.modified = datetime.fromtimestamp(mtime_ts)
            node._mtime_raw = mtime_ts
        except OSError:
            pass

        try:
            raw = list(os.scandir(path))
        except (PermissionError, OSError):
            node.is_accessible = False
            node.loaded = True
            return node

        dirs  = [e for e in raw if not e.is_symlink() and e.is_dir(follow_symlinks=False)]
        files = [e for e in raw if not e.is_symlink() and e.is_file(follow_symlinks=False)]

        file_children: list[FolderEntry] = []
        for e in files:
            try:
                st = e.stat(follow_symlinks=False)
                node.size       += st.st_size
                node.file_count += 1
                fe = FolderEntry(name=e.name, path=e.path, size=st.st_size,
                                 is_dir=False, parent=node)
                try:
                    fe.modified = datetime.fromtimestamp(st.st_mtime)
                    fe._mtime_raw = st.st_mtime
                except OSError:
                    pass
                file_children.append(fe)
            except (PermissionError, OSError):
                pass

        dir_children: list[FolderEntry] = []
        workers = min(8, max(1, len(dirs)))
        with ThreadPoolExecutor(max_workers=workers) as pool:
            futures = {pool.submit(self._scan_dir, e.path, node): e for e in dirs}
            for future in as_completed(futures):
                if self._cancelled:
                    break
                try:
                    child = future.result()
                    node.size       += child.size
                    node.file_count += child.file_count
                    dir_children.append(child)
                except Exception:
                    pass

        dir_children.sort(key=lambda c: c.size, reverse=True)
        file_children.sort(key=lambda c: c.size, reverse=True)
        node.children = dir_children + file_children
        node.loaded = True
        return node

    # ── per-directory: cache check → reuse or full scan ───────────────────

    def _scan_dir(self, path: str, parent: FolderEntry | None) -> FolderEntry:
        if self._cancelled:
            return FolderEntry(name=os.path.basename(path), path=path)

        self.current_path = path  # before stat — restores original GIL release pattern

        try:
            mtime_ts = os.stat(path).st_mtime
        except OSError:
            node = FolderEntry(name=os.path.basename(path) or path, path=path, parent=parent)
            node.is_accessible = False
            return node

        cached = self._cache.get(path)
        if cached is not None and abs(cached._mtime_raw - mtime_ts) < 0.001:
            return self._from_cache(cached, parent, mtime_ts)

        return self._full_scan(path, parent, mtime_ts)

    def _from_cache(self, cached: FolderEntry, parent: FolderEntry | None,
                    mtime_ts: float) -> FolderEntry:
        """Rebuild node from cache, recursing into subdirs to check their mtimes."""
        node = FolderEntry(
            name=cached.name,
            path=cached.path,
            modified=datetime.fromtimestamp(mtime_ts),
            parent=parent,
        )
        node._mtime_raw = mtime_ts

        file_children: list[FolderEntry] = []
        for c in cached.children:
            if not c.is_dir:
                fe = FolderEntry(name=c.name, path=c.path, size=c.size,
                                 is_dir=False, modified=c.modified, parent=node)
                fe._mtime_raw = c._mtime_raw
                node.size       += fe.size
                node.file_count += 1
                file_children.append(fe)

        dir_children: list[FolderEntry] = []
        for c in cached.children:
            if c.is_dir and not self._cancelled:
                child = self._scan_dir(c.path, node)
                node.size       += child.size
                node.file_count += child.file_count
                dir_children.append(child)

        dir_children.sort(key=lambda c: c.size, reverse=True)
        file_children.sort(key=lambda c: c.size, reverse=True)
        node.children = dir_children + file_children
        node.loaded = True
        return node

    def _full_scan(self, path: str, parent: FolderEntry | None,
                   mtime_ts: float) -> FolderEntry:
        """Full recursive scan — used when cache is absent or mtime changed."""
        node = FolderEntry(
            name=os.path.basename(path) or path,
            path=path,
            modified=datetime.fromtimestamp(mtime_ts),
            parent=parent,
        )
        node._mtime_raw = mtime_ts

        try:
            raw = list(os.scandir(path))
        except (PermissionError, OSError):
            node.is_accessible = False
            node.loaded = True
            return node

        dir_children: list[FolderEntry] = []
        file_children: list[FolderEntry] = []

        for entry in raw:
            if self._cancelled:
                break
            try:
                if entry.is_symlink():
                    continue
                if entry.is_dir(follow_symlinks=False):
                    child = self._scan_dir(entry.path, parent=node)
                    node.size       += child.size
                    node.file_count += child.file_count
                    dir_children.append(child)
                elif entry.is_file(follow_symlinks=False):
                    st = entry.stat(follow_symlinks=False)
                    node.size       += st.st_size
                    node.file_count += 1
                    fe = FolderEntry(
                        name=entry.name, path=entry.path,
                        size=st.st_size, file_count=0,
                        is_dir=False, parent=node,
                    )
                    try:
                        fe.modified = datetime.fromtimestamp(st.st_mtime)
                        fe._mtime_raw = st.st_mtime
                    except OSError:
                        pass
                    file_children.append(fe)
            except (PermissionError, OSError):
                pass

        dir_children.sort(key=lambda c: c.size, reverse=True)
        file_children.sort(key=lambda c: c.size, reverse=True)
        node.children = dir_children + file_children
        node.loaded = True
        return node

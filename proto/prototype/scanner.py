from __future__ import annotations
import os
import threading
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
from datetime import datetime

from PyQt6.QtCore import QThread, pyqtSignal

# Single shared pool used for every directory at every depth. Workers never
# block waiting on another task submitted to this same pool (see _fan_out) —
# each task either finishes immediately or fans out to children and returns,
# so there is no deadlock risk, and the OS thread count stays bounded by
# _MAX_WORKERS regardless of tree depth or width.
_MAX_WORKERS = 32


def _is_real_dir(entry: os.DirEntry) -> bool:
    """True if entry is a directory that is safe to recurse into.

    Excludes symlinks and NTFS junctions/mount points. is_dir() is checked
    first so the symlink/junction checks are skipped for file entries.
    """
    if not entry.is_dir(follow_symlinks=False):
        return False
    return not entry.is_symlink() and not entry.is_junction()


@dataclass
class FolderEntry:
    name: str
    path: str
    size: int = 0
    file_count: int = 0
    modified: datetime = field(default_factory=datetime.now)
    is_accessible: bool = True
    is_dir: bool = True
    children: list["FolderEntry"] = field(default_factory=list)
    parent: "FolderEntry | None" = field(default=None, repr=False, compare=False)
    loaded: bool = False
    _mtime_raw: float = field(default=0.0, repr=False, compare=False)


def build_cache(root: FolderEntry) -> dict[str, FolderEntry]:
    cache: dict[str, FolderEntry] = {}

    def _walk(node: FolderEntry) -> None:
        if node.is_dir:
            cache[node.path] = node
            for child in node.children:
                _walk(child)

    _walk(root)
    return cache


class ScanWorker(QThread):
    """Single shared pool, non-blocking callback fan-out — parallel at every
    depth instead of capping at depth 2."""

    scan_progress = pyqtSignal(object)
    scan_finished = pyqtSignal(object)
    scan_error    = pyqtSignal(str)

    current_path: str = ""

    def __init__(self, root_path: str, parent=None,
                 cache: dict[str, FolderEntry] | None = None):
        super().__init__(parent)
        self._root_path = root_path
        self._cancelled = False
        self._cache: dict[str, FolderEntry] = cache or {}
        self.current_path = ""
        self._pool: ThreadPoolExecutor | None = None

    def cancel(self) -> None:
        self._cancelled = True

    def run(self) -> None:
        try:
            done = threading.Event()
            result: dict[str, FolderEntry] = {}
            self._pool = ThreadPoolExecutor(max_workers=_MAX_WORKERS)
            try:
                def on_root_done(root: FolderEntry) -> None:
                    result["root"] = root
                    done.set()

                self._pool.submit(self._scan_root_task, self._root_path, on_root_done)
                done.wait()
            finally:
                self._pool.shutdown(wait=False)

            if not self._cancelled:
                self.scan_finished.emit(result["root"])
        except Exception as exc:
            self.scan_error.emit(str(exc))

    def _scan_root_task(self, path: str, on_done) -> None:
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
            on_done(node)
            return

        dirs  = [e for e in raw if _is_real_dir(e)]
        files = [e for e in raw if not e.is_symlink() and e.is_file(follow_symlinks=False)]

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
                node.children.append(fe)
            except (PermissionError, OSError):
                pass

        file_children = list(node.children)

        if not dirs or self._cancelled:
            node.loaded = True
            on_done(node)
            return

        pending = len(dirs)
        lock = threading.Lock()
        dir_children: list[FolderEntry] = []

        def child_done(child: FolderEntry) -> None:
            nonlocal pending
            with lock:
                node.size       += child.size
                node.file_count += child.file_count
                dir_children.append(child)
                pending -= 1
                finished = pending == 0
            self.scan_progress.emit(child)
            if finished:
                dir_children.sort(key=lambda c: c.size, reverse=True)
                node.children = dir_children + file_children
                node.loaded = True
                on_done(node)

        for e in dirs:
            self._scan_node_async(e.path, node, child_done)

    def _scan_node_async(self, path: str, parent: FolderEntry | None, on_done) -> None:
        if self._cancelled:
            on_done(FolderEntry(name=os.path.basename(path), path=path, parent=parent))
            return
        self._pool.submit(self._scan_node_task, path, parent, on_done)

    def _scan_node_task(self, path: str, parent: FolderEntry | None, on_done) -> None:
        if self._cancelled:
            on_done(FolderEntry(name=os.path.basename(path), path=path, parent=parent))
            return

        self.current_path = path

        try:
            mtime_ts = os.stat(path).st_mtime
        except OSError:
            node = FolderEntry(name=os.path.basename(path) or path, path=path, parent=parent)
            node.is_accessible = False
            on_done(node)
            return

        cached = self._cache.get(path)
        if cached is not None and abs(cached._mtime_raw - mtime_ts) < 0.001:
            self._from_cache_async(cached, parent, mtime_ts, on_done)
        else:
            self._full_scan_async(path, parent, mtime_ts, on_done)

    def _from_cache_async(self, cached: FolderEntry, parent: FolderEntry | None,
                          mtime_ts: float, on_done) -> None:
        node = FolderEntry(
            name=cached.name, path=cached.path,
            modified=datetime.fromtimestamp(mtime_ts), parent=parent,
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
        file_children.sort(key=lambda c: c.size, reverse=True)

        cached_dirs = [c for c in cached.children if c.is_dir]

        if not cached_dirs or self._cancelled:
            node.children = file_children
            node.loaded = True
            on_done(node)
            return

        self._fan_out([c.path for c in cached_dirs], node, file_children, on_done)

    def _full_scan_async(self, path: str, parent: FolderEntry | None,
                         mtime_ts: float, on_done) -> None:
        node = FolderEntry(
            name=os.path.basename(path) or path, path=path,
            modified=datetime.fromtimestamp(mtime_ts), parent=parent,
        )
        node._mtime_raw = mtime_ts

        try:
            raw = list(os.scandir(path))
        except (PermissionError, OSError):
            node.is_accessible = False
            node.loaded = True
            on_done(node)
            return

        dirs  = [e for e in raw if _is_real_dir(e)]
        files = [e for e in raw if not e.is_symlink() and e.is_file(follow_symlinks=False)]

        file_children: list[FolderEntry] = []
        for entry in files:
            if self._cancelled:
                break
            try:
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
        file_children.sort(key=lambda c: c.size, reverse=True)

        if not dirs or self._cancelled:
            node.children = file_children
            node.loaded = True
            on_done(node)
            return

        self._fan_out([e.path for e in dirs], node, file_children, on_done)

    def _fan_out(self, dir_paths: list[str], node: FolderEntry,
                file_children: list[FolderEntry], on_done) -> None:
        pending = len(dir_paths)
        lock = threading.Lock()
        dir_children: list[FolderEntry] = []

        def child_done(child: FolderEntry) -> None:
            nonlocal pending
            with lock:
                node.size       += child.size
                node.file_count += child.file_count
                dir_children.append(child)
                pending -= 1
                finished = pending == 0
            if finished:
                dir_children.sort(key=lambda c: c.size, reverse=True)
                node.children = dir_children + file_children
                node.loaded = True
                on_done(node)

        for p in dir_paths:
            self._scan_node_async(p, node, child_done)

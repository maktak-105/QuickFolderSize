from __future__ import annotations
import os
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass, field
from datetime import datetime

from PyQt6.QtCore import QThread, pyqtSignal

# Parallelize at depth 0 (root, _scan_root), 1, 2 only.
# Each level uses an independent local pool — no shared pool, no deadlock.
# Depth 3+ falls back to serial recursion to avoid thread explosion.
_ROOT_WORKERS = 16
_DEPTH_WORKERS = {1: 8, 2: 4}   # workers for depth 1 and 2 in _full_scan/_from_cache


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

    Signals
    -------
    scan_progress(FolderEntry)
        Emitted each time one top-level child completes. Enables incremental
        UI updates so the tree fills in as dirs finish rather than all at once.
    scan_finished(FolderEntry)
        Emitted once when the entire scan is done (full root).
    scan_error(str)
        Emitted on unhandled exception.

    Parallelism
    -----------
    Uses independent local ThreadPoolExecutors per level to avoid deadlock:
      depth 0  – _scan_root        – _ROOT_WORKERS (16)
      depth 1  – _full_scan/cache  – 8 workers (local pool per dir)
      depth 2  – _full_scan/cache  – 4 workers (local pool per dir)
      depth 3+ – serial recursion
    """

    scan_progress = pyqtSignal(object)  # FolderEntry for one top-level child
    scan_finished = pyqtSignal(object)  # root FolderEntry
    scan_error    = pyqtSignal(str)

    # Main thread polls this attribute via QTimer instead of using cross-thread
    # signals.  Signal emissions from parallel threads caused Qt event-queue
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

        dir_children: list[FolderEntry] = []
        workers = min(_ROOT_WORKERS, max(1, len(dirs)))
        with ThreadPoolExecutor(max_workers=workers) as pool:
            futures = {pool.submit(self._scan_dir, e.path, node, 1): e for e in dirs}
            for future in as_completed(futures):
                if self._cancelled:
                    break
                try:
                    child = future.result()
                    node.size       += child.size
                    node.file_count += child.file_count
                    dir_children.append(child)
                    self.scan_progress.emit(child)   # incremental UI update
                except Exception:
                    pass

        dir_children.sort(key=lambda c: c.size, reverse=True)
        node.children = dir_children + [c for c in node.children if not c.is_dir]
        node.loaded = True
        return node

    # ── per-directory: cache check → reuse or full scan ───────────────────

    def _scan_dir(self, path: str, parent: FolderEntry | None,
                  depth: int) -> FolderEntry:
        if self._cancelled:
            return FolderEntry(name=os.path.basename(path), path=path)

        self.current_path = path

        try:
            mtime_ts = os.stat(path).st_mtime
        except OSError:
            node = FolderEntry(name=os.path.basename(path) or path, path=path, parent=parent)
            node.is_accessible = False
            return node

        cached = self._cache.get(path)
        if cached is not None and abs(cached._mtime_raw - mtime_ts) < 0.001:
            return self._from_cache(cached, parent, mtime_ts, depth)

        return self._full_scan(path, parent, mtime_ts, depth)

    def _from_cache(self, cached: FolderEntry, parent: FolderEntry | None,
                    mtime_ts: float, depth: int) -> FolderEntry:
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

        cached_dirs = [c for c in cached.children if c.is_dir and not self._cancelled]
        dir_children = self._scan_children(
            [c.path for c in cached_dirs], node, depth
        )

        dir_children.sort(key=lambda c: c.size, reverse=True)
        file_children.sort(key=lambda c: c.size, reverse=True)
        node.children = dir_children + file_children
        node.loaded = True
        return node

    def _full_scan(self, path: str, parent: FolderEntry | None,
                   mtime_ts: float, depth: int) -> FolderEntry:
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

        dirs  = [e for e in raw if not e.is_symlink() and e.is_dir(follow_symlinks=False)]
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

        dir_children = self._scan_children(
            [e.path for e in dirs], node, depth
        )

        dir_children.sort(key=lambda c: c.size, reverse=True)
        file_children.sort(key=lambda c: c.size, reverse=True)
        node.children = dir_children + file_children
        node.loaded = True
        return node

    # ── core dispatcher: parallel (local pool) or serial based on depth ───

    def _scan_children(self, paths: list[str], parent: FolderEntry,
                       depth: int) -> list[FolderEntry]:
        """Scan child directories, in parallel or serially depending on depth.

        Uses an independent local ThreadPoolExecutor so no deadlock can occur
        between levels. Children at depth >= 3 recurse serially.
        """
        children: list[FolderEntry] = []
        if not paths or self._cancelled:
            return children

        workers = _DEPTH_WORKERS.get(depth, 0)

        if workers > 0:
            w = min(workers, len(paths))
            with ThreadPoolExecutor(max_workers=w) as pool:
                futures = {
                    pool.submit(self._scan_dir, p, parent, depth + 1): p
                    for p in paths
                }
                for future in as_completed(futures):
                    if self._cancelled:
                        break
                    try:
                        child = future.result()
                        parent.size       += child.size
                        parent.file_count += child.file_count
                        children.append(child)
                    except Exception:
                        pass
        else:
            for p in paths:
                if self._cancelled:
                    break
                child = self._scan_dir(p, parent, depth + 1)
                parent.size       += child.size
                parent.file_count += child.file_count
                children.append(child)

        return children

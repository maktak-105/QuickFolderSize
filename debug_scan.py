"""
スキャン動作デバッグスクリプト
- スレッド数・アクティブスレッド数を監視
- トップレベルディレクトリごとのスキャン時間を計測
- デッドロック検出（30秒以上進捗なし）
"""
import sys
import os
import time
import threading
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass, field
from datetime import datetime

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "python"))
from scanner import FolderEntry, build_cache

# ── 計測用ラッパー ──────────────────────────────────────────────────────────

_lock = threading.Lock()
_dir_times: dict[str, float] = {}   # path → elapsed
_visit_count = 0
_last_progress = time.monotonic()

def _fmt(n: int) -> str:
    if n >= 1_000_000_000: return f"{n/1_000_000_000:.1f} GB"
    if n >= 1_000_000:     return f"{n/1_000_000:.1f} MB"
    if n >= 1_000:         return f"{n/1_000:.1f} KB"
    return f"{n} B"

# ── 計測付きスキャン ─────────────────────────────────────────────────────────

def scan_dir_timed(path: str, parent, depth: int, parallel: bool) -> FolderEntry:
    global _visit_count, _last_progress

    node = FolderEntry(name=os.path.basename(path) or path, path=path, parent=parent)

    try:
        st = os.stat(path)
        node._mtime_raw = st.st_mtime
        node.modified = datetime.fromtimestamp(st.st_mtime)
    except OSError:
        node.is_accessible = False
        node.loaded = True
        return node

    try:
        raw = list(os.scandir(path))
    except (PermissionError, OSError):
        node.is_accessible = False
        node.loaded = True
        return node

    with _lock:
        _visit_count += 1
        _last_progress = time.monotonic()

    dirs  = [e for e in raw if not e.is_symlink() and e.is_dir(follow_symlinks=False)]
    files = [e for e in raw if not e.is_symlink() and e.is_file(follow_symlinks=False)]

    for e in files:
        try:
            st = e.stat(follow_symlinks=False)
            node.size       += st.st_size
            node.file_count += 1
        except (PermissionError, OSError):
            pass

    children: list[FolderEntry] = []

    if parallel and dirs:
        workers = min(8, len(dirs))
        with ThreadPoolExecutor(max_workers=workers) as pool:
            futures = {pool.submit(scan_dir_timed, e.path, node, depth+1, False): e
                       for e in dirs}
            for f in as_completed(futures):
                try:
                    child = f.result()
                    node.size       += child.size
                    node.file_count += child.file_count
                    children.append(child)
                except Exception as ex:
                    print(f"  [ERR] {ex}")
    else:
        for e in dirs:
            child = scan_dir_timed(e.path, node, depth+1, False)
            node.size       += child.size
            node.file_count += child.file_count
            children.append(child)

    node.children = children
    node.loaded = True
    return node


def scan_root_timed(root_path: str) -> FolderEntry:
    node = FolderEntry(name=root_path, path=root_path)

    try:
        raw = list(os.scandir(root_path))
    except (PermissionError, OSError):
        return node

    dirs  = [e for e in raw if not e.is_symlink() and e.is_dir(follow_symlinks=False)]
    files = [e for e in raw if not e.is_symlink() and e.is_file(follow_symlinks=False)]

    for e in files:
        try:
            st = e.stat(follow_symlinks=False)
            node.size       += st.st_size
            node.file_count += 1
        except (PermissionError, OSError):
            pass

    results: list[tuple[str, float, FolderEntry]] = []  # (path, elapsed, entry)

    def _scan_one(e):
        t0 = time.monotonic()
        child = scan_dir_timed(e.path, node, depth=1, parallel=True)
        elapsed = time.monotonic() - t0
        return e.path, elapsed, child

    workers = min(16, max(1, len(dirs)))
    with ThreadPoolExecutor(max_workers=workers) as pool:
        futures = {pool.submit(_scan_one, e): e for e in dirs}
        for f in as_completed(futures):
            try:
                path, elapsed, child = f.result()
                node.size       += child.size
                node.file_count += child.file_count
                node.children.append(child)
                results.append((path, elapsed, child))
            except Exception as ex:
                print(f"[ERR] {ex}")

    # トップレベル結果を時間降順で表示
    results.sort(key=lambda x: x[1], reverse=True)
    print(f"\n{'─'*60}")
    print(f"{'フォルダ':<40} {'時間':>8}  {'サイズ':>10}  {'ファイル数':>10}")
    print(f"{'─'*60}")
    for path, elapsed, child in results:
        name = os.path.basename(path) or path
        print(f"{name:<40} {elapsed:>7.1f}s  {_fmt(child.size):>10}  {child.file_count:>10,}")
    print(f"{'─'*60}")

    node.loaded = True
    return node


# ── スレッド監視 ─────────────────────────────────────────────────────────────

def monitor(interval: float = 5.0):
    while not _stop_event.is_set():
        time.sleep(interval)
        elapsed_since = time.monotonic() - _last_progress
        tc = threading.active_count()
        with _lock:
            vc = _visit_count
        print(f"  [monitor] スレッド数={tc:3d}  訪問ディレクトリ={vc:,}  "
              f"最終進捗から {elapsed_since:.1f}s")
        if elapsed_since > 60:
            print("  [monitor] !! 60秒以上進捗なし — デッドロックの可能性 !!")


# ── main ─────────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    target = sys.argv[1] if len(sys.argv) > 1 else "C:\\"
    print(f"スキャン対象: {target}")
    print(f"監視間隔: 5秒ごとにスレッド数・進捗を表示")
    print("=" * 60)

    _stop_event = threading.Event()
    mon = threading.Thread(target=monitor, args=(5.0,), daemon=True)
    mon.start()

    t0 = time.monotonic()
    root = scan_root_timed(target)
    total = time.monotonic() - t0

    _stop_event.set()

    print(f"\n合計時間  : {total:.2f}s")
    print(f"合計サイズ: {_fmt(root.size)}")
    print(f"ファイル数: {root.file_count:,}")
    print(f"訪問ディレクトリ数: {_visit_count:,}")

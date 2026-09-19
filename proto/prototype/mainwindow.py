from __future__ import annotations
import os
import shutil
from datetime import datetime

from PyQt6.QtCore import Qt, QTimer
from PyQt6.QtGui import QAction, QKeySequence
from PyQt6.QtWidgets import (
    QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QLineEdit, QPushButton, QTreeView, QStatusBar,
    QFileDialog, QLabel, QSplitter, QHeaderView,
    QMessageBox,
)

from scanner import FolderEntry, ScanWorker, build_cache
from model import FolderModel, SortProxyModel, COL_BAR, COL_SIZE, COLUMN_COUNT, ROLE_SORT
from delegate import SizeBarDelegate
from navmodel import NavModel
from utils import format_size, generate_md_report
import i18n
from i18n import tr


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.resize(1200, 720)

        self._worker: ScanWorker | None = None
        self._current_root: FolderEntry | None = None
        self._scan_time: datetime | None = None
        self._scan_cache: dict[str, FolderEntry] = {}
        self._scan_cache_norm: dict[str, FolderEntry] = {}   # normcase(path) -> entry, O(1) lookup
        self._scanned_root_path: str = ""   # 最後にスキャンしたルートパス

        self._spinner_chars = "⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏"
        self._spinner_idx = 0
        self._spinner_timer = QTimer(self)
        self._spinner_timer.timeout.connect(self._tick_spinner)

        # Polls worker.current_path every 150 ms — replaces cross-thread signals
        # that caused Qt event-queue lock contention and tripled scan time.
        self._poll_timer = QTimer(self)
        self._poll_timer.setInterval(150)
        self._poll_timer.timeout.connect(self._poll_scan_path)

        # ── status / scan-time display state (re-rendered on language toggle) ──
        self._status_kind = "idle"
        self._status_data: dict = {}
        self._scan_time_kind = "idle"   # idle | measuring | done
        self._scan_time_elapsed: float = 0.0

        self._build_ui()
        self._build_menu()
        self._retranslate_ui()

    # ── UI construction ───────────────────────────────────────────────────

    def _build_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        root_layout = QVBoxLayout(central)
        root_layout.setContentsMargins(6, 6, 6, 4)
        root_layout.setSpacing(4)

        # ── Address bar (full width, above the splitter) ──────────────────
        addr_row = QHBoxLayout()
        self._lbl_drive = QLabel("—")
        self._lbl_drive.setMinimumWidth(200)
        self._addr_bar = QLineEdit()
        self._addr_bar.returnPressed.connect(self._on_scan)
        self._btn_browse = QPushButton()
        self._btn_browse.clicked.connect(self._on_browse)
        self._btn_scan = QPushButton()
        self._btn_scan.clicked.connect(self._on_scan)
        self._btn_scan.setDefault(True)
        self._lbl_scan_time = QLabel("—")
        self._lbl_scan_time.setMinimumWidth(64)
        self._lbl_scan_time.setAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
        self._btn_lang = QPushButton()
        self._btn_lang.setFixedWidth(64)
        self._btn_lang.setToolTip("English / 日本語")
        self._btn_lang.clicked.connect(self._on_toggle_lang)
        addr_row.addWidget(self._lbl_drive)
        addr_row.addWidget(self._addr_bar, stretch=1)
        addr_row.addWidget(self._btn_browse)
        addr_row.addWidget(self._btn_scan)
        addr_row.addWidget(self._lbl_scan_time)
        addr_row.addWidget(self._btn_lang)
        root_layout.addLayout(addr_row)

        # ── Horizontal splitter: [nav tree] | [scan tree] ────────────────
        splitter = QSplitter(Qt.Orientation.Horizontal)

        # Left: drive / folder navigation tree
        self._nav_tree = QTreeView()
        self._nav_model = NavModel()
        self._nav_tree.setModel(self._nav_model)
        self._nav_tree.setHeaderHidden(True)
        self._nav_tree.setMinimumWidth(160)
        self._nav_tree.setMaximumWidth(340)
        self._nav_tree.setAnimated(True)
        self._nav_tree.setIndentation(16)
        self._nav_tree.clicked.connect(self._on_nav_clicked)
        splitter.addWidget(self._nav_tree)

        # Right: main scan / result tree
        self._tree = QTreeView()
        self._tree.setAlternatingRowColors(True)
        self._tree.setSortingEnabled(True)
        self._tree.setUniformRowHeights(True)
        self._tree.setAnimated(False)
        self._tree.setIndentation(18)
        self._tree.header().setSectionsMovable(True)

        dummy_root = FolderEntry(name="", path="")
        self._source_model = FolderModel(dummy_root)
        self._proxy = SortProxyModel()
        self._proxy.setSourceModel(self._source_model)
        self._proxy.setSortRole(ROLE_SORT)
        self._tree.setModel(self._proxy)
        self._tree.setItemDelegateForColumn(COL_BAR, SizeBarDelegate(self._tree))
        splitter.addWidget(self._tree)

        splitter.setSizes([220, 980])
        splitter.setStretchFactor(0, 0)
        splitter.setStretchFactor(1, 1)
        root_layout.addWidget(splitter, stretch=1)

        # 選択変更シグナルを接続
        self._tree.selectionModel().selectionChanged.connect(self._on_tree_selection_changed)

        # ── Status bar ────────────────────────────────────────────────────
        self._status = QStatusBar()
        self.setStatusBar(self._status)
        self._lbl_status = QLabel()
        self._status.addWidget(self._lbl_status, 1)

    def _build_menu(self):
        menubar = self.menuBar()

        # ── File menu ─────────────────────────────────────────────────────
        self._menu_file = menubar.addMenu("")

        self._act_open = QAction(self)
        self._act_open.setShortcut(QKeySequence("Ctrl+O"))
        self._act_open.triggered.connect(self._on_browse)
        self._menu_file.addAction(self._act_open)

        self._act_rescan = QAction(self)
        self._act_rescan.setShortcut(QKeySequence("F5"))
        self._act_rescan.triggered.connect(self._on_scan)
        self._menu_file.addAction(self._act_rescan)

        self._menu_file.addSeparator()

        self._act_report = QAction(self)
        self._act_report.setShortcut(QKeySequence("Ctrl+Shift+S"))
        self._act_report.setEnabled(False)
        self._act_report.triggered.connect(self._on_export_report)
        self._menu_file.addAction(self._act_report)

        self._menu_file.addSeparator()

        self._act_quit = QAction(self)
        self._act_quit.setShortcut(QKeySequence("Ctrl+Q"))
        self._act_quit.triggered.connect(self.close)
        self._menu_file.addAction(self._act_quit)

        # ── Help menu ─────────────────────────────────────────────────────
        self._menu_help = menubar.addMenu("")

        self._act_about = QAction(self)
        self._act_about.triggered.connect(self._on_about)
        self._menu_help.addAction(self._act_about)

    # ── i18n ──────────────────────────────────────────────────────────────

    def _on_toggle_lang(self):
        i18n.toggle_lang()
        self._retranslate_ui()

    def _retranslate_ui(self):
        self.setWindowTitle(tr("window_title"))
        self._addr_bar.setPlaceholderText(tr("addr_placeholder"))
        self._btn_browse.setText(tr("btn_browse"))
        self._btn_scan.setText(tr("btn_scan"))
        self._btn_lang.setText(i18n.other_lang_label())

        self._menu_file.setTitle(tr("menu_file"))
        self._act_open.setText(tr("act_open"))
        self._act_rescan.setText(tr("act_rescan"))
        self._act_report.setText(tr("act_report"))
        self._act_quit.setText(tr("act_quit"))
        self._menu_help.setTitle(tr("menu_help"))
        self._act_about.setText(tr("act_about"))

        self._source_model.headerDataChanged.emit(Qt.Orientation.Horizontal, 0, COLUMN_COUNT - 1)
        self._render_status()
        self._render_scan_time()

    # ── slots ─────────────────────────────────────────────────────────────

    def _on_about(self):
        QMessageBox.about(self, tr("about_title"), tr("about_html", version=i18n.APP_VERSION))

    def _on_tree_selection_changed(self, selected, deselected):
        indexes = selected.indexes()
        if not indexes:
            return
        # 最初の列のインデックスを使用
        index = indexes[0]
        source_index = self._proxy.mapToSource(index)
        if source_index.isValid():
            node = source_index.internalPointer()
            if node and node.path:
                self._addr_bar.setText(node.path)

    def _on_nav_clicked(self, index):
        path = self._nav_model.path_for(index)
        if not path or not os.path.isdir(path):
            return
        self._addr_bar.setText(path)
        # スキャン済みルート配下（同一または子パス）なら既存データで右ペインを更新
        if self._scanned_root_path:
            norm_path = os.path.normcase(path)
            norm_root_exact = os.path.normcase(self._scanned_root_path)
            norm_root_prefix = norm_root_exact.rstrip(os.sep) + os.sep
            if norm_path == norm_root_exact or norm_path.startswith(norm_root_prefix):
                entry = self._scan_cache_norm.get(norm_path)
                if entry is not None:
                    self._source_model.update_root(entry)
                    self._configure_columns()
                    self._proxy.sort(COL_SIZE, Qt.SortOrder.DescendingOrder)
                    self._update_drive_label(path)
                return
        self._on_scan()

    def _on_browse(self):
        path = QFileDialog.getExistingDirectory(
            self, tr("dlg_browse_title"), self._addr_bar.text() or "C:\\"
        )
        if path:
            self._addr_bar.setText(path)
            self._on_scan()

    def _on_scan(self):
        path = self._addr_bar.text().strip()
        if not path:
            return
        if not os.path.isdir(path):
            QMessageBox.warning(self, tr("err_title"), tr("err_folder_not_found", path=path))
            return

        if self._worker and self._worker.isRunning():
            self._worker.cancel()
            self._worker.wait(2000)

        self._act_report.setEnabled(False)
        self._update_drive_label(path)
        self._set_scan_time("measuring")
        self._source_model.update_root(self._make_placeholder(path))
        self._configure_columns()
        self._set_scanning(True, path)
        self._scan_time = datetime.now()
        self._scanned_root_path = path
        self._worker = ScanWorker(path, self, cache=self._scan_cache)
        self._worker.scan_progress.connect(self._on_scan_progress)
        self._worker.scan_finished.connect(self._on_scan_finished)
        self._worker.scan_error.connect(self._on_scan_error)
        self._worker.start()

    def _poll_scan_path(self) -> None:
        if self._worker and self._worker.isRunning():
            path = self._worker.current_path
            if path:
                short = path if len(path) <= 60 else "…" + path[-57:]
                self._set_status("scanning", path=short)

    def _on_scan_progress(self, child: FolderEntry) -> None:
        """Called as each top-level directory finishes — updates tree incrementally."""
        self._source_model.replace_root_child(child)
        self._proxy.invalidate()   # re-sort so finished dirs slot into correct position

    def _on_scan_finished(self, root: FolderEntry):
        elapsed = (datetime.now() - self._scan_time).total_seconds() if self._scan_time else 0.0
        self._current_root = root
        # Rebuild (not accumulate) — bounds memory to the current tree instead
        # of growing forever as the user scans one folder after another.
        self._scan_cache = build_cache(root)
        self._scan_cache_norm = {os.path.normcase(k): v for k, v in self._scan_cache.items()}
        self._set_scanning(False)
        self._source_model.update_root(root)
        self._configure_columns()
        self._proxy.sort(COL_SIZE, Qt.SortOrder.DescendingOrder)
        self._act_report.setEnabled(True)
        self._set_scan_time("done", elapsed)

        dir_count  = sum(1 for c in root.children if c.is_dir)
        file_count = sum(1 for c in root.children if not c.is_dir)
        self._set_status(
            "result",
            size=format_size(root.size),
            dirs=dir_count,
            files=root.file_count,
            topfiles=file_count,
        )

    def _on_scan_error(self, msg: str):
        self._set_scanning(False)
        self._set_status("error", msg=msg)

    def _on_export_report(self):
        if not self._current_root:
            QMessageBox.information(self, tr("report_title"), tr("report_need_scan"))
            return

        default_name = f"report_{datetime.now():%Y%m%d_%H%M%S}.md"
        save_path, _ = QFileDialog.getSaveFileName(
            self, tr("report_save_title"), default_name, tr("report_filter")
        )
        if not save_path:
            return

        try:
            content = generate_md_report(self._current_root, self._scan_time)
            with open(save_path, "w", encoding="utf-8") as f:
                f.write(content)
            QMessageBox.information(
                self, tr("report_done_title"), tr("report_done_text", path=save_path)
            )
        except OSError as e:
            QMessageBox.critical(self, tr("report_save_err_title"), tr("report_save_err_text", err=e))

    def _tick_spinner(self):
        self._spinner_idx = (self._spinner_idx + 1) % len(self._spinner_chars)

    # ── helpers ───────────────────────────────────────────────────────────

    def _set_scanning(self, scanning: bool, path: str = ""):
        self._btn_scan.setEnabled(not scanning)
        self._btn_browse.setEnabled(not scanning)
        if scanning:
            self._spinner_timer.start(100)
            self._poll_timer.start()
            self._set_status("start", path=path)
        else:
            self._spinner_timer.stop()
            self._poll_timer.stop()

    def _set_status(self, kind: str, **data) -> None:
        """Stores the status kind/params and renders it, so a language toggle can re-render it."""
        self._status_kind = kind
        self._status_data = data
        self._render_status()

    def _render_status(self) -> None:
        if self._status_kind == "scanning":
            spinner = self._spinner_chars[self._spinner_idx]
            self._lbl_status.setText(f"{spinner}  {tr('status_scanning', **self._status_data)}")
            return
        key = {
            "idle":   "status_idle",
            "start":  "status_scan_start",
            "result": "status_result",
            "error":  "status_error",
        }[self._status_kind]
        self._lbl_status.setText(tr(key, **self._status_data))

    def _set_scan_time(self, kind: str, elapsed: float = 0.0) -> None:
        self._scan_time_kind = kind
        self._scan_time_elapsed = elapsed
        self._render_scan_time()

    def _render_scan_time(self) -> None:
        if self._scan_time_kind == "measuring":
            self._lbl_scan_time.setText(tr("scan_time_measuring"))
        elif self._scan_time_kind == "done":
            self._lbl_scan_time.setText(f"{self._scan_time_elapsed:.2f}s")
        else:
            self._lbl_scan_time.setText("—")

    def _update_drive_label(self, path: str) -> None:
        try:
            drive = os.path.splitdrive(path)[0] or path[:1]
            drive_root = drive + "\\"
            usage = shutil.disk_usage(drive_root)
            used  = format_size(usage.used)
            total = format_size(usage.total)
            self._lbl_drive.setText(f"{drive_root}  {used} / {total}")
        except OSError:
            self._lbl_drive.setText("—")

    def _make_placeholder(self, path: str) -> FolderEntry:
        """Quick single-level scandir to show folder names before scan completes."""
        root = FolderEntry(name=os.path.basename(path) or path, path=path)
        try:
            with os.scandir(path) as it:
                for entry in it:
                    try:
                        if entry.is_symlink():
                            continue
                        child = FolderEntry(
                            name=entry.name,
                            path=entry.path,
                            is_dir=entry.is_dir(follow_symlinks=False),
                            parent=root,
                        )
                        root.children.append(child)
                    except OSError:
                        pass
        except (PermissionError, OSError):
            root.is_accessible = False
        root.children.sort(key=lambda c: (not c.is_dir, c.name.lower()))
        return root

    def _configure_columns(self):
        self._tree.header().resizeSection(0, 280)   # 名前
        self._tree.header().resizeSection(1, 100)   # サイズ
        self._tree.header().resizeSection(2, 160)   # 割合バー
        self._tree.header().resizeSection(3, 90)    # ファイル数
        self._tree.header().resizeSection(4, 140)   # 更新日時

    def closeEvent(self, event):
        if self._worker and self._worker.isRunning():
            self._worker.cancel()
            self._worker.wait(3000)
        super().closeEvent(event)

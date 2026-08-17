# 開発環境

## 実行環境

| 項目 | 内容 |
|:-----|:-----|
| OS | Windows 10 / 11 (64bit) |
| Python | 3.13.x |
| PyQt6 | 6.11.0 |
| PyInstaller | 6.20.0 |

## セットアップ

```powershell
pip install -r requirements.txt
```

## Python 版の起動

```powershell
cd folder_viewer/python
python main.py
```

## EXE ビルド

```powershell
cd folder_viewer
build.bat
# → dist\assets\folder_viewer.exe（dist\assets\ 直下に DLL と共に展開）
```

### ビルド成果物

```
dist/
├── assets/              # build.bat が生成（Git管理外、フォルダ構造のみ管理）
│   ├── folder_viewer.exe   # メイン実行ファイル
│   └── _internal/          # PyQt6 DLL 群（EXE と同じフォルダに置く）
└── documents/           # 配布用ドキュメント（Git管理）
    ├── readme.txt
    └── history.txt
```

> `_internal/` は `folder_viewer.exe` と常に同じディレクトリに置く必要がある。
> 配布時は `dist\assets\` と `dist\documents\` をまとめて渡す。

## 依存パッケージ

| パッケージ | バージョン | 用途 |
|:-----------|:-----------|:-----|
| PyQt6 | >=6.4.0 | GUI フレームワーク |
| PyInstaller | >=6.0.0 | EXE パッケージング |

stdlib のみ使用（`os`, `shutil`, `concurrent.futures`, `ctypes`, `string`, `dataclasses`, `datetime`）。

## ファイル構成

```
folder_viewer/
├── python/
│   ├── main.py        エントリーポイント。QApplication 起動・Fusion テーマ適用
│   ├── scanner.py     FolderEntry / build_cache / ScanWorker (QThread)
│   ├── model.py       FolderModel (QAbstractItemModel) + SortProxyModel
│   ├── delegate.py    SizeBarDelegate — COL_BAR 列にプログレスバー描画
│   ├── navmodel.py    NavModel (QAbstractItemModel) — 左ナビペイン用ドライブ・フォルダツリー
│   ├── mainwindow.py  MainWindow (QMainWindow) — UI 全体の組み立て
│   ├── utils.py       format_size() / generate_md_report()
│   └── i18n.py        tr() / 言語切替（日本語 ⇔ English）
├── document/
│   ├── spec.md        仕様書
│   ├── environment.md 本ファイル（開発環境）
│   └── about.md        バージョン情報
├── dist/
│   ├── assets/        EXE・DLL（build.bat が生成、Git 管理外）
│   └── documents/     配布用ドキュメント（readme.txt / history.txt、Git 管理）
├── build/             PyInstaller 中間ファイル（Git 管理外）
├── requirements.txt   Python 依存パッケージ
├── build.bat          EXE ビルドスクリプト
├── folder_viewer.spec PyInstaller spec（build.bat 実行時に自動更新）
├── .gitignore
└── CLAUDE.md          実装計画・Claude 向け指示
```

## Phase 3: C++ Qt6 移植（将来）

| Python | C++ 相当 |
|:-------|:---------|
| `os.scandir()` | `FindFirstFileW / FindNextFileW` |
| `ThreadPoolExecutor` | `QThreadPool + QRunnable` |
| `mtime` キャッシュ | `FILETIME` 比較（Win32 API） |
| `shutil.disk_usage()` | `GetDiskFreeSpaceEx` |
| `PyInstaller` | `windeployqt` + NSIS |

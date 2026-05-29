# フォルダ使用容量ビューワー — 実装計画

## Phase 1: Python プロトタイプ ✅ 完了

### 実装済み機能
- **並列スキャン**: ThreadPoolExecutor(max_workers=8) で root 直下のサブディレクトリを同時スキャン
- **mtime キャッシュ**: 前回スキャン結果を保持し、変化のない dir は scandir() をスキップ
- **プレースホルダー表示**: スキャン開始時に直下1レベルを即時表示、完了後に数値埋める
- **スキャン時間表示**: アドレスバー右端に `X.XXs` で所要時間を表示
- **ドライブ容量表示**: アドレスバー左端に `C:\ 150.3 GB / 512.0 GB` と表示
- **Markdown レポート**: `ファイル > レポート作成` で階層フォルダ容量を出力
- **左ナビペイン**: ドライブ一覧とフォルダツリー（遅延展開）

### パフォーマンス
- **初回スキャン（C:\）**: 60-70 秒
- **再スキャン（キャッシュあり）**: 20 秒程度
- **最適化**: ポーリング方式で emit ロック競合を排除

---

## Phase 2: Step 4 — PyInstaller EXE パッケージング

### 実装内容
```bash
pip install pyinstaller
cd folder_viewer
pyinstaller --onefile --windowed --name folder_viewer python/main.py
# → dist/folder_viewer.exe
```

### 検証項目
- [ ] exe 単体で起動可能
- [ ] C:\ スキャンで 60-70s
- [ ] 再スキャンで 20s
- [ ] UI・キャッシュ・レポート出力が正常

### 完了後
- GitHub Releases に exe を公開するか、CLAUDE.md にビルド手順を記載

---

## Phase 3: C++ Qt6 移植（将来）

| Python | C++ |
|--------|-----|
| `os.scandir()` | `FindFirstFileW / FindNextFileW` |
| `ThreadPoolExecutor` | `QThreadPool + QRunnable` |
| `mtime` キャッシュ | `FILETIME` 比較 |
| `shutil.disk_usage()` | Win32 API（GetDiskFreeSpaceEx） |

---

## ファイル構成

```
folder_viewer/
├── python/
│   ├── main.py
│   ├── scanner.py
│   ├── model.py
│   ├── delegate.py
│   ├── navmodel.py
│   ├── mainwindow.py
│   └── utils.py
├── document/
│   └── spec.md
├── CLAUDE.md (this file)
├── cpp/ (Phase 3)
└── dist/ (PyInstaller output)
```

---

## 仕様書

→ `document/spec.md` を参照

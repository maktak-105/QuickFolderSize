# フォルダ使用容量ビューワー — 実装計画

## Phase 1: Python プロトタイプ ✅ 完了

### 実装済み機能
- **全レベル並列スキャン**: 共有 `ThreadPoolExecutor(max_workers=32)` で全階層のサブディレクトリを並列スキャン
- **mtime キャッシュ**: 前回スキャン結果を保持し、変化のない dir は scandir() をスキップ
- **プレースホルダー表示**: スキャン開始時に直下1レベルを即時表示、完了後に数値埋める
- **スキャン時間表示**: アドレスバー右端に `X.XXs` で所要時間を表示
- **ドライブ容量表示**: アドレスバー左端に `C:\ 150.3 GB / 512.0 GB` と表示
- **Markdown レポート**: `ファイル > レポート作成` で階層フォルダ容量を出力
- **左ナビペイン**: ドライブ一覧とフォルダツリー（遅延展開）

### パフォーマンス（C:\、参考値）
- **初回スキャン**: 50〜150s（OSディスクキャッシュ状態に依存）
- **再スキャン（キャッシュあり）**: 20 秒程度

---

## Phase 2: PyInstaller EXE パッケージング ✅ 完了

### ビルド方法
```powershell
cd folder_viewer
build.bat
# → dist\folder_viewer.exe（dist\ 直下に DLL と共に展開）
```

### 成果物
- `dist\folder_viewer.exe` — メイン実行ファイル
- `dist\_internal\` — PyQt6 DLL 群

---

## Phase 3: C++ Qt6 移植（将来）

`document/environment.md` の Phase 3 欄を参照。

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
│   ├── spec.md          仕様書
│   └── environment.md   開発環境・ビルド手順
├── dist/                EXE デプロイ先
├── build/               PyInstaller 中間ファイル（Git 管理外）
├── requirements.txt
├── build.bat
├── folder_viewer.spec
├── .gitignore
└── CLAUDE.md            本ファイル
```

---

## 参照ドキュメント

- 仕様書 → `document/spec.md`
- 開発環境・ビルド手順 → `document/environment.md`

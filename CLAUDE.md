# QuickFolderSize — 実装計画

## Phase 1: Python プロトタイプ ✅ 完了

### 実装済み機能
- **全レベル並列スキャン**: 共有 `ThreadPoolExecutor(max_workers=32)` で全階層のサブディレクトリを並列スキャン
- **mtime キャッシュ**: 前回スキャン結果を保持し、変化のない dir は scandir() をスキップ
- **プレースホルダー表示**: スキャン開始時に直下1レベルを即時表示、完了後に数値埋める
- **スキャン時間表示**: アドレスバー右端に `X.XXs` で所要時間を表示
- **ドライブ容量表示**: アドレスバー左端に `C:\ 150.3 GB / 512.0 GB` と表示
- **Markdown レポート**: `ファイル > レポート作成` で階層フォルダ容量を出力
- **左ナビペイン**: ドライブ一覧とフォルダツリー（遅延展開）
- **表示言語切替**: アドレスバー右端のトグルボタンで 日本語 ⇔ English（`python/i18n.py`、UI全文言・レポート出力に反映）

現在は `python/` に参照実装として残っている(削除していない)。

---

## Phase 2: PyInstaller EXE パッケージング ✅ 完了(旧Python版)

Python/PyQt6版のEXEパッケージング。詳細は `python/` 配下の旧実装を参照。

---

## Phase 3: MinGW/C++ネイティブビルド＋WebView2化 ✅ 完了

Python/PyQt6実装をC++17(MinGW-w64)+ WebView2 GUIへ全面移植した。UIテーマ・アーキテクチャはワークスペース内の `QuickDiskBench` を踏襲している。

### 実装内容
- **スキャンエンジン(`core/native/engine.cpp`)**: Python版の「単一共有スレッドプール・非ブロッキングfan-out」設計をC++で再現。`FindFirstFileW`/`FindNextFileW`による列挙、FILETIME比較によるmtimeキャッシュ、ジャンクション/シンボリックリンク除外、ドライブ容量取得
- **WebView2ホスト(`core/native/webview_main.cpp`)**: JSON WebMessageプロトコルでJS側と通信。`IFileDialog`によるフォルダ選択・レポート保存ダイアログ
- **フロントエンド(`templates/`, `static/`)**: HTML/CSS/バニラJSでツリーテーブル・左ナビペイン・メニュー・言語切替(i18n)・Markdownレポート生成を実装。フレームワーク非依存
- **ビルド(`build_native.py`, `bundle_html.py`)**: `engine_x64.dll`(単体成果物) + `QuickFolderSize.exe`(engineを静的リンク)の2点をビルド。CSS/JSは配布用に自己完結HTMLへバンドル

### ビルド方法
```powershell
cd QuickFolderSize
build.bat
# → dist\binary\QuickFolderSize.exe（同フォルダの engine_x64.dll / WebView2Loader.dll / index.html と共に配布）
```

詳細は `document/environment.md` を参照。

バージョン: v1.0.0(据え置き)

---

## ファイル構成

```
QuickFolderSize/
├── core/native/          スキャンエンジン・WebView2ホスト(C++)
├── templates/            開発用HTMLテンプレート
├── static/                開発用CSS/JS
├── python/                Phase 1プロトタイプ(参照用に保持)
├── document/
│   ├── spec.md            仕様書
│   ├── environment.md     開発環境・ビルド手順
│   └── about.md            バージョン情報
├── dist/
│   ├── binary/             EXE・DLL・バンドル済みHTML（build.bat が生成、Git 管理外）
│   └── documents/          配布用 readme.txt / history.txt（Git 管理）
├── plans/                  計画書・実施結果(バージョン管理、上書き禁止)
├── build_native.py         ビルド本体
├── bundle_html.py           HTML/CSS/JSバンドラー
├── build.bat                ビルドラッパー
├── .gitignore
└── CLAUDE.md                本ファイル
```

---

## 参照ドキュメント

- 仕様書 → `document/spec.md`
- 開発環境・ビルド手順 → `document/environment.md`

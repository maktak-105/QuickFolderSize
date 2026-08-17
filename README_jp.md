# QuickFolderSize

[English README.md](README.md)

ローカルドライブ・フォルダの使用容量を視覚的に把握するための Windows デスクトップアプリです。パスをスキャンし、割合バー付きのソート可能なツリーで結果を表示し、Markdown レポートを出力できます。

バージョン: **v1.0.0**

実装: **C++17（MinGW-w64 / g++）+ WebView2**。UI はネイティブの WebView2 ウィンドウ上の HTML / CSS / バニラ JS です。配布アプリに Python や Qt のランタイムは含まれません。

## 配布版を使う

ソースをビルドしなくてよい場合は、GitHub Releases から ZIP をダウンロードしてください。

- [最新の Release](https://github.com/maktak-105/QuickFolderSize/releases)
- [v1.0.0](https://github.com/maktak-105/QuickFolderSize/releases/tag/v1.0.0)
- [QuickFolderSize-binary.zip を直接ダウンロード](https://github.com/maktak-105/QuickFolderSize/releases/download/v1.0.0/QuickFolderSize-binary.zip)

ZIP を同じフォルダに展開して `QuickFolderSize.exe` を実行します。

- `QuickFolderSize.exe` — 本体
- `engine_x64.dll` — スキャンエンジン単体 DLL
- `WebView2Loader.dll` — WebView2 ローダー
- `index.html` — バンドル済み UI
- `readme.txt` / `readme-jp.txt` — 使い方
- `LICENSE.txt` / `LICENSE_jp.txt` — MIT License

Windows 11 には WebView2 Runtime が標準搭載です。一部の Windows 10 / LTSC / Server では Evergreen Runtime の追加インストールが必要です。

今後の更新は `main` へのプルリクエストで行います。`v*` タグを push するか Release ワークフローを実行すると、GitHub Actions が ZIP をビルドして Release に載せます。

## 主な機能

- 指定フォルダ以下を再帰スキャンし、サイズ・再帰ファイル数・更新日時を集計
- バックグラウンドスキャン（UI をブロックしない）
- 全深度並列スキャン: 共有 32 ワーカープール + 非ブロッキング fan-out。深い階層でも並列度が落ちない
- ルート直下の子が終わるたびにツリーを順次更新
- スキャン開始と同時に直下 1 レベルをプレースホルダー表示
- 左下のスキャン時間カードに経過秒を表示（0.2 秒ごとに更新、`0.00s` … `12.34s`）
- アドレスバーにドライブ使用量（`C:\ 150.3 GB / 512.0 GB`）
- 左ナビ: ドライブ一覧と遅延展開フォルダ
- スキャン済みツリー内のパスは再スキャンせず即時表示切替
- NTFS ジャンクション / マウントポイント / リパースポイントは再帰から除外
- mtime キャッシュ: 変化のないディレクトリは再列挙をスキップ（`FILETIME` 比較）
- Markdown 形式のフォルダ容量レポート
- 日本語 ⇔ English（メニューバー右端）。メニュー・ヘッダー・ダイアログ・レポートが即時切替

## UI

QuickDiskBench と同じ系統のダーク・グラスモーフィズムです。

- ほぼ黒のキャンバス（`#0a0c10`）に青 / 紫 / シアンの放射グラデーション
- すりガラス風カード（`backdrop-filter` ブラー、半透明 `#10141c`）
- アクセントはシアン `#00f0ff` と青 `#3b82f6`（スキャンボタン、ホバー、サイズバー）
- アクセスできないフォルダは赤（`#ef4444`）
- フォントはシステムのみ（Segoe UI / 游ゴシック UI）。CDN なしでオフラインでも表示できる

画面構成:

```
┌─ ファイル / ヘルプ ────────────────────────────── [🌐 English] ─┐
├─ [C:\ 150.3 GB / 512.0 GB]  [パス入力]  [スキャン] ─────────────┤
├─ ドライブ / フォルダ（遅延）─┬─ 名前 | サイズ | 割合 | 数 | 日時 ─┤
│                              │  📁 Windows   40.1 GB  ████  62.3% │
│  スキャン時間        2.34s   │  📄 pagefile  16.0 GB  ██    24.9% │
└──────────────────────────────┴──────────────────────────────────┘
```

- **ファイル** — フォルダを開く（`Ctrl+O`）、再スキャン（`F5`）、レポート作成（`Ctrl+Shift+S`）、終了（`Ctrl+Q`）
- **ヘルプ** — バージョン情報
- **言語ボタン** — 日本語 ⇔ English
- **アドレスバー** — ドライブ容量、パス、スキャン（Enter でも開始）
- **左ナビ** — ドライブ一覧。展開時に子フォルダをロード
- **スキャン時間** — 実行中は経過秒、完了後は最終時間
- **結果ツリー** — 列ヘッダーでソート、▶ / ▼ で展開

## ビルド済みアプリの起動

```text
dist\binary\QuickFolderSize.exe
```

次のファイルは **同じフォルダ** に置いてください。

| ファイル | 役割 |
|----------|------|
| `QuickFolderSize.exe` | ネイティブホスト + スキャンエンジン（静的リンク） |
| `engine_x64.dll` | 単体のスキャンエンジン DLL（EXE 実行時にはロードしない） |
| `WebView2Loader.dll` | WebView2 ローダー |
| `index.html` | バンドル済み UI（CSS/JS インライン） |

**Microsoft Edge WebView2 Runtime** は Windows 11 に標準搭載です。一部の Windows 10 / LTSC / Server では、ウィンドウが出ない場合に Evergreen Runtime の追加インストールが必要です。起動に失敗したら EXE と同じ場所の `QuickFolderSize_debug.log` を見てください。

配布パッケージ向けの説明: [`dist/documents/readme-jp.txt`](dist/documents/readme-jp.txt)（日本語）、[`dist/documents/readme.txt`](dist/documents/readme.txt)（英語）。

## ソースからビルド

```powershell
winget install --id BrechtSanders.WinLibs.MCF.UCRT --exact --source winget
# WebView2 SDK のヘッダ / ローダーを C:\tools\webview2\build\native\ に配置
#   include\WebView2.h  と  x64\WebView2Loader.dll

cd QuickFolderSize
build.bat
# → dist\binary\QuickFolderSize.exe
```

`build.bat` は `python build_native.py` を呼びます。WinLibs の `g++` を探し、HTML をバンドルし、`engine_x64.dll` と GUI EXE（`-mwindows`、エンジンは静的リンク）をコンパイルし、`WebView2Loader.dll` をコピーします。

詳細は [`document/environment.md`](document/environment.md)。

## キーボードショートカット

| ショートカット | 機能 |
|----------------|------|
| `Ctrl+O` | フォルダ選択ダイアログ |
| `F5` | 再スキャン |
| `Ctrl+Shift+S` | Markdown レポート出力 |
| `Ctrl+Q` | 終了 |
| パス欄で `Enter` | スキャン |

## 動作環境

- Windows 10 / 11（64bit）
- Microsoft Edge WebView2 Runtime
- **ビルド時**: MinGW-w64 g++（WinLibs MCF/UCRT）、WebView2 SDK、Python 3（ビルドスクリプト用のみ）

サードパーティの C++ ライブラリは使いません。フロントエンドもフレームワーク非依存です。

## リポジトリ構成

```
QuickFolderSize/
├── core/native/          スキャンエンジン + WebView2 ホスト（C++）
├── templates/            開発用 HTML
├── static/css|js         開発用 CSS / JS
├── python/               Phase 1 の Python/PyQt6 プロトタイプ（参照用）
├── document/             仕様・開発環境・バージョン情報
├── dist/binary/          ビルド成果物（Git 管理外）
├── dist/documents/       配布用 readme / history
├── build_native.py       ネイティブビルド
├── bundle_html.py        CSS/JS を 1 枚の HTML にインライン化
└── build.bat
```

## ドキュメント

- 仕様書 → [document/spec.md](document/spec.md)
- 開発環境・ビルド手順 → [document/environment.md](document/environment.md)
- バージョン情報 → [document/about.md](document/about.md)
- English README → [README.md](README.md)

## コンセプト

仕事で使っている PC の SSD がかつかつで、苦し紛れに作りました。

レポートを AI エージェントに流すと、いい感じでアドバイスをくれます。

同じようなツールはありますが、使い勝手が自分には合わず、一から作ってみました。試行錯誤して条件をいくつも変えたので、そこそこ高速で動くと思います。

## 制作者

GitHub: [maktak-105](https://github.com/maktak-105)

# 開発環境

## 実行環境

| 項目 | 内容 |
|:-----|:-----|
| OS | Windows 10 / 11 (64bit) |
| C++ | C++17 |
| コンパイラ | MinGW-w64 (g++)。WinLibs (MCF threads, UCRT) `BrechtSanders.WinLibs.MCF.UCRT` 16.1.0-14.0.0-r1 で動作確認 |
| Python | 3.x（`build_native.py` / `bundle_html.py` 用。配布アプリには不要） |
| WebView2 SDK | ヘッダを `C:\tools\webview2\build\native\include` に配置（既定。`WEBVIEW2_INCLUDE` で変更可）。ローカル確認は NuGet `Microsoft.Web.WebView2` 1.0.4129.50 |
| WebView2 Runtime | 実行時に必要（Windows 11 は標準搭載。Windows 10 / LTSC / Server は別途インストールすることがある） |

## セットアップ

### MinGWツールチェイン

```powershell
winget install --id BrechtSanders.WinLibs.MCF.UCRT --exact --source winget
```

`build_native.py` は WinGet の既定インストール先を直接探す。`g++` が見つかったフォルダの `windres.exe` / `llvm-windres.exe` も同じ場所から探すので、ビルドだけなら PATH 追加は必須ではない。

`g++` / `windres` をターミナルから直接使いたい場合は、次をユーザー PATH に追加する:

```text
%LOCALAPPDATA%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.MCF.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin
```

PATH 変更後はターミナル / IDE を再起動すること（既存セッションは古い PATH を保持する）。

### WebView2 SDK

NuGet パッケージ `Microsoft.Web.WebView2` からヘッダ（`WebView2.h` 等）と `WebView2Loader.dll` を取得し、`C:\tools\webview2\build\native\` 以下に置く（`include\` と `x64\` を含む構成）。

```powershell
$nupkg = "$env:TEMP\Microsoft.Web.WebView2.nupkg"
Invoke-WebRequest "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.4129.50" -OutFile $nupkg
Copy-Item $nupkg "$env:TEMP\Microsoft.Web.WebView2.zip" -Force
Expand-Archive "$env:TEMP\Microsoft.Web.WebView2.zip" -DestinationPath C:\tools\webview2 -Force
```

配置先を変えた場合は `WEBVIEW2_INCLUDE` / `WEBVIEW2_LOADER` で明示する。

疎通確認:

```powershell
g++ --version
windres --version
Test-Path C:\tools\webview2\build\native\include\WebView2.h
```

## ビルド方法

```powershell
cd QuickFolderSize
build.bat
# → dist\binary\QuickFolderSize.exe
```

`build.bat` は `python build_native.py` を呼ぶ薄いラッパー。EXE が起動中だとリンクに失敗する（`Permission denied`）。閉じてから再実行する。

### ビルド手順の内訳（`build_native.py`）

1. `bundle_html.py` が `templates/index.html` + `static/css/style.css` + `static/js/app.js` を自己完結の `dist/binary/index.html` 1枚へバンドルする。CSS/JS はインライン化、相対パスの `<img>`（About の `static/img/author.png`）は data URI に埋め込む（`NavigateToString` は外部リソースを解決できない）
2. `windres` でアプリアイコン（`core/native/QuickFolderSize.ico`）をリソースオブジェクト化
3. `core/native/engine.cpp` を `-shared -static -std=c++17` でコンパイルし `engine_x64.dll` を生成（単体成果物。EXE 実行時にはロードしない）
4. `engine.cpp` + `webview_main.cpp` + リソースを `-mwindows -static` でコンパイルし `QuickFolderSize.exe` を生成（engine は静的リンク）
5. `WebView2Loader.dll` と開発用テンプレート一式（`templates/`、`static/css`、`static/js`）を `dist/binary/` へコピー

### ビルド成果物

```
dist/
├── binary/                    # build.bat が生成（Git 管理外）
│   ├── QuickFolderSize.exe    # メイン実行ファイル
│   ├── engine_x64.dll         # スキャンエンジン DLL（単体成果物）
│   ├── WebView2Loader.dll     # WebView2 ローダー
│   ├── index.html             # バンドル済み自己完結 HTML（exe が読む）
│   ├── templates/             # 開発用コピー（実行時は未使用）
│   └── static/                # 開発用コピー（実行時は未使用）
└── documents/                 # 配布用ドキュメント（Git 管理）
    ├── readme.txt             # 英語
    ├── readme-jp.txt          # 日本語
    ├── history.txt
    ├── history_jp.txt
    ├── LICENSE.txt            # MIT 英語原文
    └── LICENSE_jp.txt         # MIT 日本語参考訳
```

> `QuickFolderSize.exe` は `WebView2Loader.dll` と `index.html` を自分と同じフォルダから探す。この3点は常に同じディレクトリに置く。

### 配布 ZIP（フラット）

Release 用 ZIP はサブフォルダを作らず、次を同じ階層に入れる。

- `QuickFolderSize.exe`
- `engine_x64.dll`
- `WebView2Loader.dll`
- `index.html`
- `readme.txt` / `readme-jp.txt`
- `history.txt` / `history_jp.txt`
- `LICENSE.txt` / `LICENSE_jp.txt`

## GitHub Actions

| ワークフロー | 起動条件 | 内容 |
|:-------------|:---------|:-----|
| `.github/workflows/ci.yml` | `main` への push / pull_request | windows-latest で MinGW + WebView2 SDK を入れて `build_native.py` |
| `.github/workflows/release.yml` | `v*` タグ、または workflow_dispatch | 同様にビルドし、フラットな `QuickFolderSize-binary.zip` を Release に添付 |

CI / Release ランナーは Chocolatey の MinGW を使う。`WEBVIEW2_INCLUDE` はジョブ内で NuGet 展開先を指す。

今後の更新は `main` へのプルリクエスト。タグを push すると Release ZIP が同期される。

## トラブルシューティング

- 起動直後にウィンドウが出ない / エラーダイアログ: exe と同じフォルダの `QuickFolderSize_debug.log` を見る（WebView2 初期化の各ステップを記録）
- `WebView2 Runtimeを初期化できませんでした`: Microsoft Edge WebView2 Runtime (Evergreen) を入れる
- `WebView2 SDK headers not found`: `WEBVIEW2_INCLUDE` でヘッダの実パスを指定する
- `llvm-windres/windres が見つかりませんでした`: コンパイラと同じ `mingw64\bin` に `windres.exe` があるか確認する
- `cannot open output file ... QuickFolderSize.exe: Permission denied`: exe が起動中。終了してから `build.bat` を再実行する

## 依存関係

Win32 API（`FindFirstFileW` / `FindNextFileW`, `IFileDialog`, `DwmSetWindowAttribute` 等）と WebView2 SDK のみ。サードパーティの C++ ライブラリ依存なし。フロントエンドはバニラ JS。

## ファイル構成

```
QuickFolderSize/
├── core/native/
│   ├── engine.h / engine.cpp     スキャンエンジン
│   ├── webview_main.cpp          WebView2ホスト・WebMessage・ダイアログ
│   ├── QuickFolderSize.rc / .ico アイコン
├── templates/index.html          開発用 HTML
├── static/
│   ├── css/style.css
│   ├── js/app.js
│   └── img/author.png            About ダイアログ用（バンドル時に埋め込み）
├── assets/                       README 用スクリーンショット
├── python/                       Phase 1 プロトタイプ（参照用）
├── document/
│   ├── spec.md
│   ├── environment.md            本ファイル
│   └── about.md
├── dist/
│   ├── binary/                   ビルド成果物（Git 管理外）
│   └── documents/                配布 readme / history / LICENSE
├── .github/workflows/            CI と Release
├── LICENSE                       リポジトリ用 MIT（英語原文）
├── README.md / README_jp.md
├── build_native.py
├── bundle_html.py
├── build.bat
├── .gitignore
└── CLAUDE.md
```

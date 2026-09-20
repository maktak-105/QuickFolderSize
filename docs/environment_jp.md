# 開発環境

[English environment.md](environment.md)

## 実行環境

| 項目 | 内容 |
|:-----|:-----|
| OS | Windows 10 / 11 (64bit) |
| C++ | C++17 |
| コンパイラ | MinGW-w64 (g++)。WinLibs (MCF threads, UCRT) `BrechtSanders.WinLibs.MCF.UCRT` 16.1.0-14.0.0-r1 で動作確認 |
| Python | 3.x（`scripts/build.py` / `bundle_html.py` 用。配布アプリには不要） |
| WebView2 SDK | ヘッダを `C:\tools\webview2\build\native\include` に配置（既定。`WEBVIEW2_INCLUDE` で変更可）。ローカル確認は NuGet `Microsoft.Web.WebView2` 1.0.4129.50 |
| WebView2 Runtime | 実行時に必要（Windows 11 は標準搭載。Windows 10 / LTSC / Server は別途インストールすることがある） |

## セットアップ

### MinGWツールチェイン

```powershell
winget install --id BrechtSanders.WinLibs.MCF.UCRT --exact --source winget
```

`scripts/build.py` は WinGet の既定インストール先を直接探す。`g++` が見つかったフォルダの `windres.exe` / `llvm-windres.exe` も同じ場所から探すので、ビルドだけなら PATH 追加は必須ではない。

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
# → dist\QuickFolderSize.exe
```

`build.bat` は `python scripts\build.py` を呼ぶ薄いラッパー。EXE が起動中だとリンクに失敗する（`Permission denied`）。閉じてから再実行する。

### ビルド手順の内訳（`scripts/build.py`）

1. `bundle_html.py` が `src/ui/index.html` + `src/ui/css/style.css` + `src/ui/js/app.js` を自己完結の `build/intermediate/index.html` 1枚へバンドルする。CSS/JS はインライン化、相対パスの `<img>`（About の `src/ui/img/author.png`）は data URI に埋め込む（`NavigateToString` は外部リソースを解決できない）。同じ内容を `src/index_embed.html` にもコピーする(次のリソースコンパイルで参照するため)
2. `windres` で `src/QuickFolderSize.rc`(アプリアイコン・マニフェスト・`index_embed.html`のRCDATA埋め込み)、`src/QuickFolderSize_cli.rc`(アイコン・バージョン情報のみ、マニフェストなし)をそれぞれリソースオブジェクト化
3. `engine.cpp` + `webview_main.cpp` + GUI用リソースを `-mwindows -static` でコンパイルし `QuickFolderSize.exe` を生成(engineは静的リンク、バンドルHTMLはRCDATAとして埋め込み済み)
4. `engine.cpp` + `main_cli.cpp` + CLI用リソースを(`-mwindows`を付けず)`-static`でコンパイルし `QuickFolderSize_cli.exe` を生成(コンソールサブシステム、管理者マニフェストなし=既定でasInvoker)
5. `WebView2Loader.dll` と開発用の `src/ui/css`・`src/ui/js` を `dist/` へコピー

### ビルド成果物

```
dist/
├── binary/                    # build.bat が生成（Git 管理外）
│   ├── QuickFolderSize.exe    # メイン実行ファイル（バンドルHTMLをRCDATA埋め込み済み）
│   ├── QuickFolderSize_cli.exe # CLI版（管理者権限は要求しない、単体で動作）
│   ├── WebView2Loader.dll     # WebView2 ローダー（GUI版に必須）
│   ├── index.html             # バンドル済みHTMLの参考コピー（GUIはこれを読まない。配布必須ではない）
│   └── src/ui/                # 開発用コピー（実行時は未使用）
└── documents/                 # 配布用ドキュメント（Git 管理）
    ├── readme.txt             # 英語
    ├── readme_jp.txt          # 日本語
    ├── history.txt
    ├── history_jp.txt
    ├── LICENSE.txt            # MIT 英語原文
    └── LICENSE_jp.txt         # MIT 日本語参考訳
```

> `QuickFolderSize.exe` はバンドルHTMLをRCDATAとして埋め込み済みで、`WebView2Loader.dll` だけを自分と同じフォルダから探す。この2点は常に同じディレクトリに置く。`QuickFolderSize_cli.exe` はどちらにも依存せず単体で動く。

### 配布 ZIP（フラット）

Release 用 ZIP はサブフォルダを作らず、次を同じ階層に入れる。

- `QuickFolderSize.exe`
- `QuickFolderSize_cli.exe`
- `WebView2Loader.dll`
- `readme.txt` / `readme_jp.txt`
- `history.txt` / `history_jp.txt`
- `LICENSE.txt` / `LICENSE_jp.txt`

## GitHub Actions

| ワークフロー | 起動条件 | 内容 |
|:-------------|:---------|:-----|
| `.github/workflows/ci.yml` | `main` への push / pull_request | windows-latest で MinGW + WebView2 SDK を入れて `scripts/build.py` |
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

Win32 API（`FindFirstFileW` / `FindNextFileW`, `DeviceIoControl` / NTFS MFT, `IFileDialog`, `DwmSetWindowAttribute` 等）と WebView2 SDK のみ。サードパーティの C++ ライブラリ依存なし。フロントエンドはバニラ JS。

NTFS MFT 高速経路は、管理者権限で開いた `\\.\X:` ボリュームから `$MFT` のデータランを読み取る。GUI版(`QuickFolderSize.exe`)には`requireAdministrator`マニフェストを埋め込み、起動時にUAC確認を表示する。MFTレコードを安全に解釈できない場合、または対象がNTFSボリューム直下でない場合は、通常のWin32列挙にフォールバックする。

CLI版(`QuickFolderSize_cli.exe`)は管理者マニフェストを埋め込んでおらず、既定のasInvokerで起動する(UACを要求しない)。非管理者実行時は上記のフォールバックにより自動的にWin32列挙が使われ、MFT高速経路は管理者権限で実行した場合のみ有効になる。

## ファイル構成

```
QuickFolderSize/
├── src/
│   ├── app/                     GUIホスト、リソース、アプリ内ヘルプ
│   ├── cli/                     CLIエントリーポイントとリソース
│   ├── engine/                  共有スキャンエンジンとMFT処理
│   ├── ui/                      HTML、CSS、JavaScript、画像
│   └── integrations/
│       └── mcp-server/          任意のNode.js MCPサーバーソース
├── proto/                       保存済みプロトタイプとCLI検証ツール
├── scripts/                     ビルドとUIバンドル用スクリプト
├── assets/                       README 用スクリーンショット
├── docs/
│   └── distribution/             リリース用readme、履歴、ライセンス
├── build/intermediate/           ビルド中間生成物（Git管理外）
├── dist/                         ビルド成果物（Git管理外）
├── .github/workflows/            CI と Release
├── LICENSE                       リポジトリ用 MIT（英語原文）
├── README.md / README_jp.md
└── .gitignore
```

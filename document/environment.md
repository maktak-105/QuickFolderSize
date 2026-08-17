# 開発環境

## 実行環境

| 項目 | 内容 |
|:-----|:-----|
| OS | Windows 10 / 11 (64bit) |
| C++ | C++17 |
| コンパイラ | MinGW-w64 (g++)。WinLibs (MCF threads, UCRTランタイム) `BrechtSanders.WinLibs.MCF.UCRT` で動作確認 |
| WebView2 SDK | ヘッダを `C:\tools\webview2\build\native\include` に配置(既定パス。`WEBVIEW2_INCLUDE` 環境変数で変更可) |
| WebView2 Runtime | 実行時に必要(Windows 11は標準搭載、Windows 10等は別途インストール) |

## セットアップ

### MinGWツールチェイン

```powershell
winget install --id BrechtSanders.WinLibs.MCF.UCRT --exact --source winget
```

`build_native.py` はWinGetの既定インストール先を自動探索するため、PATHへの追加は必須ではない。`g++`/`windres` を直接使いたい場合は以下をユーザーPATHに追加する:

```text
%LOCALAPPDATA%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.MCF.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin
```

PATH変更後はターミナル/IDEを再起動すること(既存セッションは古いPATHを保持したまま)。ただし `build_native.py` はWinGetの既定パスを直接チェックするため、この再起動を待たずにビルド自体は可能。

### WebView2 SDK

NuGetパッケージ `Microsoft.Web.WebView2` からヘッダ(`WebView2.h` 等)と `WebView2Loader.dll` を取得し、`C:\tools\webview2\build\native\` 以下に配置する(`include\` と `x64\` を含む構成)。配置先を変えた場合は `WEBVIEW2_INCLUDE` / `WEBVIEW2_LOADER` 環境変数で明示できる。

コンパイラの疎通確認:

```powershell
g++ --version
windres --version
```

## ビルド方法

```powershell
cd QuickFolderSize
build.bat
# → dist\binary\QuickFolderSize.exe（同フォルダの engine_x64.dll / WebView2Loader.dll / index.html と一緒に配布）
```

`build.bat` は `python build_native.py` を呼び出すだけの薄いラッパー。`build_native.py` が実際のビルド手順(コンパイラ探索・HTMLバンドル・DLL/EXEコンパイル・成果物コピー)を担う。

### ビルド手順の内訳(`build_native.py`)

1. `bundle_html.py` が `templates/index.html` + `static/css/style.css` + `static/js/app.js` を、CSS/JSをインライン化した自己完結の `dist/binary/index.html` 1枚へバンドルする(WebView2の`NavigateToString`は相対パスの外部リソースを解決できないため)
2. `windres` でアプリアイコン(`core/native/QuickFolderSize.ico`)をリソースオブジェクト化
3. `core/native/engine.cpp` を `-shared` でコンパイルし `engine_x64.dll` を生成(単体でも再利用可能なDLL成果物として)
4. `engine.cpp` + `webview_main.cpp` + リソースオブジェクトを `-mwindows` でコンパイルし `QuickFolderSize.exe` を生成(engineは静的リンク、DLLは実行時にロードしない)
5. `WebView2Loader.dll` と開発用テンプレート一式(`templates/`, `static/`)を `dist/binary/` へコピー

### ビルド成果物

```
dist/
├── binary/                 # build.bat が生成(Git管理外、フォルダ構造のみ管理)
│   ├── QuickFolderSize.exe    # メイン実行ファイル
│   ├── engine_x64.dll         # スキャンエンジンDLL(単体成果物、exe自体は静的リンク版を使用)
│   ├── WebView2Loader.dll     # WebView2ローダー
│   ├── index.html             # バンドル済み自己完結HTML(実際にexeが読み込むファイル)
│   ├── templates/             # 開発用テンプレート(参考。実行時は未使用)
│   └── static/                # 開発用CSS/JS(参考。実行時は未使用)
└── documents/               # 配布用ドキュメント(Git管理)
    ├── readme.txt
    └── history.txt
```

> `QuickFolderSize.exe` は `WebView2Loader.dll` と `index.html` を自分と同じフォルダから探すため、この3点は常に同じディレクトリに置く必要がある。

## トラブルシューティング

- 起動直後にウィンドウが表示されない/エラーダイアログが出る場合、exeと同じフォルダに生成される `QuickFolderSize_debug.log` を確認する(WebView2環境初期化の各ステップをログ出力している)
- `WebView2 Runtimeを初期化できませんでした` エラー: Microsoft Edge WebView2 Runtime (Evergreen) をインストールする
- ビルド時に `WebView2 SDK headers not found`: `WEBVIEW2_INCLUDE` 環境変数でヘッダの実際の配置先を指定する

## 依存関係

Win32 API (`FindFirstFileW`/`FindNextFileW`, `IFileDialog`, `DwmSetWindowAttribute` 等) と WebView2 SDK のみ。サードパーティのC++ライブラリ依存なし。フロントエンドはバニラJS(フレームワーク非依存)。

## ファイル構成

```
QuickFolderSize/
├── core/
│   └── native/
│       ├── engine.h           スキャンエンジンのC互換API定義
│       ├── engine.cpp         スキャンエンジン本体(並列スキャン・mtimeキャッシュ・ドライブ容量)
│       ├── webview_main.cpp   WebView2ホスト・WebMessageプロトコル・ファイルダイアログ
│       ├── QuickFolderSize.rc アイコンリソース
│       └── QuickFolderSize.ico
├── templates/
│   └── index.html             開発用テンプレート
├── static/
│   ├── css/style.css
│   └── js/app.js               UIロジック一式(i18n・ツリー描画・WebMessageブリッジ)
├── python/                    Phase 1プロトタイプ(参照用に保持)
├── document/
│   ├── spec.md                 仕様書
│   ├── environment.md          本ファイル
│   └── about.md                 バージョン情報
├── dist/
│   ├── binary/                 EXE・DLL・バンドル済みHTML(build.bat が生成、Git管理外)
│   └── documents/              配布用 readme.txt / history.txt（Git 管理）
├── build_native.py             ビルド本体
├── bundle_html.py               HTML/CSS/JSバンドラー
├── build.bat                    ビルドラッパー
├── .gitignore
└── CLAUDE.md                    実装計画・Claude 向け指示
```

# 計画書: MinGW/C++ネイティブビルド＋WebView2化(v1.0)

## 目的

現行の Python + PyQt6 実装(`python/`)を、MinGW-w64(g++)でビルドする C++ ネイティブ実行ファイル + WebView2 GUI に置き換える。スキャンエンジンは C++ で DLL 化する。UI テーマ・雰囲気は同ワークスペース内の `QuickDiskBench` を踏襲する。名称(プロジェクト名/ローカルフォルダ名/GitHubリポジトリ名/ブランチ名)の変更は前回セッションで完了済みのため、本計画はアプリ本体・ドキュメントの刷新のみを対象とする。

## 現状分析

### 参照実装: QuickDiskBench(`c:/Users/0120025-Z100/source/project/QuickDiskBench`)

同一ワークスペース内で稼働実績のある構成。今回はこのアーキテクチャパターンをそのまま踏襲する。

- `core/native/engine.cpp` — 計算エンジン本体。DLL(`engine_x64.dll`)としてもビルドしつつ、GUI/CLI 実行ファイルには**静的リンク**(ソース重複コンパイル)している
- `core/native/webview_main.cpp`(867行) — `WinMain` + `WndProc` によるネイティブウィンドウ、WebView2 の環境/コントローラ生成、`NavigateToString` によるHTML読み込み、JSON文字列ベースの `WebMessage` プロトコル(JS→native: `get_drives`/`start`/`stop`、native→JS: `PostWebMessageAsJson` でprogress/drives通知)、ワーカースレッド+`PostMessage`によるUIスレッドへの安全な進捗反映
- `build_native.py` — コンパイラ検出(PATH→WinGetパッケージ既定パス→既知パスの順にフォールバック。`g++`優先、`clang++`次点)、リソース(.rc→.o)ビルド、DLL/GUI exe/CLI exeの3点ビルド、WebView2Loader.dllと成果物一式を`dist/binary`へコピー
- `bundle_html.py` — 開発用の`templates/index.html` + `static/css/style.css` + `static/js/*.js` を、`<style>`/`<script>`をインライン化した**自己完結index.html 1枚**に変換。WebView2の`NavigateToString`は相対パスの外部リソースを解決できないため必須の工程
- ビルド環境は本機で検証済み: MinGW(WinLibs MCF UCRT, `g++`は既にPATH解決可)、WebView2 SDKヘッダ(`C:\tools\webview2\build\native\include`)とも導入済み

### 現行 QuickFolderSize (Python/PyQt6, `python/`) の機能一覧

| ファイル | 役割 |
|:-----|:-----|
| `scanner.py`(293行) | `FolderEntry`データ構造、`ScanWorker`(QThread)。単一共有`ThreadPoolExecutor(32)`で**全階層並列**スキャン、mtimeキャッシュ(前回スキャン結果と`st_mtime`比較して変化なしならディレクトリを再走査せず再利用)、junction/symlink除外(`_is_real_dir`) |
| `model.py`(180行) | `FolderModel`(`QAbstractItemModel`)。列は Name/Size/Bar(進捗バー用比率)/Files/Modified。`SortProxyModel`で列ソート |
| `delegate.py` | `SizeBarDelegate` — Bar列に相対サイズのプログレスバーを描画 |
| `navmodel.py` | `NavModel` — 左ナビペインのドライブ一覧+フォルダツリー、遅延展開(`canFetchMore`/`fetchMore`) |
| `mainwindow.py`(428行) | 全体組み立て。メニュー(ファイル: 開く/再スキャン/レポート作成/終了、ヘルプ: バージョン情報)、アドレスバー(ドライブ容量表示・スキャン時間表示・言語切替ボタン)、ツリーテーブル、スキャン進捗のポーリング表示 |
| `utils.py` | `format_size()`、`generate_md_report()` — Markdownレポート生成 |
| `i18n.py` | `tr()` による日本語/English切り替え。`APP_VERSION = "v1.0.0"` |

### アイコン

`python/resources/icon.ico` が既存。新実装でも同ファイルを`.rc`経由で流用する。

### ドキュメント(要更新)

- `CLAUDE.md`(プロジェクト直下) — 現在「Phase 3: C++ Qt6移植(将来)」という**古い方針**が書かれている。今回のWebView2化で実際に更新する
- `document/spec.md` / `document/environment.md` / `document/about.md` — 実装に合わせて全面更新が必要
- `main.py`内`setApplicationName("FolderViewer")`など、名称変更(→QuickFolderSize)が未反映の箇所が残っている

## 変更内容

### 新規ディレクトリ構成(QuickDiskBenchと同一パターン)

```
QuickFolderSize/
├── core/
│   └── native/
│       ├── engine.cpp            # 新規: スキャンエンジン(並列スキャン+mtimeキャッシュ+ドライブ容量)
│       ├── webview_main.cpp      # 新規: WebView2ホスト + JSON WebMessageプロトコル
│       ├── QuickFolderSize.rc    # 新規: アイコンリソース
│       └── QuickFolderSize.ico   # 新規: python/resources/icon.ico から複製
├── templates/
│   └── index.html                # 新規: 開発用テンプレート
├── static/
│   ├── css/style.css             # 新規
│   └── js/app.js                 # 新規: ツリーテーブル・ソート・進捗描画・言語切替・レポート生成
├── build_native.py               # 新規: build_native.py を移植・流用
├── bundle_html.py                # 新規: そのまま流用可能(プロジェクト名のみ差し替え)
├── dist/
│   ├── binary/                   # ビルド生成物(Git管理外)
│   └── documents/                # 配布用ドキュメント(Git管理)
├── document/
│   ├── spec.md                   # 更新: 新アーキテクチャに合わせて全面書き直し
│   ├── environment.md            # 更新: Phase3節を撤去し、実際のビルド手順に置換
│   └── about.md                  # 更新: 開発環境の記載をC++/MinGW/WebView2に変更
├── python/                       # 現状維持(削除しない。動作確認が取れるまでの参照実装として残す)
├── build.bat                     # 更新: python版ビルドから `python build_native.py` 呼び出しへ
└── CLAUDE.md                     # 更新: Phase3を「実施済み」に、実装計画をC++/WebView2版に書き換え
```

**`python/`ディレクトリは今回削除しない。** 新実装が実機動作確認まで完了した後、別途ユーザーの判断で削除を検討する(本計画のスコープ外)。

### `core/native/engine.cpp` — スキャンエンジン

Python `scanner.py` の移植。以下をC++で実装:

- `FindFirstFileW`/`FindNextFileW` によるディレクトリ列挙(`os.scandir()`相当)
- `FILE_ATTRIBUTE_REPARSE_POINT` チェックによる junction/symlink 除外(`_is_real_dir()`相当)
- スレッドプール(`std::thread` + キュー、または `std::async`)による全階層並列スキャン。Python版は共有プール・非ブロッキングfan-outで深さ制限なしに並列化しているため、同じ設計思想を踏襲する(単純な固定深度制限は行わない)
- `FILETIME` 比較によるmtimeキャッシュ(前回スキャン結果を保持し、変化のないディレクトリは再走査をスキップ) — プロセス内グローバル状態としてホスト(`webview_main.cpp`)側で保持し、再スキャン時にengine側へ渡す
- `GetDiskFreeSpaceExW` によるドライブ容量取得
- 進捗コールバック(1ディレクトリ完了ごとに呼ばれる関数ポインタ) — `webview_main.cpp`からWebViewへJSON中継する

DLL(`engine_x64.dll`)としても出力しつつ、GUI実行ファイルには静的リンクする(QuickDiskBenchと同一方式)。

### `core/native/webview_main.cpp` — WebView2ホスト

- ウィンドウ生成・WebView2環境/コントローラ初期化・`index.html`(dist配布時は自己完結バンドル)の`NavigateToString`読み込みは QuickDiskBench の実装パターンを踏襲
- JSON `WebMessage` プロトコル(JS↔native):
  - JS→native: `browse`(フォルダ選択ダイログ表示)、`scan`(スキャン開始)、`nav_expand`(左ペインの子フォルダ取得)、`export_report`(レポート保存ダイアログ+書き込み)、`toggle_lang`
  - native→JS: `scan_progress`(子フォルダ1件完了ごとの部分木)、`scan_finished`(完了時のルート全体)、`scan_error`、`drives`(ドライブ一覧)、`nav_children`(遅延展開結果)
- フォルダ選択・レポート保存は `IFileDialog`(Win32)を使用
- ドライブ一覧・使用容量取得は QuickDiskBench の `GetDrivesList()` をベースに、ドライブラベル等の不要な項目を削り本アプリに必要な情報(パス・使用量・空き容量)に絞る

### `templates/index.html` + `static/css/style.css` + `static/js/app.js`

PyQt6 UIの1:1移植。

- アドレスバー: 現在パス表示、ドライブ容量(`C:\ 150.3 GB / 512.0 GB`)、スキャン時間(`X.XXs`)、言語切替ボタン
- 左ナビペイン: ドライブ一覧 + 遅延展開フォルダツリー(HTML `<ul>`のネスト開閉、`nav_expand`メッセージで子要素を都度取得)
- メインツリーテーブル: Name/Size/Bar(相対サイズの横棒)/Files/Modified列。ヘッダクリックでJS側ソート(`SortProxyModel`相当のロジックをJSで再実装)
- メニュー: HTML/CSS製のドロップダウン(ネイティブWin32メニューは使わない。QuickDiskBenchに前例がなく実装コストが低いため)。ファイル(開く/再スキャン/レポート作成/終了)、ヘルプ(バージョン情報)
- placeholder表示: スキャン開始時に直下1レベルを即時表示し、`scan_progress`受信ごとに数値を埋めていく(既存仕様を維持)
- 言語切替: `i18n.py`の全キーをJSオブジェクトに移植。レポート出力の文言にも反映
- レポート生成: `utils.py`の`generate_md_report()`をJSに移植。生成したMarkdown文字列を`export_report`メッセージでnativeへ渡し、ファイル保存はnative側の`IFileDialog`で行う

### `build_native.py` / `bundle_html.py`

QuickDiskBenchのファイルをベースに、プロジェクト名・パス・出力ファイル名(`QuickFolderSize.exe`)のみ差し替えて移植。コンパイラ探索ロジック・WebView2 SDKパス解決・ビルド手順は同一のまま踏襲する。

### ドキュメント更新

- `CLAUDE.md`(プロジェクト直下): Phase 2まで完了・Phase 3(C++ Qt6移植)を撤回し、「Phase 3: C++/WebView2移植」として実施内容に書き換え。ファイル構成節も新構成に更新
- `document/spec.md`: 新アーキテクチャ・新UI(HTML/JS)に合わせて全面更新
- `document/environment.md`: ビルド手順を`build_native.py`ベースに置換。Phase3節(古いQt6対応表)を削除
- `document/about.md`: 開発環境をC++/MinGW/WebView2に更新

### 変更しないもの

- 名称(プロジェクト名/ローカルフォルダ名/GitHubリポジトリ名/ブランチ名)— 前回セッションで完了済み、本計画では扱わない
- `python/`ディレクトリ自体の削除
- git操作(commit/push)— 本計画には含めない。実装完了後、別途ユーザーの明示的な指示を待つ

## 検証方法

1. `python build_native.py` が成功し、`dist/binary/QuickFolderSize.exe` / `engine_x64.dll` / `WebView2Loader.dll` / `index.html` が生成されること
2. `QuickFolderSize.exe` を起動し、ウィンドウが表示され WebView2 コンテンツが描画されること(QuickDiskBenchと同様、`QuickFolderSize_debug.log` にトレースログを出力し起動失敗時の切り分けに使う)
3. 任意のドライブ(例: `C:\`)を選択してスキャンを実行し、直下1レベルのplaceholder即時表示 → 完了後の数値埋め込み、という既存の体感速度仕様が再現されること
4. スキャン結果のフォルダサイズ合計が、エクスプローラーのプロパティ表示(または既存Python版の結果)と概ね一致すること
5. 再スキャン時、mtimeキャッシュにより変化のないディレクトリで所要時間が短縮されること(既存の「再スキャン20秒程度」という参考値と比較)
6. 左ナビペインのドライブ一覧・遅延展開フォルダツリーが機能すること
7. レポート作成(ファイル→レポート作成)でMarkdownファイルが正しく出力され、内容が既存の`generate_md_report()`と同等のフォーマットであること
8. 言語切替ボタンでUI全文言・レポート出力が日本語⇔Englishに切り替わること
9. `document/`配下の各ドキュメントが実装と整合していること

## リスク

- **スキャンエンジンの並列化ロジック移植**: Python版は「単一共有プール・非ブロッキングfan-out」という特有の設計で全深度並列化とデッドロック回避を両立している(`scanner.py`冒頭のコメント参照)。C++でスレッドプールを素朴に実装すると、深いツリーでタスクがプール内タスクの完了を待って詰まる(デッドロック)可能性があるため、Python版と同じ「非ブロッキングfan-out」設計を踏襲する必要がある
- **巨大ツリーのJSONメッセージサイズ**: `C:\` のような数十万エントリのドライブをフルスキャンした場合、`scan_finished`で渡すJSON文字列が数MB〜数十MB規模になる可能性がある。WebView2の`PostWebMessageAsJson`のサイズ上限・パフォーマンスは未検証のため、実装時に大規模ドライブで実測が必要
- **WebView2のNavigateToStringにおける外部リソース制約**: `bundle_html.py`でCSS/JSをインライン化する前提だが、開発中(`templates/index.html`を直接ブラウザで開く場合)と配布ビルド後で挙動差が出うる。QuickDiskBenchと同じ運用(開発は`templates/`直編集、配布前に必ずバンドル)を徹底する
- **ファイルダイアログ(IFileDialog)の実装コスト**: QuickDiskBenchにはフォルダ選択・ファイル保存ダイアログの実装例がないため、本計画で新規に実装する。COM初期化まわりのエラーハンドリングに注意
- **mtimeキャッシュの永続化範囲**: Python版はアプリ起動中のメモリ上でのみキャッシュを保持(プロセス再起動で消える)。C++版も同様の挙動で問題ないか、あるいはディスクへの永続化まで求めるかは本計画では「Python版と同じ挙動(プロセス内メモリのみ)」を前提とする
- **実装規模**: `webview_main.cpp`だけでQuickDiskBenchは867行。UI要素(ツリーテーブル・ナビペイン・メニュー)が本アプリの方が多いため、`app.js`/`webview_main.cpp`とも同等かそれ以上の規模になる見込み。1セッションで全て完了しない可能性があり、エンジン移植→UI移植→ドキュメント整備の順にチェックポイントを区切って進める

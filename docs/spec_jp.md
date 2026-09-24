# QuickFolderSize 仕様書

## 1. アプリ概要

| 項目 | 内容 |
|:-----|:-----|
| バージョン | v3.2.0 |
| 目的 | ローカルドライブ・フォルダの使用容量を視覚的に把握する |
| 対象OS | Windows 10 / 11 (64bit) |
| 実装言語 | C++17（MinGW-w64 / g++）+ WebView2（HTML/CSS/JS） |
| 表示言語 | 日本語 / English（メニューバー右端のトグルボタンで切替、クライアント側のみで完結。再起動で日本語に戻る） |
| 起動方法 | `dist\QuickFolderSize.exe` を実行 |

> 開発環境・ビルド手順 → `docs/environment.md` を参照

Phase 1（Python/PyQt6プロトタイプ）からPhase 3として全面移植した。UIロジック・アーキテクチャはワークスペース内の `QuickDiskBench`（C++/MinGW/WebView2、実績あり）を踏襲している。旧Python実装は `python/prototype/` に参照用として残っている。

---

## 2. アーキテクチャ

```
┌─────────────────────────────────────────────────────────┐
│ QuickFolderSize.exe (WinMain)                            │
│  ┌───────────────────────────────────────────────────┐  │
│  │ src/webview_main.cpp                       │  │
│  │  - WebView2ホスト(環境/コントローラ初期化)            │  │
│  │  - JSON WebMessageプロトコル送受信                    │  │
│  │  - IFileDialog(フォルダ選択・レポート保存)             │  │
│  │  - スキャンワーカースレッド管理・mtimeキャッシュ保持     │  │
│  └───────────────────────────────────────────────────┘  │
│              │ 呼び出し                    │ JSON postMessage │
│              ▼                             ▼             │
│  ┌─────────────────────┐      ┌─────────────────────┐   │
│  │ src/engine.cpp│      │  WebView2 (Chromium)│   │
│  │  - 並列スキャンエンジン │      │  index.html/app.js  │   │
│  │  - ドライブ容量取得     │      │  (ツリー表示・i18n等) │   │
│  │  - フォルダ列挙(nav用)  │      │                     │   │
│  └─────────────────────┘      └─────────────────────┘   │
└─────────────────────────────────────────────────────────┘
```

- `engine.cpp` は単体DLLとしては提供せず、GUI実行ファイル(`QuickFolderSize.exe`)・CLI実行ファイル(`QuickFolderSize_cli.exe`)の双方へソースを直接static linkする(QuickDiskBenchと同一方式)。両実行ファイルは同じ `engine.cpp`/`engine.h` を共有する
- ネイティブ⇔JS間は `PostWebMessageAsJson` / `window.chrome.webview.postMessage` によるJSON文字列メッセージでやり取りする(WebMessageプロトコルは本ファイル5節を参照)
- バンドルHTMLはビルド時に `bundle_html.py` が `src/ui/index.html` + `src/ui/css/style.css` + `src/ui/js/app.js` を1枚のHTMLへインライン化し(相対パスの `<img>` は data URI に埋め込む。`NavigateToString` は外部リソースを解決できないため)、`src/index_embed.html` へコピーしたうえで `QuickFolderSize.rc` の `RCDATA` としてEXEへ埋め込む。GUIは起動時に `FindResourceW`/`LoadResource` で読み出すだけで、ディスク上の `index.html` には依存しない

---

## 3. 画面構成

ダーク・グラスモーフィズム（QuickDiskBench と同系統）。ほぼ黒のキャンバスにシアン／青のアクセント。ステータスバーは持たない。

```
┌─ ファイル / ヘルプ ────────────────────────────────── [🌐 English] ─┐
├─ [C:\ (Label) 150.3 GB / 512.0 GB]  [パス入力]  [スキャン] ─────────┤
├─ ドライブ / フォルダ（遅延）─┬─ 名前 | サイズ | 割合 | ファイル数 | 日時 ─┤
│                              │  📁 Users     254.6 GB  ████  62.4%  │
│  スキャン時間  完了  23.73s  │  📄 pagefile   16.0 GB  ██    24.9%  │
└──────────────────────────────┴──────────────────────────────────────┘
```

| エリア | 説明 |
|:-------|:-----|
| メニューバー | ファイル（開く / 再スキャン / レポート作成 / 終了）、ヘルプ（ヘルプ本文 / バージョン情報）。参照ダイアログはメニューまたは `Ctrl+O` のみ（アドレスバーに Browse ボタンはない） |
| 言語切替ボタン | メニューバー右端。クリックで 日本語 ⇔ English。メニュー・列ヘッダー・ボタン・About・Markdownレポートが即時切替。ネイティブ通信なし。再起動で日本語に戻る |
| ドライブ容量ラベル | アドレスバー左端。ボリュームラベル付きで使用量/総容量を表示（例: `C:\ (Windows-SSD) 426.7 GB / 930.4 GB`） |
| パス入力・スキャン | フルパスを直接入力してスキャン。Enter でも開始 |
| 左ナビペイン | ドライブ一覧（ラベル付き）と遅延展開フォルダツリー。クリックでスキャン、またはスキャン済みツリー内なら即時表示切替 |
| スキャン時間カード | 左ナビ下。スキャン中はボタン押下からの経過秒を 0.2 秒ごとに `X.XXs` で更新。完了後はラベルと秒数のあいだに `完了`（English: `Done`）を出し、同じ経過秒を固定表示する |
| 右スキャンツリー | 結果の階層ツリー。▶/▼で展開。ヘッダークリックでソート（既定はサイズ降順）。行クリックでアドレスバーにそのパスを入れる |
| About ダイアログ | ヘルプ → バージョン情報。開発環境・制作者の下に `src/ui/img/author.png`（配布HTMLへ埋め込み） |
| ヘルプダイアログ | ヘルプ → ヘルプ...。`src/app/help/help.md`(英語)/`help_jp.md`(日本語)の原文をビルド時にHTMLへ埋め込み、表示言語に応じて自前のMarkdownサブセットレンダラーでHTML化して表示。スクロール可能なモーダル |

---

## 4. 機能一覧

| 機能 | 説明 |
|:-----|:-----|
| フォルダスキャン | 指定パス以下を再帰スキャン、フォルダ・ファイルを集計 |
| バックグラウンドスキャン | ワーカースレッド + `scan_directory()` でUIスレッドをブロックしない |
| 全深度並列スキャン | 固定サイズのスレッドプール(32 workers)を全ディレクトリで共有。非ブロッキングfan-outで完了通知するため、深さに関わらず並列度が落ちない |
| NTFS MFT高速経路 | `C:\` などのNTFSボリューム直下を`\\.\X:`からMFTを読み、FILEレコードの親参照でツリーを復元。ディレクトリ巡回を省略する。EXEは`requireAdministrator`で起動する |
| スキャン方式フォールバック | MFTを開けない場合、非NTFS、個別フォルダ、ネットワークパスは従来の`FindFirstFileW`/`FindNextFileW`方式へ自動フォールバック |
| ジャンクション対策 | 再解析ポイントのうち、タグが名前サロゲート(`IsReparseTagNameSurrogate`: ジャンクション、シンボリックリンク、マウントポイント等)のものだけを集計・再帰から除外(ファイル・ディレクトリ双方)。OneDrive等のクラウドファイル、重複除去、WOF圧縮はデータがその場所にあるため集計する。タグが読めない場合は除外。タグはWin32経路では`WIN32_FIND_DATAW::dwReserved0`、MFT経路では`$REPARSE_POINT`属性(非常駐なら先頭クラスタをボリュームから読む)から取得 |
| オンラインのみのファイル | `FILE_ATTRIBUTE_OFFLINE`または`FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS`を持つファイル(OneDriveの「オンラインのみ」等)は、ファイル数には含めるがサイズは0とする(ローカルのディスクを使わないため)。`0x40000`(RECALL_ON_OPEN)はディスク上では`FILE_ATTRIBUTE_EA`と同じビットのため判定に使わない |
| ハードリンク | 複数の名前を持つファイルは1回だけ集計する(ディスク使用量を二重に数えない)。MFT経路では名前空間の優先度が最も高い名前(Win32+DOS > Win32 > POSIX > DOS)の親フォルダに計上する |
| MFT拡張レコード | `$ATTRIBUTE_LIST`を持つファイル(断片化の激しい大きいファイル、ハードリンクの多いファイル等)は`$DATA`・`$FILE_NAME`・`$REPARSE_POINT`が拡張レコード側にあるため、読み込み後にベースレコードへマージする。ベース参照のシーケンス番号が一致しない拡張レコードは使わない |
| インクリメンタルUI更新 | ルート直下のディレクトリが完了するたびに `scan_progress` メッセージでツリーを順次更新 |
| mtime 差分キャッシュ | 前回スキャン結果のツリーをネイティブ側で保持。再スキャン時は各ディレクトリのFILETIMEを比較し、変化のないディレクトリは列挙をスキップしてキャッシュから再利用(ファイルは無条件再利用、サブディレクトリは個別に再検証) |
| プレースホルダー表示 | スキャン開始と同時に対象フォルダの直下1レベルをすぐに表示（サイズは空）。完了後に数値が埋まる |
| スキャン時間表示 | 左下カードに、スキャンボタン押下からの JS 経過秒を `X.XXs` で表示（0.2 秒更新）。完了後も同じ時計で固定し、あいだに `完了` / `Done` を出す。native の `elapsed_seconds`（`scan_directory` 本体のみ）は表示に使わない（JSON化・転送分で巻き戻って見えるため） |
| ドライブナビ | 左ペインにシステムドライブ一覧を表示（`GetLogicalDriveStringsW`）。ボリュームラベルがあれば括弧付きで出す |
| フォルダ遅延展開 | ナビツリーはクリック時に初めて`nav_expand`で子フォルダをロード |
| スキャン済みツリー内ナビ | スキャン済みツリー内のパスをナビクリックした場合、ネイティブへ再スキャンを要求せずJS側の保持木から該当ノードを取得して即時表示切替 |
| ツリー表示 | フォルダ（📁）とファイル（📄）を混在でツリー表示、行ごとに展開/折りたたみ |
| サイズバー | 親フォルダに対する割合をプログレスバーで可視化 |
| 列ソート | 各列ヘッダークリックで昇順/降順ソート(JS側で実装) |
| 参照ダイアログ | `IFileDialog`(FOS_PICKFOLDERS)でフォルダ選択 |
| ドライブ容量表示 | 選択フォルダのドライブの使用容量/総容量を `GetDiskFreeSpaceExW` で取得し表示 |
| レポート出力(Markdown) | Markdown 形式で階層フォルダ容量レポートを生成し、`IFileDialog`(保存)でファイル書き出し |
| レポート出力(JSON) | `path`/`scanned_at`/`total_size`/`subfolder_count`/`file_count_recursive`/`tree`を持つJSON形式でレポートを生成し、`IFileDialog`(保存)でファイル書き出し。CLI版(`QuickFolderSize_cli.exe`)のJSON出力と同一スキーマ |
| アクセスエラー対応 | `GetFileAttributesExW` / `FindFirstFileW` が失敗したフォルダは `is_accessible=false` とし、行を赤字（`#ef4444`）で表示してクラッシュしない。ジャンクション等のリパースポイントは赤字ではなく再帰対象外 |
| 表示言語切替 | `app.js` の `I18N` テーブルで全UI文言（メニュー・ボタン・列ヘッダー・About・スキャン完了ラベル・Markdownレポート）を管理。メニューバー右端のトグルで即時切替（ネイティブ通信なし、再起動で日本語に戻る） |
| About | バージョン・開発環境・制作者と、ダイアログ下部の作者画像 |
| ヘルプ本文表示 | `src/app/help/`のMarkdown原稿をアプリ内モーダルで表示。外部ライブラリなしの自前レンダラー(見出し・段落・強調・コード・リンク・箇条書き・表のみ対応) |
| CLI版 | `QuickFolderSize_cli.exe <path>`。GUIを介さず標準出力へJSON(レポート出力(JSON)と同一スキーマ)を返す。管理者権限は要求しない(非対話実行を妨げないため)。詳細は`README.md`/`README_jp.md`を参照 |

---

## 5. WebMessage プロトコル

JS→native は `window.chrome.webview.postMessage({cmd: ..., ...})`、native→JS は `PostWebMessageAsJson` で送る(自動でJSON文字列⇔JSオブジェクト変換される)。

### JS → native

| cmd | パラメータ | 説明 |
|:----|:-----------|:-----|
| `get_drives` | なし | ドライブ一覧を要求 |
| `browse` | `initial` | フォルダ選択ダイアログを開く |
| `scan` | `path` | 指定パスの再帰スキャンを開始(実行中スキャンがあればキャンセルしてから開始) |
| `nav_expand` | `path` | 左ナビペイン用、直下サブディレクトリを非再帰で列挙 |
| `export_report` | `format`(`md`/`json`), `lang`(`ja`/`en`) | レポート本文はJSからは送らない。ネイティブ側がスキャン結果(`g_prevRoot`)から直接組み立てて保存ダイアログ経由でファイル書き出しする(数万ノード規模のツリーで数十MBの文字列がWebMessageを往復し実用不能になっていたため、v2.1.1でこの方式に変更) |
| `quit` | なし | アプリを終了 |

### native → JS

| type | 内容 |
|:-----|:-----|
| `drives` | ドライブ一覧(`path`/`label`/`total_gb`/`free_gb`/`used_gb`) |
| `browse_result` | 選択されたフォルダパス(キャンセル時は空文字) |
| `scan_placeholder` | スキャン開始直後の直下1レベル一覧 + 対象ドライブの容量情報 |
| `scan_progress` | ルート直下の子フォルダが1つ完了するたびに、その子の完全な部分木(入れ子JSON) |
| `scan_finished` | スキャン完了時のルート全体(入れ子JSON) + `elapsed_seconds` + `scan_method`（`mft` または `win32`。エンジン本体の秒数はUIの表示時間には使わない） |
| `scan_error` | 指定パスが存在しない/フォルダでない場合のエラー通知 |
| `nav_children` | `nav_expand` の応答(直下フォルダ一覧) |
| `export_result` | レポート保存の成否とパス |

スキャン結果のノードJSON形式:

```json
{
  "name": "SubFolder", "path": "C:\\...\\SubFolder\\",
  "size": 838860800, "file_count": 500,
  "mtime_ms": 1737000000000,
  "is_accessible": true, "is_dir": true,
  "children": [ ... ]
}
```

---

## 6. スキャン・キャッシュ動作フロー

```
フォルダ選択 (scan cmd受信、UIスレッド)
  │
  ├─ 実行中スキャンがあれば stop_flag=1 → join
  ├─ 直下1レベルを同期的に列挙 → scan_placeholder 送信
  └─ ワーカースレッド起動
       │
       └─ scan_directory(path, 前回ルート, ...) [engine.cpp]
            ├─ NTFSボリューム直下 + 管理者権限 → MFT高速経路（完了時に一括通知）
            └─ その他 / MFT読取失敗 → Win32列挙経路
                 ├─ ルート直下は常に FindFirstFileW/FindNextFileW で再列挙
            │    サブディレクトリごとに ScanNode() をスレッドプールへ投入(非ブロッキング)
            │
            └─ 各ディレクトリタスク ScanNode()
                 ├─ GetFileAttributesExW で mtime(FILETIME)取得
                 ├─ キャッシュあり & mtime一致
                 │    └─ FromCacheAndFanOut()（ファイルは再利用、サブディレクトリは個別に再帰検証）
                 └─ キャッシュなし / mtime変化
                      └─ FullScanAndFanOut()（FindFirstFileW等で再列挙）
                 │
                 └─ 全子タスク完了(pendingカウントが0)でノード確定
                      → ルート直下の子ならその場でJSON化してscan_progress送信(借用ポインタ)
                      → ルート全体完了で scan_finished 送信 + ネイティブ側のキャッシュ木を更新

完了
  │
  ├─ ネイティブ側: 直前のキャッシュ木を解放し、今回のツリーを次回用に保持
  └─ JS側: ボタン押下からの経過秒を完了表示として固定し、受信ツリーを pathIndex に登録、テーブル再描画
```

MFT高速経路はNTFSのFILEレコードを直接読むため、EXE起動時にUAC確認を表示して管理者権限を取得する。MFT経路では巨大な部分木の進捗JSONを送らず、完了結果を一度だけ送信する。画面表示用のMFTのJSONではノードごとの絶対パスを省略し、JS側で親パスから復元する。エンジンのツリー(`ScanEntryC::path`)には各ノードのフルパスを設定するため、CLI出力とJSON/Markdownレポートにはパスが入る。

キャンセルされたスキャン(別フォルダへの切替等)は、Python版と同様に結果を破棄しキャッシュを更新しない。

並列スキャン中に発見したディレクトリエントリは、名前サロゲートの再解析ポイント(ジャンクション/シンボリックリンク等)を除外してから再帰対象に加える。OneDrive等のクラウドフォルダは再帰する。

---

## 7. キーボードショートカット

| ショートカット | 機能 |
|:--------------|:-----|
| `Ctrl+O` | フォルダを開く（ダイアログ） |
| `F5` | 再スキャン |
| `Ctrl+Shift+S` | レポート作成（Markdown 保存） |
| `Ctrl+Q` | アプリ終了 |
| `Enter`（アドレスバー） | スキャン実行 |

すべて `app.js` の `keydown` イベントリスナーで実装(WebView2ページ内で完結、ネイティブアクセラレータテーブルは使用しない)。

---

## 8. Markdown レポートフォーマット仕様

`File > Export Report...` で生成されるファイルの構造（見出し等の文言はレポート作成時点の表示言語に従う。以下は日本語表示時の例）:

```markdown
# フォルダ使用容量レポート

| 項目 | 値 |
|:-----|:---|
| パス | `C:\path\to\folder` |
| スキャン日時 | 2025/01/15 10:30 |
| 合計サイズ | 1.2 GB |
| サブフォルダ数 | 42 |
| ファイル数（再帰） | 1,234 |

## フォルダ構成

サイズ降順・階層表示。ファイルは各フォルダ内にインデントで記載。

- 📁 **SubFolder** — 800.0 MB (66.7%)  ファイル: 500個
  - 📁 **Nested** — 400.0 MB (50.0%)  ファイル: 200個
    - 📄 largefile.bin — 200.0 MB (50.0%)
  - 📄 data.csv — 50.0 MB (6.3%)
- 📄 readme.txt — 4.0 KB (0.0%)

---
*生成日時: 2025/01/15 10:30:45*
```

生成は `app.js` の `generateMdReport()`(JS側)で行い、`export_report` メッセージでネイティブへ渡してファイル書き込みする(UTF-8、BOMなし)。

---

## 9. パフォーマンス

### 並列化設計

固定サイズのスレッドプール(32 workers、`src/engine.cpp` の `ThreadPool`)を全深度・全ディレクトリで共有する。各タスクは「自分の分を処理して即返る」か「子ディレクトリをプールへ再投入して即返る」かのどちらかしかせず、他タスクの完了をブロック待ちしない(`pending` カウンタが0になった時点で最後に完了したスレッドが親ノードを確定させる)。そのためツリーの深さに関わらず全ワーカーがブロックで固まることがない。

### キャッシュ(再スキャン)

- ディレクトリのmtimeはFILETIME(100ns精度)をそのまま64bit整数として比較するため、Python版で必要だった浮動小数点誤差の許容(`abs(a-b)<0.001`)は不要
- キャッシュヒット時は該当ディレクトリの列挙(FindFirstFileW)をスキップ。ファイルは無条件再利用、サブディレクトリは個別に再帰検証(孫ディレクトリの変更も正しく検出される)
- スキャン完了ごとに直前のキャッシュ木を解放して置き換える(累積させない)
- キャンセルされたスキャンの結果はキャッシュに反映しない

---

## 10. MCPサーバー(`src/integrations/mcp-server/`)

`QuickFolderSize_cli.exe` をHTTP経由のMCP(Model Context Protocol)ツールとして公開するNode.js製サイドカー。本体アプリ(C++/WebView2)とは別プロセスで、独自の`package.json`でnpmパッケージとしてのバージョンは別管理だが、この機能追加自体がプロジェクト全体をv3.1.0へ引き上げた要因。Claude CodeのようなAIエージェントが、GUIを開かずスキャン結果を取得できるようにする目的で追加した。

- **構成**: `express` + `@modelcontextprotocol/sdk` の `StreamableHTTPServerTransport`(ステートレス、`sessionIdGenerator: undefined`)。`http://127.0.0.1:39391/mcp` で待ち受け(ポートは`QFS_MCP_PORT`環境変数で変更可)。リクエストごとに`QuickFolderSize_cli.exe`を子プロセスとしてspawnし、標準出力のJSONをそのまま(または要約して)返す
- **管理者権限**: `start-admin.bat`/`start-admin.ps1`で起動する前提。`fs.accessSync('C:\Windows\System32\config\SAM', ...)`が成功するかでelevation判定する。管理者権限で起動すればCLIがMFT高速経路を使える。非管理者起動時にCLIへ巨大フォルダを投げると低速なWin32列挙になり、システム負荷が高くなる
- **提供ツール**:

  | ツール | 説明 |
  |:-------|:-----|
  | `server_status` | サーバープロセスの昇格状態を返す |
  | `scan_folder(path, pretty?)` | 同期スキャン。CLIの標準出力JSONをそのまま1回のレスポンスで返す |
  | `start_scan(path, pretty?)` | 非同期スキャンを開始し、即座に`scanId`を返す(サーバー内の`Map`でジョブ状態を保持) |
  | `get_scan_result(scanId)` | `start_scan`のポーリング。完了時は`{path, scanned_at, total_size, subfolder_count, file_count_recursive, top_level_children}`の軽量要約と、フルツリーの保存先(`scan-reports/<scanId>.json`)を返す |

- **`start_scan`/`get_scan_result`を追加した理由**: MCPクライアント(Claude Code)側のツール呼び出しには、サーバー側タイムアウト(5分)よりずっと短い、明示されていない待機タイムアウトがある。`scan_folder`の処理に数十秒以上かかる大きいフォルダ(数万ファイル規模)を対象にすると、サーバーは正常応答しているにもかかわらずクライアント側で`session expired`エラーになることを確認した。非同期パターンでツール呼び出し1回あたりの待機時間を短く保つことで回避する
- **運用上の注意**: 詳細は[`src/integrations/mcp-server/README.md`](../src/integrations/mcp-server/README.md)を参照。Claude Code側のツール呼び出しタイムアウトを完全に避けたい場合は、Bashツールから`http://127.0.0.1:39391/mcp`へ直接curlで叩く方法もある(SSE形式のレスポンスをNode.jsでパースする)

---

## 11. 今後の実装予定

| 案 | 内容 |
|:---|:-----|
| 除外フィルター UI | `node_modules` / `.git` / `AppData\Local\Temp` をスキップするオプション |
| 深さ制限＋遅延展開 | 巨大ツリーでのJSON肥大化対策として、一定深度以降は展開クリック時にスキャンする方式を検討 |

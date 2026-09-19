QuickFolderSize - フォルダ使用容量ビューワー
配布パッケージ  v3.0.0

GitHub
------
https://github.com/maktak-105/QuickFolderSize

概要
----
QuickFolderSize は、ローカルのフォルダやドライブをスキャンして、
各フォルダ・ファイルの使用容量を表示する Windows アプリです。
結果は割合バー付きのツリーでソートでき、Markdown レポートも出力できます。

ネイティブアプリ（C++ + WebView2）です。Python や Qt は不要です。

起動方法
--------
1. 下記のファイルが同じフォルダに並ぶように展開します。
2. QuickFolderSize.exe をダブルクリックします。
3. パスを入力するか「ファイル > フォルダを開く...」で選び、
   「スキャン」をクリックします（パス欄で Enter でも開始します）。

必要なファイル（同じフォルダに置く）
------------------------------------
- QuickFolderSize.exe   本体（UIはEXEに埋め込み済み）
- WebView2Loader.dll    WebView2 Runtime への接続用ローダー
- readme.txt            英語版の説明
- readme_jp.txt         このファイル
- history.txt           更新履歴（英語）
- history_jp.txt        更新履歴（日本語）
- LICENSE.txt           MIT License（英語原文）
- LICENSE_jp.txt        MIT License（日本語参考訳）

任意:
- QuickFolderSize_cli.exe   コマンドライン版。「コマンドライン版」の
  節を参照。GUI本体の動作には不要で、依存ファイルもありません。
- mcp-server\               AIエージェント(Claude Code等)向けMCP
  サーバー。Node.jsと初回の"npm install"が必要です。
  mcp_readme_jp.txt を参照してください。
- mcp_readme.txt / mcp_readme_jp.txt   MCPサーバーの説明書。

完全性検証（SHA-256）
---------------------
配布用ZIPおよびバイナリの公式SHA-256ハッシュ値はCIビルド時に自動計算され、
GitHub Releasesの各リリースに SHA256SUMS.txt として添付・公開されています。
PowerShellでダウンロードファイルの整合性を確認できます:
  Get-FileHash .\QuickFolderSize-binary.zip -Algorithm SHA256

QuickFolderSize.exe を WebView2Loader.dll から
離して置かないでください。EXE は自分と同じフォルダから探します。

動作環境
--------
- Windows 10 / 11（64bit）
- Microsoft Edge WebView2 Runtime
- QuickFolderSize.exe（GUI）はデフォルトで管理者権限を要求し、起動時にUAC確認を表示します（QuickFolderSize_cli.exeは要求しません。「コマンドライン版」参照）

WebView2 Runtime について
-------------------------
Windows 11 には通常、WebView2 Runtime が入っています。
Windows 10 でも多くの環境には入っていますが、古い Windows 10、LTSC、
Windows Server、企業管理端末では入っていないことがあります。
ウィンドウが出ない場合は、Microsoft 公式の Microsoft Edge WebView2
Runtime（Evergreen）をインストールしてください。

WebView2Loader.dll は Runtime 本体ではなく、接続用のローダーです。

それでも起動しない場合は、EXE と同じ場所の QuickFolderSize_debug.log
を確認してください。

主な機能
--------
- NTFSドライブ直下では、管理者権限でMFT高速スキャンを使用
- フォルダサイズ・再帰ファイル数・更新日時の集計
- バックグラウンドスキャン（操作できるまま計測）
- 全階層の並列スキャン（ワーカー 32）
- 左下にスキャン経過時間を表示（0.2 秒ごとに更新）
- アドレスバーにドライブの使用量 / 総容量
- 左ペイン: ドライブとフォルダ（展開時に子を読み込み）
- 今回の結果に含まれるパスは、クリックしても再スキャンしない
  （再スキャンは F5）
- ジャンクション / マウントポイント等のリパースポイントは除外
- 更新日時が変わっていないフォルダは再スキャンを高速化
- レポート出力: Markdown（ファイル > レポート作成...）または JSON（ファイル > レポート作成(JSON)...）、CLI版と同一スキーマ
- 表示言語: 日本語 ⇔ English（メニューバー右端）
- 任意のCLI版（QuickFolderSize_cli.exe）。スキャン結果をJSONで標準出力、スクリプト・AIエージェント向け

コマンドライン版
----------------
QuickFolderSize_cli.exe は、スクリプトやAIエージェント向けの単体コンソール版です。依存ファイルはなく、管理者権限も要求しないためUACダイアログで止まりません。

使い方:
  QuickFolderSize_cli.exe <path> [--pretty] [--version]

- 成功時はJSONオブジェクトを1個、標準出力へUTF-8で出力します。
  失敗時は{"error": "..."}を標準エラー出力へ出し、終了コードは
  非0になります。
- 終了コード: 0=成功、1=パスが存在しない/ディレクトリでない。
- --pretty でインデント付きの整形出力（既定はコンパクトな1行）。
- --version でCLIのバージョンを表示して終了します。
- JSONのスキーマは、GUIの「ファイル > レポート作成(JSON)...」と
  同一です: path、scanned_at、total_size、subfolder_count、
  file_count_recursive、入れ子の"tree"。
- 管理者マニフェストを埋め込んでいないため、NTFS MFT高速経路は
  すでに管理者権限のシェルから起動した場合のみ使われます。
  それ以外は通常のフォルダスキャナへ自動的にフォールバックします
  （GUIと同じフォールバック）。
- 1回の実行ごとに独立したプロセスで、前回実行のキャッシュは
  持たないため、毎回フルスキャンになります。

例:
  QuickFolderSize_cli.exe C:\Users\me\Downloads > report.json

キーボードショートカット
------------------------
Ctrl+O          フォルダを開く
F5              再スキャン
Ctrl+Shift+S    Markdown レポート出力
Ctrl+Q          終了
Enter           スキャン開始（パス欄にフォーカスがあるとき）

画面
----
ダークなグラス風のウィンドウです。ほぼ黒の背景にすりガラスのカード、
シアン / 青のアクセント。スキャンボタンと割合バーはシアンから青への
グラデーションです。読めないフォルダは赤で表示されます。

更新履歴
--------
history_jp.txt を参照してください。

ライセンス
----------
本ソフトウェアは MIT License で提供されます。
英語原文は LICENSE.txt、日本語参考訳は LICENSE_jp.txt を確認してください。
法的な解釈では英語原文を優先します。

免責事項
--------
本ソフトウェアは現状有姿で提供されます。使用によって生じた損害について、
作者は責任を負いません。

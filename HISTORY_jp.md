# QuickFolderSize 変更履歴

## v3.1.0 — 2026-09-20

- フォルダ構成移行後のビルド・配布パスとリリース検証を修正。

[English HISTORY.md](HISTORY.md)

## バージョン命名規則

- 1桁目の更新：機能追加
- 2桁目の更新：バグ修正
- 3桁目の更新：その他の変更（ドキュメント修正など）

## v3.0.0（2026-09-14）

- `mcp-server/`を追加。`QuickFolderSize_cli.exe`をHTTP経由のMCP(Model Context Protocol)ツールとして公開し、Claude CodeのようなAIエージェントからGUI不要でスキャン結果を取得できるようにした。
- ツール: `server_status`(疎通確認)、`scan_folder`(同期スキャン)、`start_scan`/`get_scan_result`(非同期スキャン、大きいフォルダ向け)。
- MCPクライアント側のツール呼び出しタイムアウト(サーバー側5分より短い、未文書化)により、大きいフォルダで`scan_folder`が`session expired`エラーになる事象を確認、`start_scan`/`get_scan_result`の非同期パターンで回避する構成にした。
- 詳細は[`src/integrations/mcp-server/README.md`](src/integrations/mcp-server/README.md)、[docs/spec_jp.md](docs/spec_jp.md)10節を参照。

## v2.1.1（2026-08-24）

- 既存のMarkdownレポートに加え、JSONレポート出力(「ファイル > レポート作成(JSON)...」)を追加。CLI版の出力と同じスキーマ。
- スキャン結果をJSONで標準出力するCLI版(`QuickFolderSize_cli.exe`)を追加。スクリプト・AIエージェント向け。管理者権限を要求しないためUACで止まらない(管理者権限で実行した場合のみNTFS MFT高速経路が使われる)。
- バンドル済みUI HTMLをEXEへリソースとして埋め込み。起動時にディスクから`index.html`を読まなくなった。
- 配布パッケージから単体`engine_x64.dll`を削除(元々EXEからロードされておらず、スキャンエンジンはEXEに静的リンク済みだった)。
- 管理者権限でのNTFSボリューム直下MFT高速スキャンを追加。
- 起動時にUAC確認を表示し、MFT経路を標準で利用するよう変更。
- MFTを利用できない場合のWin32スキャン自動フォールバックを追加。
- MFT時のJSONをコンパクト化し、絶対パスをJavaScript側で復元。

## v2.0.1（2026-08-18）

- ネイティブC++17 + WebView2版をリリース（MinGW-w64 / g++）。
- ダークなグラスモーフィズムUI（HTML/CSS/バニラJS）。
- 全深度並列スキャン、mtimeキャッシュ、ジャンクション除外。
- 表示言語の日本語⇔English切替。
- スキャン時間カードに経過秒を表示（0.2秒ごとに更新）。
- 配布ドキュメント: readme.txt（英語）、readme_jp.txt（日本語）、history.txt / history_jp.txt。

## それ以前の開発（Python / PyQt6 プロトタイプ）

- **2026-07-15**: ヘルプメニューにバージョン情報ダイアログを追加。
- **2026-07-10**: スキャンの並列化を全階層対応に変更。NTFSジャンクション／マウントポイント対策、メモリ使用量を改善。
- **2026-06-02**: PyInstallerによるEXEパッケージング（旧Python版）。並列スキャンの強化、UI改善。
- **2026-05-29**: 初版プロトタイプ: フォルダスキャン・ツリー表示・Markdownレポート。

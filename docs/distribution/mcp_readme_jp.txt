QuickFolderSize - MCP サーバー
任意の開発者向けツール（本パッケージの mcp-server\ フォルダに同梱）

概要
----
QuickFolderSize_cli.exe をHTTP経由のMCP（Model Context Protocol）ツール
として公開する、Node.js製のサイドカープロセスです。Claude Codeのような
AIエージェントが、GUIを開かずにフォルダスキャンを直接呼び出せるように
します。

本体アプリ（QuickFolderSize.exe）とは別プロセスとして動作します。GUI・
CLIと違いこちらは自己完結の実行ファイルではなく、Node.jsのソースとして
同梱されています。ソース・最新情報は以下も参照してください。
  https://github.com/maktak-105/QuickFolderSize

起動方法
--------
1. Node.js（18以上推奨）をインストールします。
   https://nodejs.org/
2. mcp-server フォルダで依存パッケージを1回だけインストールします。
     cd mcp-server
     npm install
3. 以下を実行します。
     start-admin.bat
     （または start-admin.ps1）

管理者権限での起動が必須です。非管理者で起動すると、CLIがNTFS MFT
高速経路を使えず低速なWin32汎用列挙にフォールバックし、巨大フォルダの
スキャンでシステムに大きな負荷がかかります。

起動すると http://127.0.0.1:39391/mcp で待ち受けます（ポートは
QFS_MCP_PORT 環境変数で変更可）。管理者PowerShellウィンドウを閉じる
とサーバーも停止します。

提供ツール
----------
server_status
  このサーバープロセスが管理者権限で動いているかを返します。
  まず疎通確認に使います。

scan_folder(path, pretty?)
  path を再帰スキャンし、JSON全体を1回のレスポンスで返します。
  小〜中規模のフォルダ向けです。

start_scan(path, pretty?)
  スキャンをバックグラウンドで開始し、即座に scanId を返します。
  大きい・時間のかかるフォルダ向けです。

get_scan_result(scanId)
  start_scan の結果をポーリングします。完了したら、トップレベルの
  子フォルダ内訳など軽量な要約と、フルツリーJSONの保存先パスを
  返します。

運用上の注意（重要）
--------------------
Claude Code側のMCPツール呼び出しには、サーバー側の5分タイムアウト
よりずっと短い、明示されていない待機タイムアウトがあります。
scan_folderの処理が数十秒以上かかる大きいフォルダを対象にすると、
サーバーは正常に応答しているにもかかわらず、呼び出し側で
「MCP server "quickfoldersize" session expired」という汎用エラーに
なることがあります。小さいフォルダ（数百ファイル程度）は問題なく
成功します。

回避策は2つあります。

1. start_scan / get_scan_result の非同期パターンを使う（上記参照）。
   ツール呼び出し1回あたりの待機時間を短く保てます。

2. BashツールからHTTPエンドポイントを直接curlで叩く。Claude Code
   自身のツール呼び出しタイムアウトを完全に回避できます。

   例:
     node -e "require('fs').writeFileSync('req.json', JSON.stringify({
       jsonrpc:'2.0', id:1, method:'tools/call',
       params:{name:'scan_folder',
               arguments:{path:'C:/Users/me/AppData/Local', pretty:false}}
     }))"
     curl -s -X POST http://127.0.0.1:39391/mcp ^
       -H "Content-Type: application/json" ^
       -H "Accept: application/json, text/event-stream" ^
       --data-binary @req.json

   レスポンスはSSE形式（"data: {...}" 行にJSON-RPC結果）です。
   Node.jsで JSON.parse(dataLine).result.content[0].text をさらに
   JSON.parse すればスキャン結果が取れます。

   Windowsパスをリクエストに含める場合は「/」（フォワードスラッシュ）
   で書いてください。ツールの中間エスケープ処理で「\\」（二重
   バックスラッシュ）が化けることがあります。

3. 大きいフォルダの結果はレスポンス自体が数千万文字になることが
   あります（AppData\Local全体で1億文字超）。フルツリーをそのまま
   読み込まず、トップレベルの子フォルダのサイズだけを取り出す軽量な
   要約スクリプトを介して読んでください。

付属スクリプト
--------------
parse_result.js <curlの生出力.txt> <保存先.json> [topN]
  curlで直接叩いたSSEレスポンスをパースし、フルツリーをJSONファイル
  へ保存しつつ、トップレベル子フォルダの内訳を標準出力します。

drill.js <ツリーjson> <スラッシュ区切りのサブパス> [topN]
  保存済みのフルツリーJSONから、任意のサブフォルダの内訳だけを
  再取得します（再スキャン不要）。

コード変更後の反映
------------------
server.js を編集しても、動いているプロセスには反映されません。
管理者PowerShellウィンドウで Ctrl+C の後 start-admin.bat を再実行
してください。

さらに、Claude Code側はMCPサーバーのツール一覧をセッション開始時に
一度だけ取得してキャッシュします。サーバーを再起動してツールを追加・
変更しても、同じ会話内では新しいツールが見えません。新しいツールを
使うには新しい会話を始める必要があります（server_statusのような
既存ツールは同じ会話内でも再接続して使い続けられます）。

詳しいドキュメント
------------------
リポジトリ内の以下も参照してください。
- mcp-server/README.md      （このファイルの元になった詳細版）
- README_jp.md               「MCP サーバー」節
- document/spec_jp.md        10節（技術仕様）
- HISTORY_jp.md               v3.0.0（このツールを追加した経緯）

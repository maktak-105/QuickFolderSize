# MCPサーバーをsrc/integrationsへ移動 実施結果 v1.0

## 実施内容
- `mcp-server/`を`src/integrations/mcp-server/`へ移動。
- MCPサーバーが開発ツリーと配布ZIPのどちらでも`QuickFolderSize_cli.exe`を探索できるように修正。
- Release workflowは新しいソース位置から読み込み、ZIP内では`mcp-server/`として梱包する形に変更。
- README、仕様書、開発環境の構成図、変更履歴の参照先を更新。

## 確認
- `node --check src/integrations/mcp-server/server.js` 成功。
- `git diff --check` 成功（改行コードに関するGit警告のみ）。
- 旧ルート`mcp-server/`が存在しないこと、新しい配置と参照先が存在することを確認。
- QuickFolderSize本体のビルドおよびMCP実通信テストは未実施。

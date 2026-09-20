# MCPサーバーをsrc/integrationsへ移動 v1.0

## 目的
QuickFolderSizeのMCPサーバーをquick-app-templateのソース配置へまとめ、開発時・配布時の起動と参照を維持する。

## 変更計画
- `mcp-server/`を`src/integrations/mcp-server/`へ移動する。
- 開発時とフラット配布ZIP内の両方から`dist/QuickFolderSize_cli.exe`を解決できるようにする。
- README、仕様書、Releaseワークフローの参照元を更新する。ZIP内のフォルダ名は`mcp-server/`のままにする。
- 構文と差分を確認し、結果を記録する。

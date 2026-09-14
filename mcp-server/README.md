# QuickFolderSize MCP サーバー

`QuickFolderSize_cli.exe` をHTTP経由のMCP(Model Context Protocol)ツールとして公開するサイドカー。Claude Code のようなAIエージェントが、GUIを開かずにフォルダスキャンを呼び出せるようにする。

本体アプリ(`../dist/binary/`)とは別の Node.js プロセスとして動く(このディレクトリ自体の`package.json`はnpmパッケージとして独自にバージョン管理している)。ただしこの機能追加がプロジェクト全体をv3.0.0へ引き上げた変更点そのものなので、`../HISTORY.md`にはv3.0.0のエントリとして記録している。

## 起動

```
start-admin.bat   (または start-admin.ps1)
```

**管理者権限で起動すること。** 非管理者で起動すると `QuickFolderSize_cli.exe` がNTFS MFT高速経路を使えず、低速なWin32汎用列挙にフォールバックする。巨大フォルダ(`AppData\Local`全体など)を非管理者で舐めると、システムメモリを圧迫してClaude Desktopアプリ自体が強制再起動に追い込まれた実例がある。**巨大フォルダは必ず管理者権限で起動した状態のこのサーバー経由でスキャンし、CLIを直接非管理者シェルから叩かないこと。**

起動すると `http://127.0.0.1:39391/mcp` で待ち受ける(ポートは `QFS_MCP_PORT` 環境変数で変更可)。管理者PowerShellウィンドウを閉じるとサーバーも停止する。

`.claude.json` のuserスコープに `quickfoldersize` として登録すると、Claude Codeの新しい会話から自動でツールが使えるようになる(登録直後の会話には反映されない。登録は新しい会話開始時に読み込まれる)。

## 提供ツール

| ツール | 説明 |
|:-------|:-----|
| `server_status` | このサーバープロセスが管理者権限で動いているかを返す。まずこれで疎通確認するとよい |
| `scan_folder(path, pretty?)` | `path` を再帰スキャンしてJSON全体を1回のレスポンスで返す。小〜中規模フォルダ向け |
| `start_scan(path, pretty?)` | スキャンをバックグラウンドで開始し、即座に `scanId` を返す。大きい/時間のかかるフォルダ向け |
| `get_scan_result(scanId)` | `start_scan` の結果をポーリングする。完了したら軽量な要約(`top_level_children`)とフルJSONの保存先パス(`reportFile`)を返す |

`scan_folder`/`start_scan`/`get_scan_result` はいずれも同じ `QuickFolderSize_cli.exe` を子プロセスとして起動する(サーバー側タイムアウト5分)。`start_scan`は完了時にフルツリーを `scan-reports/<scanId>.json` へ書き出す(`.gitignore`済み)。

## 運用上の注意(重要)

**Claude Code側のMCPツール呼び出しには、サーバー側の5分タイムアウトよりずっと短い、明示されていない待機タイムアウトがある。** `scan_folder`の処理(CLIの実行)が数十秒以上かかる大きいフォルダを対象にすると、サーバーは正常に応答しているにもかかわらず、呼び出し側で `MCP server "quickfoldersize" session expired` という汎用エラーになる。小さいフォルダ(数百ファイル程度)は問題なく成功する。

回避策は2つ:

1. **`start_scan`/`get_scan_result`の非同期パターンを使う**(上記参照)。ツール呼び出し1回あたりの待機時間を短く保てる。
2. **BashツールからHTTPエンドポイントを直接curlで叩く。** Claude Code自身のツール呼び出しタイムアウトを完全に回避できる。

   ```bash
   node -e "const fs=require('fs'); fs.writeFileSync('req.json', JSON.stringify({
     jsonrpc:'2.0', id:1, method:'tools/call',
     params:{name:'scan_folder', arguments:{path:'C:/Users/me/AppData/Local', pretty:false}}
   }));"
   curl -s -X POST http://127.0.0.1:39391/mcp \
     -H "Content-Type: application/json" \
     -H "Accept: application/json, text/event-stream" \
     --data-binary @req.json
   ```

   レスポンスはSSE形式(`data: {...}` 行にJSON-RPC結果)。Node.jsで `JSON.parse(dataLine).result.content[0].text` をさらに `JSON.parse` すればスキャン結果が取れる。

   **Windowsパスをリクエストに含める場合は `/`(フォワードスラッシュ)で書くこと。** ヒアドキュメントや `node -e` の引用符経由だと `\\`(二重バックスラッシュ)が中間エスケープ処理で化けることがある(原因未特定)。`C:/Users/me/AppData/Local` のように書けば確実。

3. 大きいフォルダの結果はレスポンス自体が数千万文字になることがある(`AppData\Local`全体で1億文字超)。Claudeの1レスポンス文字数上限を超えると自動でファイル保存されるので、`node`で軽量な要約(トップレベル子フォルダのサイズだけ)を出すスクリプトを介して読むこと。フルツリーをそのまま読み込まない。

## 付属スクリプト

- `parse_result.js <curlの生出力.txt> <保存先.json> [topN]` — curlで直接叩いたSSEレスポンスをパースし、フルツリーをJSONファイルへ保存しつつ、トップレベル子フォルダの内訳を標準出力する
- `drill.js <ツリーjson> <スラッシュ区切りのサブパス> [topN]` — `parse_result.js`や`get_scan_result`が保存したフルツリーJSONから、任意のサブフォルダの内訳だけを再取得する(再スキャン不要)

## コード変更後の反映

`server.js` を編集しても、動いているプロセスには反映されない。管理者PowerShellウィンドウで `Ctrl+C` → `start-admin.bat` 再実行が必要。

さらに、Claude Code側はMCPサーバーのツール一覧をセッション開始時に一度だけ取得してキャッシュする。サーバーを再起動してツールを追加/変更しても、**同じ会話内では新しいツールが見えない。** 新しいツールを使うには新しい会話を始める必要がある(`server_status`のような既存ツールは同じ会話内でも再接続して使い続けられる)。

# 計画書: GitHub Actions の Node.js 24 対応(v1.0)

## 依頼

2026-11-26、v3.2.1 の公開用ワークフローで出た Node.js 20 の非推奨警告について説明したところ、ユーザーから「やっといて」との指示を受けた。

## 現状

警告(v3.2.1 の release.yml 実行時):
`Node.js 20 is deprecated. The following actions target Node.js 20 but are being forced to run on Node.js 24: actions/checkout@v4, softprops/action-gh-release@v2.`

| action | 今のバージョン | 使っている所 | 実行環境 |
|---|---|---|---|
| actions/checkout | v4 | ci / codeql / release | node20 |
| softprops/action-gh-release | v2 | release | node20(action.yml で確認) |
| github/codeql-action(init / analyze) | v3 | codeql | node20(action.yml で確認)。**v3 は 2026年12月に非推奨になる**(公式の CHANGELOG 4.31.3) |

## 変更内容と、各バージョンの変更点の確認(公式のリリースノートと action.yml)

1. `actions/checkout@v4` → `@v7`(最新は v7.0.1)
   - v5: Node 24 に移行。ランナーは v2.327.1 以上が必要(GitHub がホストするランナーなら問題ない)。
   - v6: 認証情報を別のファイルに保存するように変わった。このリポジトリでは、checkout のあとに git の認証情報を使う処理はない。
   - v7: `pull_request_target` / `workflow_run` での fork の PR の checkout を禁止した。このリポジトリのトリガーは push / pull_request / schedule / workflow_dispatch だけなので、影響はない。
2. `softprops/action-gh-release@v2` → `@v3`(最新は v3.0.4)
   - v3.0.0 は Node 24 への移行だけ。入力項目は v2 と同じ(action.yml の inputs を比べて確認した)。
     今使っている tag_name / name / files / overwrite_files / generate_release_notes / body は、v3 でも使える。
3. `github/codeql-action/init@v3` と `analyze@v3` → `@v4`
   - v4 では Node 24 で動く(CHANGELOG 4.30.7「[v4+ only] The CodeQL Action now runs on Node.js v24」)。そのほかの使い方は同じ。

## 検証

- PR で ci.yml(build)と codeql.yml(Analyze)が成功し、ログに Node.js 20 の警告が出ないこと。
- release.yml は、タグの push か手動実行でしか動かない。試すと公開リリースが作られたり上書きされたりするので、確かめ方はユーザーに確認する。

# 結果: GitHub Actions の Node.js 24 対応(v1.0)

計画書: `2026-11-26_GitHubActionsのNode24対応_v1.0.md`

## 実施内容

- `actions/checkout` v4 → v7(ci / codeql / release)
- `softprops/action-gh-release` v2 → v3(release)
- `github/codeql-action/init`・`analyze` v3 → v4(codeql)
- ワークフローの3ファイルとも、改行が CRLF のままであることを確認した。

## 検証(PR #22)

- build: 成功(1分35秒)、Analyze (cpp): 成功(4分28秒)、CodeQL: 成功。
- 両方のジョブで注釈(annotations)が0件だった。Node.js 20 の警告は出ていない。
  比べるために、v3.2.1 の release の実行(45760217823)の注釈も取得した。そこには `Node.js 20 is deprecated ...` の warning があり、
  この方法で警告を見つけられることを確認した。

## 未検証

- release.yml(action-gh-release v3 と checkout v7)は、タグの push か手動実行でしか動かないので、まだ実行していない。
  入力項目が v2 と同じことは action.yml で確認した。試すと公開リリースが作られたり上書きされたりするので、次のリリースで確かめるかどうかはユーザーの判断を待つ。

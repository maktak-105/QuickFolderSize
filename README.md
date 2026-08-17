# フォルダ使用容量ビューワー

ローカルドライブ・フォルダの使用容量を視覚的に把握するための Windows デスクトップアプリ（Python + PyQt6）。

バージョン: v1.0.0

## 主な機能

- 指定フォルダ以下を再帰スキャンし、フォルダ・ファイルのサイズを集計
- バックグラウンドスキャン（UIをブロックしない）
- スキャン結果をツリー表示（サイズ・割合バー・ファイル数・更新日時でソート可能）
- ドライブ／フォルダの左ナビペイン（遅延展開）
- mtime差分キャッシュによる再スキャン高速化
- Markdown形式でのフォルダ容量レポート出力
- NTFSジャンクション／マウントポイントを検出して再帰から除外（循環参照対策）
- 表示言語切り替え（日本語 ⇔ English、アドレスバー右端のトグルボタン）

## 起動方法

```powershell
pip install -r requirements.txt
cd python
python main.py
```

## EXEビルド

```powershell
build.bat
# → dist\assets\folder_viewer.exe （dist\assets\_internal\ に PyQt6 の DLL 一式が展開される）
```

`dist\assets\_internal\` は `folder_viewer.exe` と常に同じフォルダに置く必要がある。
配布時は `dist\assets\`（実行ファイル一式）と `dist\documents\`（readme.txt / history.txt）をまとめて渡す。

## スキャンの並列化

全ディレクトリ・全階層を単一の共有スレッドプール（最大32並列）で処理する。ワーカースレッドは子ディレクトリの完了をブロック待ちせず、非ブロッキングコールバックで完了を通知し合う設計のため、ツリーがどれだけ深くても並列度が落ちない（`python/scanner.py`）。

## ファイル構成

```
folder_viewer/
├── python/
│   ├── main.py        エントリーポイント
│   ├── scanner.py      FolderEntry / build_cache / ScanWorker（スキャン本体）
│   ├── model.py         FolderModel（QAbstractItemModel）+ SortProxyModel
│   ├── delegate.py      SizeBarDelegate（割合バー描画）
│   ├── navmodel.py      NavModel（左ナビペイン）
│   ├── mainwindow.py    MainWindow（UI全体）
│   └── utils.py         format_size() / generate_md_report()
├── document/
│   ├── spec.md          仕様書
│   └── environment.md   開発環境・ビルド手順
├── dist/
│   ├── assets/           EXE・DLL（build.bat が生成、Git管理外）
│   └── documents/        配布用 readme.txt / history.txt（Git管理）
├── requirements.txt
├── build.bat
├── folder_viewer.spec
└── CLAUDE.md             実装計画・Claude向け指示
```

## ドキュメント

- 詳細仕様 → [document/spec.md](document/spec.md)
- 開発環境・ビルド手順 → [document/environment.md](document/environment.md)
- 実装計画・作業ログ → [CLAUDE.md](CLAUDE.md)

## 動作環境

- Windows 10 / 11 (64bit)
- Python 3.13+
- PyQt6 >= 6.4.0

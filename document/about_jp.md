# QuickFolderSize バージョン情報

[English about.md](about.md)

## バージョン
Ver. v2.0.1

## 表示言語
画面右上のボタンで 日本語 ⇔ English を切り替え可能（メニュー・ステータス・レポート出力すべてに反映）

## 開発環境
- C++17（MinGW-w64 / g++、WinLibs MCF UCRT）
- WebView2（Microsoft Edge WebView2 Runtime）
- Win32 API（FindFirstFileW/FindNextFileW, IFileDialog, DwmSetWindowAttribute 等）

サードパーティのC++ライブラリ依存なし。フロントエンド(HTML/CSS/JS)もフレームワーク非依存。

## 旧実装(参照用)
Phase 1のPythonプロトタイプ(PyQt6ベース)は `python/` に残っている。

## 制作者
GitHub: maktak-105

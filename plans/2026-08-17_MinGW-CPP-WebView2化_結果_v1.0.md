# 結果: MinGW/C++ネイティブビルド＋WebView2化(v1.0)

対応する計画書: `2026-08-17_MinGW-CPP-WebView2化_v1.0.md`

## 実施内容

計画書どおり、Python/PyQt6実装を C++17(MinGW-w64) + WebView2 GUI へ移植した。

- **`core/native/engine.h` / `engine.cpp`**: スキャンエンジン新規実装。`scanner.py`の「単一共有スレッドプール・非ブロッキングfan-out」設計をC++の`ThreadPoolExecutor`相当(自作`ThreadPool`)+再帰fan-outで再現。`FindFirstFileW`/`FindNextFileW`列挙、FILETIME比較によるmtimeキャッシュ(ファイルは無条件再利用・サブディレクトリは個別に再検証)、`FILE_ATTRIBUTE_REPARSE_POINT`によるジャンクション/シンボリックリンク除外、ドライブ容量取得(`get_drives`)、ナビペイン用非再帰列挙(`list_subdirectories`)を実装
- **`core/native/webview_main.cpp`**: WebView2ホスト新規実装。QuickDiskBenchのCOMコールバック定型部分を流用しつつ、JSON WebMessageプロトコル(get_drives/browse/scan/nav_expand/export_report/quit)、`IFileDialog`によるフォルダ選択・レポート保存ダイアログ、スキャンワーカースレッド管理とmtimeキャッシュの保持(ミューテックス保護)を新規実装
- **`templates/index.html` / `static/css/style.css` / `static/js/app.js`**: フロントエンド新規実装。`mainwindow.py`+`model.py`+`navmodel.py`+`delegate.py`+`utils.py`+`i18n.py`のUIロジックをバニラJSへ1:1移植(ツリーテーブルの展開/折りたたみ・列ソート、左ナビペインの遅延展開、i18nテーブル、Markdownレポート生成)
- **`build_native.py` / `bundle_html.py`**: QuickDiskBenchのビルドスクリプトをベースに、CLI実行ファイルのビルドを除いた2点ビルド(DLL + GUI exe)へ簡略化して移植
- **`build.bat`**: `python build_native.py` を呼ぶだけの薄いラッパーへ置き換え
- **ドキュメント**: `CLAUDE.md` / `document/spec.md` / `document/environment.md` / `document/about.md` を新アーキテクチャに合わせて全面更新

計画どおり `python/` ディレクトリは削除せず参照実装として残した。名称変更・git操作は計画外のため実施していない。

## 検証結果

1. ✅ `build.bat`(= `python build_native.py`)が成功し、`dist/binary/QuickFolderSize.exe` / `engine_x64.dll` / `WebView2Loader.dll` / `index.html` が生成されることを確認
2. ✅ `QuickFolderSize.exe` を起動し、ウィンドウ表示・WebView2コンテンツ描画を実機で確認(スクリーンショットで検証)
3. ✅ `C:\Windows\System32\drivers` をスキャンし、直下1レベルの即時表示 → 完了後の数値埋め込みを確認。スキャン時間 `0.01s`、ドライブ容量ラベル `C:\ 186.0 GB / 235.0 GB` 表示も正常
4. ✅ ステータスバーの集計(`合計: 139.8 MB | フォルダ: 9 | ファイル: 621 | 直下ファイル: 471`)を確認。フォルダ合計サイズはツリー内の値と整合
5. — mtimeキャッシュによる再スキャン高速化は、対象フォルダが小さく初回から0.01sだったため体感差を確認できていない(機能自体は既存のPython版と同一ロジックで実装済み、大規模フォルダでの実測は未実施)
6. ✅ 左ナビペインのドライブ一覧・遅延展開フォルダツリー(C:\ (Windows)を展開しサブディレクトリ一覧が表示されること)を確認
7. ✅ ツリー行の展開/折りたたみ(UMDFフォルダを展開し子ファイルが相対サイズバー付きで表示されること)を確認
8. ✅ レポート作成: `File > Export Report...` → `IFileDialog`保存ダイアログ表示 → 保存 → 生成された`report_20260817_171021.md`の内容を確認。階層構造・サイズ・割合(%)とも正確
9. ✅ 言語切替ボタンでUI全文言(メニュー・ボタン・列ヘッダー・ステータスバー)が日本語⇔Englishに切り替わることを確認
10. ✅ `document/`配下の各ドキュメントを実装に合わせて更新済み

未検証(スコープ外・時間の都合): About ダイアログの表示、Ctrl+Q終了、巨大ドライブ(C:\ルート等)でのJSONメッセージサイズ実測。いずれも計画書の「リスク」節に記載済みの既知の確認項目。

## 残課題

- 巨大ツリー(C:\ルートなど数十万エントリ)でのJSONメッセージサイズ・パフォーマンスの実測(計画書「リスク」節を参照)
- About ダイアログ・Ctrl+Q終了など未実機確認の細部動作
- `python/`の今後の扱い(削除するか、参照実装として残し続けるか)はユーザー判断待ち

# フォルダ使用容量ビューワー 仕様書

## 1. アプリ概要

| 項目 | 内容 |
|:-----|:-----|
| 目的 | ローカルドライブ・フォルダの使用容量を視覚的に把握する |
| 対象OS | Windows 10 / 11 (64bit) |
| 実装言語 | Python 3.11+ + PyQt6（プロトタイプ） → C++ Qt6（最終版） |
| 起動方法 | `cd folder_viewer/python && python main.py` |

---

## 2. 画面構成

```
┌─ メニューバー ──────────────────────────────────────────────────────┐
│  ファイル(F) [フォルダを開く / 再スキャン / レポート作成 / 終了]          │
├─ アドレスバー ──────────────────────────────────────────────────────┤
│ [C:\ 150.3GB/512.0GB] [入力欄]  [参照...] [スキャン] [2.34s]        │
├─ 左ナビペイン ──────────┬─ 右スキャンツリー ───────────────────────────┤
│  ドライブ・フォルダ       │  名前 | サイズ | 割合 | ファイル数 | 更新日時    │
│  ツリー（遅延展開）       │  （スキャン結果ツリー）                        │
│                         │                                            │
└─────────────────────────┴────────────────────────────────────────────┤
│  ステータスバー: 合計サイズ | フォルダ数 | ファイル数（再帰）| 直下ファイル数    │
└───────────────────────────────────────────────────────────────────────┘
```

### 各エリアの役割

| エリア | 説明 |
|:-------|:-----|
| ドライブ容量ラベル | アドレスバー左端。選択フォルダのドライブの使用容量/総容量を表示（例: `C:\ 150.3 GB / 512.0 GB`） |
| 入力欄・ボタン | フルパスを直接入力またはダイアログで選択してスキャントリガー |
| スキャン時間ラベル | アドレスバー右端。スキャン中は `計測中...`、完了後に `X.XXs` を表示 |
| 左ナビペイン | ドライブ一覧と遅延展開フォルダツリー。クリックでスキャン自動実行 |
| 右スキャンツリー | スキャン結果の階層ツリー。ファイルもリーフノードとして表示。スキャン開始時点で新フォルダのファイル名一覧をプレースホルダー表示し、完了後にサイズ等を埋める |
| ステータスバー | スキャン中は進捗表示（スピナー）、完了後は集計統計を表示 |

---

## 3. 機能一覧

| 機能 | 説明 |
|:-----|:-----|
| フォルダスキャン | 指定パス以下を再帰スキャン、フォルダ・ファイルを集計 |
| バックグラウンドスキャン | QThread + ThreadPoolExecutor で UI をブロックしない |
| 並列スキャン | ルート直下サブディレクトリを `ThreadPoolExecutor(max_workers=8)` で同時スキャン |
| mtime 差分キャッシュ | 前回スキャン結果を保持。再スキャン時は `os.stat().st_mtime` を比較し、変化のないディレクトリは `scandir()` をスキップしてキャッシュから返す |
| プレースホルダー表示 | スキャン開始と同時に対象フォルダの直下1レベルをすぐに表示（サイズは空）。スキャン完了後に数値が埋まる |
| スキャン時間表示 | アドレスバー右端にスキャン所要時間を `X.XXs` で表示 |
| ドライブナビ | 左ペインにシステムドライブ一覧を表示（`GetLogicalDrives()`） |
| フォルダ遅延展開 | ナビツリーはクリック時に初めて子フォルダをロード |
| ツリー表示 | フォルダ（📁）とファイル（📄）を混在でツリー表示 |
| サイズバー | 親フォルダに対する割合をプログレスバーで可視化（COL_BAR 列） |
| 列ソート | 各列ヘッダークリックで昇順/降順ソート |
| 参照ダイアログ | フォルダ選択ダイアログで GUI からパスを選択 |
| ドライブ容量表示 | 選択フォルダのドライブの使用容量/総容量を `shutil.disk_usage()` で取得し表示 |
| レポート出力 | Markdown 形式で階層フォルダ容量レポートを保存 |
| アクセスエラー対応 | 権限不足フォルダはグレー表示でクラッシュしない |
| スピナー & 進捗表示 | スキャン中、150ms 周期のポーリングタイマーでワーカーの `current_path` を読み取り、ステータスバーに表示。クロススレッドシグナル排他ロック競合を回避 |

---

## 4. ファイル構成

```
folder_viewer/
├── python/
│   ├── main.py        エントリーポイント。QApplication 起動・Fusion テーマ適用
│   ├── scanner.py     FolderEntry / build_cache / ScanWorker (QThread)
│   ├── model.py       FolderModel (QAbstractItemModel) + SortProxyModel
│   ├── delegate.py    SizeBarDelegate — COL_BAR 列にプログレスバー描画
│   ├── navmodel.py    NavModel (QAbstractItemModel) — 左ナビペイン用ドライブ・フォルダツリー
│   ├── mainwindow.py  MainWindow (QMainWindow) — UI 全体の組み立て
│   └── utils.py       format_size() / generate_md_report()
├── document/
│   └── spec.md        本仕様書
├── cpp/               C++ 移植用（Phase 2）
└── build/             PyInstaller 出力先
```

---

## 5. データモデル

### FolderEntry（`scanner.py`）

スキャン結果のツリーノード。フォルダとファイル両方に使用。

| フィールド | 型 | 説明 |
|:-----------|:---|:-----|
| `name` | `str` | 表示名（フォルダ名またはファイル名） |
| `path` | `str` | フルパス |
| `size` | `int` | バイト数（フォルダは再帰合計） |
| `file_count` | `int` | 再帰ファイル総数（フォルダ）/ 0（ファイルノード） |
| `modified` | `datetime` | 最終更新日時（表示用） |
| `is_accessible` | `bool` | False の場合グレー表示 |
| `is_dir` | `bool` | True=フォルダノード、False=ファイルリーフノード |
| `children` | `list[FolderEntry]` | 子ノード（ディレクトリ降順→ファイル降順） |
| `parent` | `FolderEntry \| None` | 親ノード（ratio 計算に使用） |
| `loaded` | `bool` | スキャン完了フラグ |
| `_mtime_raw` | `float` | `os.stat().st_mtime` の生 float（キャッシュ比較専用） |

> `_mtime_raw` を設けた理由: NTFS のタイムスタンプは 100ns 精度だが `datetime` はマイクロ秒までしか保持しない。`datetime.fromtimestamp(ts).timestamp()` の往復で最大 900ns の誤差が生じ、`==` 比較がほぼ常に偽になるため、生 float で `abs(a - b) < 0.001` の比較を行う。

### NavNode（`navmodel.py`）

左ナビペインのツリーノード（フォルダ参照のみ、サイズ情報なし）。

| フィールド | 型 | 説明 |
|:-----------|:---|:-----|
| `name` | `str` | 表示名（ドライブ名またはフォルダ名） |
| `path` | `str` | フルパス |
| `parent` | `NavNode \| None` | 親ノード |
| `children` | `list[NavNode]` | 子フォルダ（アルファベット順） |
| `is_loaded` | `bool` | fetchMore 済みフラグ |

---

## 6. スキャン・キャッシュ動作フロー

```
フォルダ選択
  │
  ├─ _make_placeholder(path)   # 直下1レベルのみ即時表示（サイズ=0）
  │
  └─ ScanWorker 起動
       │
       ├─ _scan_root(path)      # ルート直下を常に再読み込み
       │    └─ ThreadPoolExecutor で各サブディレクトリを並列投入
       │
       └─ _scan_dir(subdir)
            ├─ os.stat().st_mtime を取得
            ├─ キャッシュあり & |cached._mtime_raw - mtime| < 0.001
            │    └─ _from_cache()  # scandir スキップ、子ディレクトリは再帰チェック
            └─ キャッシュなし / mtime 変化
                 └─ _full_scan()   # 通常の完全再帰スキャン

完了
  │
  ├─ _scan_cache.update(build_cache(root))  # 次回用にキャッシュ更新
  └─ スキャン時間ラベルを更新
```

---

## 7. ツリービュー列定義

右スキャンツリーの列（`model.py`）:

| 定数 | 番号 | 表示名 | 内容 |
|:-----|:-----|:-------|:-----|
| `COL_NAME` | 0 | 名前 | システムアイコン + ノード名 |
| `COL_SIZE` | 1 | サイズ | `format_size()` で KB/MB/GB 表示 |
| `COL_BAR` | 2 | 割合 | `SizeBarDelegate` でプログレスバー描画 |
| `COL_FILES` | 3 | ファイル数 | フォルダ: 再帰ファイル数、ファイル: `—` |
| `COL_MTIME` | 4 | 更新日時 | `%Y/%m/%d %H:%M` 形式 |

### カスタム Qt データロール

| 定数 | 値 | 用途 |
|:-----|:---|:-----|
| `ROLE_SORT` | 256 (`UserRole`) | 数値ソート用の raw 値を返す |
| `ROLE_RATIO` | 257 (`UserRole+1`) | `SizeBarDelegate` が取得する 0.0〜1.0 の割合 |

> PyQt6 では `Qt.ItemDataRole.UserRole + 1` の演算が不安定なため、`.value` で int に変換して定義する。

---

## 8. キーボードショートカット

| ショートカット | 機能 |
|:--------------|:-----|
| `Ctrl+O` | フォルダを開く（ダイアログ） |
| `F5` | 再スキャン |
| `Ctrl+Shift+S` | レポート作成（Markdown 保存） |
| `Ctrl+Q` | アプリ終了 |
| `Enter`（アドレスバー） | スキャン実行 |

---

## 9. Markdown レポートフォーマット仕様

`ファイル > レポート作成` で生成されるファイルの構造:

```markdown
# フォルダ使用容量レポート

| 項目 | 値 |
|:-----|:---|
| パス | `C:\path\to\folder` |
| スキャン日時 | 2025/01/15 10:30 |
| 合計サイズ | 1.2 GB |
| サブフォルダ数 | 42 |
| ファイル数（再帰） | 1,234 |

## フォルダ構成

サイズ降順・階層表示。ファイルは各フォルダ内にインデントで記載。

- 📁 **SubFolder** — 800.0 MB (66.7%)  ファイル: 500個
  - 📁 **Nested** — 400.0 MB (50.0%)  ファイル: 200個
    - 📄 largefile.bin — 200.0 MB (50.0%)
  - 📄 data.csv — 50.0 MB (6.3%)
- 📄 readme.txt — 4.0 KB (0.0%)

---
*生成日時: 2025/01/15 10:30:45*
```

### 記法ルール

- フォルダ: `- 📁 **名前** — サイズ (割合%)  ファイル: N個`
- ファイル: `- 📄 名前 — サイズ (割合%)`
- 割合は親ノードのサイズを分母に計算（ルート直下は root.size が分母）
- インデント: 深さ × スペース2個

---

## 10. パフォーマンス最適化の詳細

### スキャン速度（初回）
- **並列度**: ThreadPoolExecutor(max_workers=8) で root 直下のサブディレクトリを同時スキャン
- **GIL パターン**: `self.current_path = path` → `os.stat()` の順で、I/O 後に複数スレッドが GIL を競合
- **ポーリング方式**: ワーカースレッドからのクロススレッドシグナルを廃止。メインスレッドが 150ms 周期で `worker.current_path` を読み取る。これにより Qt イベントキューへのロック競合を完全に排除し、60-70s の高速スキャンを実現

### キャッシュ（再スキャン）
- `_mtime_raw` で NTFS 100ns 精度の mtime を保存
- キャッシュヒット時は `scandir()` をスキップ → 20s 程度に短縮

---

## 11. 今後の実装予定

### スキャン速度改善（追加検討）

| 案 | 内容 |
|:---|:-----|
| 深さ制限＋遅延展開 | depth パラメータ追加。depth >= N は展開クリック時にスキャン |
| 除外フィルター UI | `node_modules` / `.git` / `AppData\Local\Temp` をスキップするオプション |

### Phase 2: C++ Qt6 移植

| Python | C++ |
|:-------|:----|
| `os.scandir()` | `FindFirstFileW / FindNextFileW` |
| `ThreadPoolExecutor` | `QThreadPool + QRunnable` |
| `mtime` キャッシュ | `FILETIME` 比較（Win32 API） |
| `QAbstractItemModel` サブクラス | 同一 API |
| `QStyledItemDelegate` | 同一 API |
| `PyInstaller --onefile` | `windeployqt` + 静的リンク or NSIS |

### Phase 3: EXE パッケージング（Python版）

```powershell
pip install pyinstaller
pyinstaller --onefile --windowed --name folder_viewer python/main.py
# → dist/folder_viewer.exe
```

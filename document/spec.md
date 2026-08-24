# QuickFolderSize Specification

## 1. App overview

| Item | Details |
|:-----|:-----|
| Version | v2.1.1 |
| Purpose | Visualize how much space local drives and folders use |
| Target OS | Windows 10 / 11 (64-bit) |
| Implementation | C++17 (MinGW-w64 / g++) + WebView2 (HTML/CSS/JS) |
| Display language | Japanese / English (toggle button at the top-right of the menu bar; client-side only, resets to Japanese on restart) |
| How to run | Run `dist\binary\QuickFolderSize.exe` |

> Build environment / build steps → see `document/environment.md`

Fully ported from the Phase 1 (Python/PyQt6 prototype) as Phase 3. The UI logic and architecture follow `QuickDiskBench` in this workspace (C++/MinGW/WebView2, already proven). The old Python implementation remains in `python/prototype/` for reference.

---

## 2. Architecture

```
┌─────────────────────────────────────────────────────────┐
│ QuickFolderSize.exe (WinMain)                            │
│  ┌───────────────────────────────────────────────────┐  │
│  │ core/native/webview_main.cpp                       │  │
│  │  - WebView2 host (environment / controller init)    │  │
│  │  - JSON WebMessage protocol send/receive             │  │
│  │  - IFileDialog (folder picker, report save)          │  │
│  │  - Scan worker thread management, mtime cache        │  │
│  └───────────────────────────────────────────────────┘  │
│              │ call                        │ JSON postMessage │
│              ▼                             ▼             │
│  ┌─────────────────────┐      ┌─────────────────────┐   │
│  │ core/native/engine.cpp│      │  WebView2 (Chromium)│   │
│  │  - parallel scan engine│      │  index.html/app.js  │   │
│  │  - drive capacity      │      │  (tree view, i18n)  │   │
│  │  - folder listing (nav)│      │                     │   │
│  └─────────────────────┘      └─────────────────────┘   │
└─────────────────────────────────────────────────────────┘
```

- `engine.cpp` is not shipped as a standalone DLL; its source is statically linked directly into both the GUI executable (`QuickFolderSize.exe`) and the CLI executable (`QuickFolderSize_cli.exe`), the same approach QuickDiskBench uses. Both executables share the same `engine.cpp`/`engine.h`
- Native ↔ JS communication uses JSON string messages via `PostWebMessageAsJson` / `window.chrome.webview.postMessage` (see section 5 for the WebMessage protocol)
- At build time, `bundle_html.py` inlines `templates/index.html` + `static/css/style.css` + `static/js/app.js` into one bundled HTML (relative-path `<img>` sources are embedded as data URIs, since `NavigateToString` cannot resolve external resources), copies it to `core/native/index_embed.html`, and embeds it into the EXE as `RCDATA` via `QuickFolderSize.rc`. At startup the GUI simply reads it back with `FindResourceW`/`LoadResource` — it does not depend on an `index.html` file on disk

---

## 3. Screen layout

Dark glassmorphism (same family as QuickDiskBench). Near-black canvas with cyan/blue accents. There is no status bar.

```
┌─ File / Help ──────────────────────────────────────── [🌐 English] ─┐
├─ [C:\ (Label) 150.3 GB / 512.0 GB]  [path input]  [Scan] ───────────┤
├─ Drives / folders (lazy) ────┬─ Name | Size | Ratio | Files | Date ─┤
│                              │  📁 Users     254.6 GB  ████  62.4%  │
│  Scan Time  Done  23.73s     │  📄 pagefile   16.0 GB  ██    24.9%  │
└──────────────────────────────┴──────────────────────────────────────┘
```

| Area | Description |
|:-------|:-----|
| Menu bar | File (Open / Rescan / Export Report / Quit), Help (Help / About). The folder picker is reachable only through the menu or `Ctrl+O` (no Browse button on the address bar) |
| Language toggle | Top-right of the menu bar. Click to switch Japanese ⇔ English. Menus, column headers, buttons, About, and the Markdown report switch immediately, with no native round-trip. Resets to Japanese on restart |
| Drive capacity label | Left edge of the address bar. Shows used/total capacity with the volume label (e.g. `C:\ (Windows-SSD) 426.7 GB / 930.4 GB`) |
| Path input / Scan | Type a full path and scan. `Enter` also starts a scan |
| Left nav pane | Drive list (labeled) and a lazily-expanded folder tree. Click to scan, or switch the view instantly if the path is already inside the current scan result |
| Scan Time card | Below the left nav. While scanning, shows elapsed seconds since the button press, updated every 0.2 s as `X.XXs`. Once finished, shows `Done` (Japanese: `完了`) between the label and the seconds, and freezes that same elapsed time |
| Result tree (right pane) | Hierarchical result tree. Expand/collapse with ▶/▼. Click a header to sort (default: size, descending). Click a row to fill its path into the address bar |
| About dialog | Help → About. Shows version, development environment, and author, with `static/img/author.png` below (embedded into the bundled HTML) |
| Help dialog | Help → Help.... The raw text of `resources/help/help.md` (English) / `help_jp.md` (Japanese) is embedded into the bundled HTML at build time and rendered to HTML at runtime by a small custom Markdown-subset renderer, matching the current display language. Shown as a scrollable modal |

---

## 4. Feature list

| Feature | Description |
|:-----|:-----|
| Folder scan | Recursively scans the given path and totals folders and files |
| Background scan | A worker thread + `scan_directory()` keeps the UI thread unblocked |
| Full-depth parallel scan | A fixed-size thread pool (32 workers) is shared across every directory. Completion is signaled via non-blocking fan-out, so parallelism does not degrade with depth |
| NTFS MFT fast path | For an NTFS volume root such as `C:\`, reads the MFT directly from `\\.\X:` and reconstructs the tree from FILE record parent references, skipping directory traversal. The EXE launches with `requireAdministrator` |
| Scan-method fallback | If the MFT cannot be opened, or the target is non-NTFS, an individual folder, or a network path, automatically falls back to the classic `FindFirstFileW`/`FindNextFileW` approach |
| Junction handling | Detects `FILE_ATTRIBUTE_REPARSE_POINT` and excludes it from recursion (for both files and directories) |
| Incremental UI updates | Sends a `scan_progress` message to update the tree as each top-level directory finishes |
| mtime differential cache | The native side keeps the tree from the previous scan. On rescan, each directory's FILETIME is compared; unchanged directories skip re-enumeration and reuse the cache (files are always reused, subdirectories are individually re-verified) |
| Placeholder display | As soon as a scan starts, the first level under the target folder is shown immediately (sizes blank); values fill in once the scan completes |
| Scan time display | The bottom-left card shows the JS-side elapsed seconds since the Scan button was pressed, as `X.XXs`, updated every 0.2 s. It freezes on the same clock after completion, showing `Done` / `完了` in between. The native `elapsed_seconds` (covering only the `scan_directory` call itself) is not used for display, since it can appear to rewind once JSON serialization and transfer time are added |
| Drive nav | Lists system drives in the left pane (`GetLogicalDriveStringsW`), with the volume label in parentheses when present |
| Lazy folder expansion | The nav tree loads child folders via `nav_expand` only when clicked |
| Nav inside an already-scanned tree | Clicking a nav path that is already inside the current scan result fetches the matching node from the JS-held tree and switches the view instantly, without requesting a rescan from native |
| Tree view | Folders (📁) and files (📄) shown together in a tree, each row independently expandable/collapsible |
| Size bar | A progress bar visualizing the share of the parent folder's size |
| Column sort | Click any column header to sort ascending/descending (implemented in JS) |
| Folder picker | `IFileDialog` (`FOS_PICKFOLDERS`) for folder selection |
| Drive capacity display | Shows the selected folder's drive usage/total via `GetDiskFreeSpaceExW` |
| Report export (Markdown) | Generates a hierarchical folder-size report as Markdown and writes it via `IFileDialog` (save) |
| Report export (JSON) | Generates a report as JSON with `path`/`scanned_at`/`total_size`/`subfolder_count`/`file_count_recursive`/`tree`, written via `IFileDialog` (save). Same schema as the CLI build's (`QuickFolderSize_cli.exe`) JSON output |
| Access-error handling | A folder where `GetFileAttributesExW` / `FindFirstFileW` fails is marked `is_accessible=false` and shown in red (`#ef4444`) without crashing. Reparse points such as junctions are excluded from recursion rather than shown in red |
| Display language toggle | The `I18N` table in `app.js` manages every UI string (menus, buttons, column headers, About, scan-done label, Markdown report). Switches instantly via the toggle at the top-right of the menu bar, with no native round-trip; resets to Japanese on restart |
| About | Version, development environment, and author, plus the author image at the bottom of the dialog |
| Help viewer | Displays the Markdown source under `resources/help/` in an in-app modal, using a small self-written renderer with no external library (supports headings, paragraphs, emphasis, code, links, lists, and tables only) |
| CLI build | `QuickFolderSize_cli.exe <path>`. Returns JSON on stdout without going through the GUI (same schema as the JSON report export). Does not request administrator rights, so it never blocks non-interactive execution. See `README.md`/`README_jp.md` for details |

---

## 5. WebMessage protocol

JS → native uses `window.chrome.webview.postMessage({cmd: ..., ...})`; native → JS uses `PostWebMessageAsJson` (both directions auto-convert between JSON strings and JS objects).

### JS → native

| cmd | Parameters | Description |
|:----|:-----------|:-----|
| `get_drives` | none | Request the drive list |
| `browse` | `initial` | Open the folder-picker dialog |
| `scan` | `path` | Start a recursive scan of the given path (cancels any running scan first) |
| `nav_expand` | `path` | For the left nav pane: non-recursively list the immediate subdirectories |
| `export_report` | `format` (`md`/`json`), `lang` (`ja`/`en`) | The report body is not sent from JS. Native builds it directly from the scan result (`g_prevRoot`) and writes it via the save dialog (changed to this design in v2.1.1, since round-tripping a multi-ten-MB string through WebMessage for a tree with tens of thousands of nodes had become unusable) |
| `quit` | none | Quit the app |

### native → JS

| type | Contents |
|:-----|:-----|
| `drives` | Drive list (`path`/`label`/`total_gb`/`free_gb`/`used_gb`) |
| `browse_result` | The chosen folder path (empty string if cancelled) |
| `scan_placeholder` | The first-level listing right after a scan starts, plus the target drive's capacity info |
| `scan_progress` | The complete subtree (nested JSON) each time a top-level child folder finishes |
| `scan_finished` | The full root (nested JSON) once the scan completes, plus `elapsed_seconds` and `scan_method` (`mft` or `win32`; the engine's own seconds are not used for the UI's displayed time) |
| `scan_error` | Error notification when the given path does not exist or is not a folder |
| `nav_children` | Response to `nav_expand` (the immediate subfolder list) |
| `export_result` | Whether the report save succeeded, and the path |

Scan-result node JSON shape:

```json
{
  "name": "SubFolder", "path": "C:\\...\\SubFolder\\",
  "size": 838860800, "file_count": 500,
  "mtime_ms": 1737000000000,
  "is_accessible": true, "is_dir": true,
  "children": [ ... ]
}
```

---

## 6. Scan / cache flow

```
Folder selected (scan cmd received, UI thread)
  │
  ├─ If a scan is running: stop_flag=1 → join
  ├─ Synchronously list the first level → send scan_placeholder
  └─ Start a worker thread
       │
       └─ scan_directory(path, previous root, ...) [engine.cpp]
            ├─ NTFS volume root + administrator rights → MFT fast path (one batched notification on completion)
            └─ otherwise / MFT read failure → Win32 enumeration path
                 ├─ The root level is always re-enumerated with FindFirstFileW/FindNextFileW
            │    Each subdirectory's ScanNode() is queued to the thread pool (non-blocking)
            │
            └─ Each directory task, ScanNode()
                 ├─ Reads mtime (FILETIME) via GetFileAttributesExW
                 ├─ Cache present & mtime matches
                 │    └─ FromCacheAndFanOut() (files reused, subdirectories individually re-verified recursively)
                 └─ No cache / mtime changed
                      └─ FullScanAndFanOut() (re-enumerated via FindFirstFileW etc.)
                 │
                 └─ Once every child task finishes (pending count reaches 0), the node is finalized
                      → if it is a top-level child, it is serialized on the spot and sent as scan_progress (borrowed pointer)
                      → once the whole root finishes, scan_finished is sent + the native-side cache tree is updated

Completion
  │
  ├─ Native side: frees the previous cache tree and keeps this run's tree for next time
  └─ JS side: freezes the elapsed seconds since the button press as the completed display, registers the received tree into pathIndex, and redraws the table
```

Because the MFT fast path reads NTFS FILE records directly, the EXE shows a UAC prompt at startup to obtain administrator rights. On the MFT path, no progress JSON is sent for large subtrees — only a single completion message. The MFT JSON also omits each node's absolute path; the JS side reconstructs it from the parent path.

A cancelled scan (e.g. switching to a different folder mid-scan) discards its result and does not update the cache, matching the Python version's behavior.

Directory entries discovered during a parallel scan that carry `FILE_ATTRIBUTE_REPARSE_POINT` are excluded before being added to the recursion set (protection against junctions/symlinks).

---

## 7. Keyboard shortcuts

| Shortcut | Action |
|:--------------|:-----|
| `Ctrl+O` | Open folder (dialog) |
| `F5` | Rescan |
| `Ctrl+Shift+S` | Export report (save as Markdown) |
| `Ctrl+Q` | Quit |
| `Enter` (address bar) | Start scan |

All implemented via a `keydown` listener in `app.js` (entirely inside the WebView2 page; no native accelerator table is used).

---

## 8. Markdown report format

Structure of the file produced by `File > Export Report...` (headings and other strings follow the display language at the time of export; the example below is in Japanese):

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

As of v2.1.1 this is generated natively (`BuildMdReport()`/`RenderMdNode()` in `webview_main.cpp`) directly from the cached scan tree (`g_prevRoot`) and written straight to the chosen file (UTF-8, no BOM) — it is no longer built in JS and sent over WebMessage; see section 5.

---

## 9. Performance

### Parallelization design

A fixed-size thread pool (32 workers; `ThreadPool` in `core/native/engine.cpp`) is shared across every depth and every directory. Each task either "handles its own share and returns immediately" or "re-queues its child directories to the pool and returns immediately" — it never blocks waiting for another task to finish (the `pending` counter reaching 0 lets the last-finishing thread finalize the parent node). So no worker ever stalls regardless of tree depth.

### Cache (rescans)

- A directory's mtime is compared as a raw 64-bit FILETIME (100 ns precision), so the floating-point tolerance the Python version needed (`abs(a-b)<0.001`) is unnecessary
- On a cache hit, that directory's enumeration (`FindFirstFileW`) is skipped. Files are always reused; subdirectories are individually re-verified recursively (so a change several levels deep is still detected correctly)
- The previous cache tree is freed and replaced after every completed scan (it does not accumulate)
- A cancelled scan's result is not reflected in the cache

### Report generation (v2.1.1)

Report content (both Markdown and JSON) is built natively from the already-in-memory scan tree and written directly to the file, instead of being generated in JS and round-tripped through a WebMessage. For a tree with roughly 190,000 nodes (`C:\Windows`-scale), the previous JS-generation + WebMessage-transfer + hand-rolled native JSON parsing pipeline produced a payload around 56 MB and became effectively unusable; native generation eliminates that transfer entirely.

---

## 10. Planned

| Idea | Description |
|:---|:-----|
| Exclusion filter UI | An option to skip `node_modules` / `.git` / `AppData\Local\Temp` |
| Depth limit + lazy expansion | For very large trees, consider scanning past a certain depth only when the user expands that node, to control JSON growth |

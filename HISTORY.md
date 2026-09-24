# QuickFolderSize Changelog

## v3.2.0 — 2026-09-24

- Fixed the MFT fast path (run as administrator) leaving `path` empty for every node in CLI output and in the JSON/Markdown reports.
- Fixed the MFT fast path reporting a size of 0 for files with an `$ATTRIBUTE_LIST` (such as large, heavily fragmented files). On C:\ about 10,000 files (25GB) were missing from the totals. The same cause also made some entries show 8.3 short names and dropped files whose names were only in extension records; both are fixed.
- Fixed cloud folders such as OneDrive being left out of the tree entirely because they are reparse points. Only junctions, symbolic links, and other name surrogates are now excluded. Online-only files are included in file counts with a size of 0.
- After the fix, the C:\ total closely matches the user-file allocation from `fsutil volume allocationReport` (181.56GB vs 182.0GB, 1,300,498 vs 1,300,477 files).

## v3.1.0 — 2026-09-20

- Fixed the project restructure, build/package paths, and release verification.

[日本語版 HISTORY_jp.md](HISTORY_jp.md)

## Versioning rules

- First digit: new features
- Second digit: bug fixes
- Third digit: other changes, such as documentation updates

## v3.0.0 (2026-09-14)

- Added `mcp-server/`, exposing `QuickFolderSize_cli.exe` as an HTTP MCP (Model Context Protocol) tool so AI agents like Claude Code can pull scan results without opening the GUI.
- Tools: `server_status` (connectivity check), `scan_folder` (synchronous scan), `start_scan`/`get_scan_result` (async scan, for large folders).
- Observed `scan_folder` failing with a `session expired` error on large folders due to an undocumented MCP-client-side tool-call timeout (shorter than the server's 5-minute timeout); worked around it with the `start_scan`/`get_scan_result` async pattern.
- Details: [`src/integrations/mcp-server/README.md`](src/integrations/mcp-server/README.md), [docs/spec.md](docs/spec.md) section 10.

## v2.1.1 (2026-08-24)

- Added a JSON report export ("File > Export Report (JSON)...") alongside the existing Markdown report, sharing the same schema as the new CLI output.
- Added a CLI (`QuickFolderSize_cli.exe`) that scans a path and prints the result as JSON on stdout, for scripts and AI agents. It does not request administrator rights, so it never blocks on a UAC prompt; the NTFS MFT fast path is only used when the CLI happens to be run elevated.
- Embedded the bundled UI HTML into the EXE as a resource. The EXE no longer reads `index.html` from disk at startup.
- Removed the standalone `engine_x64.dll` from the distribution package (it was never loaded by the EXE; the scan engine has always been statically linked into it).
- Added an NTFS volume-root MFT fast path for elevated scans.
- The EXE now requests administrator rights at startup so the MFT path is the default.
- Added automatic Win32 scanner fallback when MFT access is unavailable.
- Reduced MFT JSON payloads by reconstructing absolute paths in JavaScript.

## v2.0.1 (2026-08-18)

- Native C++17 + WebView2 release (MinGW-w64 / g++).
- Dark glassmorphism UI (HTML/CSS/vanilla JS).
- Parallel full-depth scan, mtime cache, junction skip.
- Japanese / English language toggle.
- Scan Time card shows live elapsed seconds (updates every 0.2s).
- Distribution docs: readme.txt (English), readme_jp.txt (Japanese), history.txt / history_jp.txt.

## Earlier development (Python / PyQt6 prototype)

- **2026-07-15**: Added an About dialog to the Help menu.
- **2026-07-10**: Parallelized scanning across all depths; improved NTFS junction/mount-point handling and lowered memory use.
- **2026-06-02**: PyInstaller EXE packaging (legacy Python build); stronger parallel scan and UI updates.
- **2026-05-29**: First prototype: folder scan, tree view, Markdown report.

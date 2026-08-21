# QuickFolderSize Changelog

[日本語版 HISTORY_jp.md](HISTORY_jp.md)

## Versioning rules

- First digit: new features
- Second digit: bug fixes
- Third digit: other changes, such as documentation updates

## Unreleased

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

QuickFolderSize - Folder Size Viewer
Distribution package  v1.0.0

GitHub
------
https://github.com/maktak-105/QuickFolderSize

What it does
------------
QuickFolderSize scans a local folder or drive and shows how much space
each folder and file uses. Results appear in a sortable tree with size
ratio bars. You can export a Markdown report.

This is a native Windows app (C++ + WebView2). It does not require
Python or Qt.

How to start
------------
1. Extract the package so that the files below stay in the SAME folder.
2. Double-click QuickFolderSize.exe.
3. Type a path or use File > Open Folder..., then click Scan (or press Enter).

Required files (keep together)
------------------------------
- QuickFolderSize.exe   Main application
- engine_x64.dll        Standalone scan-engine DLL (not loaded by the EXE)
- WebView2Loader.dll    Connects the app to WebView2 Runtime
- index.html            UI (CSS and JavaScript are already inlined)
- readme.txt            This file (English)
- readme-jp.txt         Japanese version of this file
- history.txt           Change log (English)
- history_jp.txt        Change log (Japanese)
- LICENSE.txt           MIT License (English original)
- LICENSE_jp.txt        MIT License (Japanese translation)

Do not move QuickFolderSize.exe away from WebView2Loader.dll and
index.html. The EXE looks for both in its own folder.

Requirements
------------
- Windows 10 / 11 (64-bit)
- Microsoft Edge WebView2 Runtime

WebView2 Runtime
----------------
Windows 11 usually includes WebView2 Runtime. Many Windows 10 PCs have
it as well. Older Windows 10, LTSC, Windows Server, and managed PCs may
not. If the window does not appear, install Microsoft Edge WebView2
Runtime (Evergreen) from Microsoft.

WebView2Loader.dll is only a loader. It is not the Runtime itself.

If startup still fails, open QuickFolderSize_debug.log next to the EXE.

Features
--------
- Recursive size totals, file counts, and last-modified times
- Background scan; the window stays usable
- Parallel scan of all directory depths (32 workers)
- NTFS volume-root MFT fast path (the EXE requests administrator rights at startup)
- Automatic fallback to the regular Windows API scanner for non-NTFS, folder, or network scans
- Live elapsed time under the left pane, updated every 0.2 seconds
- Drive used / total capacity on the address bar
- Left pane: drives and folders (folders load when you expand them)
- Clicking a path already in the current result switches the view
  without scanning again (F5 forces a rescan)
- Junctions, mount points, and other reparse points are skipped
- Faster rescan when folder timestamps have not changed
- Markdown report (File > Export Report...)
- Language toggle: Japanese / English (top-right of the menu bar)

NTFS fast path
--------------
When scanning a volume root such as C:\, QuickFolderSize reads NTFS MFT records directly and reconstructs the tree without opening every directory. The EXE requests administrator rights at startup, which triggers a UAC confirmation. If the volume is not NTFS, the target is an individual folder or network path, the regular FindFirstFileW / FindNextFileW scanner is used instead.

Keyboard shortcuts
------------------
Ctrl+O          Open folder
F5              Rescan
Ctrl+Shift+S    Export Markdown report
Ctrl+Q          Exit
Enter           Start scan (when the path box is focused)

UI
--
Dark glass-style window: near-black background, frosted cards, cyan/blue
accents. The Scan button and the ratio bars use a cyan-to-blue gradient.
Folders you cannot read are shown in red.

Change log
----------
See history.txt.

License
-------
This software is provided under the MIT License. See LICENSE.txt
(English original) or LICENSE_jp.txt (Japanese translation).

Disclaimer
----------
This software is provided as-is. The author is not responsible for any
loss or damage arising from its use.

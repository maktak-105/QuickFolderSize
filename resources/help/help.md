# QuickFolderSize Operation Manual

Ver. v3.0.0

## 1. Overview

QuickFolderSize scans local drives and folders and shows the disk usage of each folder and file as a sortable tree, with a percentage bar for each row. It sorts by size (largest first) and can export the current result as a Markdown report.

The root of an NTFS drive is scanned through a fast path that reads the Master File Table (`$MFT`) directly, which is faster than a normal folder walk. This path requires administrator rights.

## 2. Starting the app

Double-click `QuickFolderSize.exe` to launch it. A UAC (User Account Control) prompt appears at startup — choose "Yes" to continue. Choosing "No" closes the app immediately; the main window never opens.

Administrator rights are required because the fast scan path reads `$MFT` directly from the NTFS volume root. The app is built to always run elevated, so the main window only opens once administrator rights have been granted.

Separately, even when running elevated, any scan target that is not the root of an NTFS volume (an individual folder, a non-NTFS drive, or a network path) automatically uses the regular folder enumeration method (`FindFirstFileW`) instead of the MFT fast path. This fallback depends on the target path, not on the UAC choice.

## 3. Screen layout

| Area | Description |
|:-----|:-------------|
| Menu bar | "File" (Open / Rescan / Export Report / Quit), "Help" (Help / About) |
| Language toggle | Top-right of the menu bar. Click to switch between Japanese and English (resets to Japanese on next launch) |
| Address bar | Drive usage / total capacity on the left, path input in the center, "Scan" button on the right |
| Left navigation pane | Drive list and folder tree (click to expand) |
| Scan time card | Below the navigation pane. Shows elapsed seconds since the scan started, and freezes once the scan finishes |
| Result tree (right pane) | Hierarchical scan result with Name, Size, percentage bar, File count, and Modified date columns |

## 4. Scanning a folder

There are three ways to start a scan:

1. Type a full path into the address bar and click "Scan" or press `Enter`
2. Use "File > Open..." (`Ctrl+O`) to pick a folder from the folder-selection dialog
3. Click a drive or folder in the left navigation pane

As soon as a scan starts, the first level of the target folder is shown immediately (sizes left blank). Sizes fill in as the scan progresses, and the full tree is finalized when the scan completes.

Clicking a folder in the left navigation pane that is already inside the current scan result reuses the held result instead of rescanning. To force a fresh rescan, use the "Scan" button, "File > Rescan", or `F5` — all three trigger the same rescan of the path currently shown in the address bar.

## 5. Reading the scan result

| Column | Description |
|:-------|:-------------|
| Name | 📁 folder / 📄 file. Clicking a row fills its path into the address bar |
| Size | Total size of everything under the folder |
| Percentage bar | Share of the parent folder's total size |
| File count | Recursive file count under the folder |
| Modified | Last-modified timestamp |

- Click a column header to sort ascending/descending by that column (default: size, descending).
- Click the ▶ / ▼ next to a folder name to expand or collapse it.
- Folders that cannot be read (e.g. insufficient permissions) are shown in red and skipped — the app does not crash on them.
- Reparse points such as junctions and symbolic links are excluded from the scan to avoid infinite loops.

## 6. Rescanning and caching

Rescanning a previously scanned folder (via the "Scan" button, "File > Rescan", or `F5`) skips re-enumerating any subfolder whose modified time has not changed since the last scan, reusing the cached result instead. Only folders that actually changed are re-read, so repeated scans are faster than the first one.

## 7. Exporting a report

The "File" menu has two report exports. Both cover the currently displayed scan result, with the save location chosen via a dialog.

- **Export Report...** (`Ctrl+Shift+S`): a Markdown file for people to read. Includes the path, scan timestamp, total size, subfolder count, file count, and a size-sorted folder listing.
- **Export Report (JSON)...**: a JSON file for other tools, scripts, or AI agents to parse. Carries the same information as the Markdown version, structured for machine reading.

Both menu items are only enabled once a scan has finished.

## 8. Switching the display language

Click the 🌐 button at the top-right of the menu bar to switch between Japanese and English instantly. This affects menus, column headers, buttons, the About dialog, and the Markdown report text. The switch is local to the running session and resets to Japanese the next time the app is started.

## 9. Keyboard shortcuts

| Shortcut | Action |
|:---------|:-------|
| `Ctrl+O` | Open folder (selection dialog) |
| `F5` | Rescan |
| `Ctrl+Shift+S` | Export report (save as Markdown) |
| `Ctrl+Q` | Quit |
| `Enter` | Start scan when the address bar has focus |

## 10. About

"Help > About..." shows the app version, development environment, and author information.

## 11. Troubleshooting

- If no window appears, verify that the Microsoft Edge WebView2 Runtime is installed.
- Keep `QuickFolderSize.exe` in the same folder as `WebView2Loader.dll`. The app looks for it next to itself and will not start if it is missing (the UI is embedded in the EXE, so `index.html` is not needed).
- If the app still does not start, check `QuickFolderSize_debug.log`, written next to the EXE.
- If you choose "No" at the UAC prompt, the app will not start. Launch it again and choose "Yes".

## 12. License

This software is provided under the MIT License. See `LICENSE.txt` (original English text) and `LICENSE_jp.txt` (Japanese reference translation) in the distribution package for details.

GitHub: https://github.com/maktak-105/QuickFolderSize

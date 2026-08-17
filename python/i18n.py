from __future__ import annotations

APP_VERSION = "v1.0.0"

_LANGS = ("ja", "en")
_lang = "ja"

_STRINGS: dict[str, dict[str, str]] = {
    "window_title": {"ja": "フォルダ使用容量ビューワー", "en": "Folder Size Viewer"},
    "addr_placeholder": {"ja": "スキャンするフォルダのパスを入力...", "en": "Enter the folder path to scan..."},
    "btn_browse": {"ja": "参照...", "en": "Browse..."},
    "btn_scan": {"ja": "スキャン", "en": "Scan"},

    "menu_file": {"ja": "ファイル(&F)", "en": "&File"},
    "act_open": {"ja": "フォルダを開く(&O)...", "en": "&Open Folder..."},
    "act_rescan": {"ja": "再スキャン(&R)", "en": "&Rescan"},
    "act_report": {"ja": "レポート作成(&E)...", "en": "&Export Report..."},
    "act_quit": {"ja": "終了(&X)", "en": "E&xit"},
    "menu_help": {"ja": "ヘルプ(&H)", "en": "&Help"},
    "act_about": {"ja": "バージョン情報(&A)...", "en": "&About..."},

    "about_title": {"ja": "バージョン情報", "en": "About"},
    "about_html": {
        "ja": (
            "<h3>フォルダ使用容量ビューワー</h3>"
            "<p><b>Ver. {version}</b></p>"
            "<hr>"
            "<p><b>【開発環境】</b><br>"
            "・Python 3.13.12<br>"
            "・PyQt6 >= 6.4.0 (GUIフレームワーク)<br>"
            "・PyInstaller >= 6.0.0 (EXEパッケージング)<br>"
            "・Pillow >= 10.0.0 (画像変換ライブラリ)</p>"
            "<p><b>【標準ライブラリ】</b><br>"
            "os, shutil, concurrent.futures, ctypes, string, dataclasses, datetime</p>"
            "<p><b>【制作者】</b><br>"
            "0120025-Z100</p>"
        ),
        "en": (
            "<h3>Folder Size Viewer</h3>"
            "<p><b>Ver. {version}</b></p>"
            "<hr>"
            "<p><b>[Development Environment]</b><br>"
            "- Python 3.13.12<br>"
            "- PyQt6 >= 6.4.0 (GUI framework)<br>"
            "- PyInstaller >= 6.0.0 (EXE packaging)<br>"
            "- Pillow >= 10.0.0 (image conversion library)</p>"
            "<p><b>[Standard Library]</b><br>"
            "os, shutil, concurrent.futures, ctypes, string, dataclasses, datetime</p>"
            "<p><b>[Author]</b><br>"
            "0120025-Z100</p>"
        ),
    },

    "status_idle": {"ja": "フォルダを選択してスキャンしてください", "en": "Select a folder and start a scan"},
    "status_scan_start": {"ja": "スキャン開始: {path}", "en": "Scan started: {path}"},
    "status_scanning": {"ja": "スキャン中: {path}", "en": "Scanning: {path}"},
    "status_result": {
        "ja": "合計: {size}  |  フォルダ: {dirs:,}  |  ファイル: {files:,}  |  直下ファイル: {topfiles:,}",
        "en": "Total: {size}  |  Folders: {dirs:,}  |  Files: {files:,}  |  Top-level files: {topfiles:,}",
    },
    "status_error": {"ja": "エラー: {msg}", "en": "Error: {msg}"},
    "scan_time_measuring": {"ja": "計測中...", "en": "Measuring..."},

    "err_title": {"ja": "エラー", "en": "Error"},
    "err_folder_not_found": {"ja": "フォルダが見つかりません:\n{path}", "en": "Folder not found:\n{path}"},
    "dlg_browse_title": {"ja": "フォルダを選択", "en": "Select Folder"},

    "report_title": {"ja": "レポート作成", "en": "Export Report"},
    "report_need_scan": {"ja": "先にスキャンを実行してください。", "en": "Please run a scan first."},
    "report_save_title": {"ja": "レポートを保存", "en": "Save Report"},
    "report_filter": {"ja": "Markdown ファイル (*.md);;すべてのファイル (*)", "en": "Markdown Files (*.md);;All Files (*)"},
    "report_done_title": {"ja": "レポート作成完了", "en": "Report Created"},
    "report_done_text": {"ja": "レポートを保存しました:\n{path}", "en": "Report saved:\n{path}"},
    "report_save_err_title": {"ja": "保存エラー", "en": "Save Error"},
    "report_save_err_text": {"ja": "保存に失敗しました:\n{err}", "en": "Failed to save:\n{err}"},

    "col_name": {"ja": "名前", "en": "Name"},
    "col_size": {"ja": "サイズ", "en": "Size"},
    "col_ratio": {"ja": "割合", "en": "Ratio"},
    "col_files": {"ja": "ファイル数", "en": "Files"},
    "col_mtime": {"ja": "更新日時", "en": "Modified"},

    "report_h1": {"ja": "# フォルダ使用容量レポート", "en": "# Folder Size Report"},
    "report_col_item": {"ja": "項目", "en": "Item"},
    "report_col_value": {"ja": "値", "en": "Value"},
    "report_path": {"ja": "パス", "en": "Path"},
    "report_scan_datetime": {"ja": "スキャン日時", "en": "Scan Date"},
    "report_total_size": {"ja": "合計サイズ", "en": "Total Size"},
    "report_subfolder_count": {"ja": "サブフォルダ数", "en": "Subfolders"},
    "report_file_count_recursive": {"ja": "ファイル数（再帰）", "en": "Files (recursive)"},
    "report_h2_structure": {"ja": "## フォルダ構成", "en": "## Folder Structure"},
    "report_structure_desc": {
        "ja": "サイズ降順・階層表示。ファイルは各フォルダ内にインデントで記載。",
        "en": "Sorted by size (descending), shown hierarchically. Files are indented within each folder.",
    },
    "report_file_suffix": {"ja": "ファイル: {count:,}個", "en": "Files: {count:,}"},
    "report_generated_at": {"ja": "生成日時", "en": "Generated at"},
}


def get_lang() -> str:
    return _lang


def set_lang(lang: str) -> None:
    global _lang
    if lang in _LANGS:
        _lang = lang


def toggle_lang() -> str:
    set_lang("en" if _lang == "ja" else "ja")
    return _lang


def other_lang_label() -> str:
    """Label for the toggle button: shows the language you'd switch TO."""
    return "EN" if _lang == "ja" else "日本語"


def tr(key: str, **kwargs) -> str:
    template = _STRINGS[key][_lang]
    return template.format(**kwargs) if kwargs else template

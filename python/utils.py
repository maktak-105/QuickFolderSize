from __future__ import annotations
from datetime import datetime


def format_size(size_bytes: int) -> str:
    if size_bytes < 0:
        return "—"
    for unit, threshold in (("GB", 1 << 30), ("MB", 1 << 20), ("KB", 1 << 10)):
        if size_bytes >= threshold:
            return f"{size_bytes / threshold:.1f} {unit}"
    return f"{size_bytes} B"


def generate_md_report(root, scan_time: datetime | None = None) -> str:
    """Generate a Markdown report from a FolderEntry tree."""
    if scan_time is None:
        scan_time = datetime.now()

    dir_count = sum(1 for c in root.children if c.is_dir)
    lines = [
        "# フォルダ使用容量レポート",
        "",
        f"| 項目 | 値 |",
        f"|:-----|:---|",
        f"| パス | `{root.path}` |",
        f"| スキャン日時 | {scan_time:%Y/%m/%d %H:%M} |",
        f"| 合計サイズ | {format_size(root.size)} |",
        f"| サブフォルダ数 | {dir_count:,} |",
        f"| ファイル数（再帰） | {root.file_count:,} |",
        "",
        "## フォルダ構成",
        "",
        "サイズ降順・階層表示。ファイルは各フォルダ内にインデントで記載。",
        "",
    ]

    def _render(node, depth: int, parent_size: int):
        indent = "  " * depth
        ratio  = node.size / parent_size * 100 if parent_size > 0 else 0.0
        if node.is_dir:
            lines.append(
                f"{indent}- 📁 **{node.name}**"
                f" — {format_size(node.size)} ({ratio:.1f}%)"
                f"  ファイル: {node.file_count:,}個"
            )
            for child in node.children:
                _render(child, depth + 1, node.size)
        else:
            lines.append(
                f"{indent}- 📄 {node.name}"
                f" — {format_size(node.size)} ({ratio:.1f}%)"
            )

    for child in root.children:
        _render(child, 0, root.size)

    lines += ["", f"---", f"*生成日時: {datetime.now():%Y/%m/%d %H:%M:%S}*"]
    return "\n".join(lines)

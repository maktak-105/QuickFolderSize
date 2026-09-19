from __future__ import annotations
from datetime import datetime

from i18n import tr


def format_size(size_bytes: int) -> str:
    if size_bytes < 0:
        return "—"
    for unit, threshold in (("GB", 1 << 30), ("MB", 1 << 20), ("KB", 1 << 10)):
        if size_bytes >= threshold:
            return f"{size_bytes / threshold:.1f} {unit}"
    return f"{size_bytes} B"


def generate_md_report(root, scan_time: datetime | None = None) -> str:
    """Generate a Markdown report from a FolderEntry tree, in the current UI language."""
    if scan_time is None:
        scan_time = datetime.now()

    dir_count = sum(1 for c in root.children if c.is_dir)
    lines = [
        tr("report_h1"),
        "",
        f"| {tr('report_col_item')} | {tr('report_col_value')} |",
        f"|:-----|:---|",
        f"| {tr('report_path')} | `{root.path}` |",
        f"| {tr('report_scan_datetime')} | {scan_time:%Y/%m/%d %H:%M} |",
        f"| {tr('report_total_size')} | {format_size(root.size)} |",
        f"| {tr('report_subfolder_count')} | {dir_count:,} |",
        f"| {tr('report_file_count_recursive')} | {root.file_count:,} |",
        "",
        tr("report_h2_structure"),
        "",
        tr("report_structure_desc"),
        "",
    ]

    def _render(node, depth: int, parent_size: int):
        indent = "  " * depth
        ratio  = node.size / parent_size * 100 if parent_size > 0 else 0.0
        if node.is_dir:
            lines.append(
                f"{indent}- 📁 **{node.name}**"
                f" — {format_size(node.size)} ({ratio:.1f}%)"
                f"  {tr('report_file_suffix', count=node.file_count)}"
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

    lines += ["", f"---", f"*{tr('report_generated_at')}: {datetime.now():%Y/%m/%d %H:%M:%S}*"]
    return "\n".join(lines)

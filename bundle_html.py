import os
import re


def bundle(output_dir=None):
    base_dir = os.path.dirname(__file__)
    tmpl_path = os.path.join(base_dir, "templates", "index.html")
    css_path = os.path.join(base_dir, "static", "css", "style.css")
    app_path = os.path.join(base_dir, "static", "js", "app.js")

    with open(tmpl_path, "r", encoding="utf-8") as f:
        html = f.read()
    with open(css_path, "r", encoding="utf-8") as f:
        css = f.read()
    with open(app_path, "r", encoding="utf-8") as f:
        app_js = f.read()

    # WebView2 の NavigateToString は相対パスの外部リソース(<link>/<script src>)を
    # 解決できないため、CSS/JS をインライン化した自己完結HTMLを1枚生成する。
    bundled = f"""<!DOCTYPE html>
<html lang="ja">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>QuickFolderSize</title>
  <style>
{css}
  </style>
</head>
<body>
"""
    body_start = html.find("<body>") + len("<body>")
    body_end = html.find("</body>")
    body_content = html[body_start:body_end]
    body_content = re.sub(r'<script.*?</script>', '', body_content, flags=re.DOTALL)

    bundled += body_content
    bundled += f"""
  <script>
{app_js}
  </script>
</body>
</html>
"""

    if output_dir is None:
        output_dir = os.path.join(base_dir, "dist")
    os.makedirs(output_dir, exist_ok=True)
    dist_index = os.path.join(output_dir, "index.html")
    with open(dist_index, "w", encoding="utf-8") as f:
        f.write(bundled)

    print(f"[OK] Generated self-contained bundle at {dist_index} ({len(bundled)} bytes)")


if __name__ == "__main__":
    bundle()

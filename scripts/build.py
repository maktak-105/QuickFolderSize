import os
import sys
import subprocess
import shutil
import glob

# Ensure scripts directory is in sys.path for importing bundle_html
script_dir = os.path.dirname(os.path.abspath(__file__))
if script_dir not in sys.path:
    sys.path.insert(0, script_dir)
import bundle_html


def find_compiler():
    # 1. PATH上を検索。MinGW固有フラグ(-mwindows)を使うためg++優先、次点clang++
    for comp in ["g++", "clang++"]:
        p = shutil.which(comp)
        if p:
            return p

    # 2. WinGetでインストールされたWinLibsパッケージ(PATH未追加でも動くようにする)
    local_app_data = os.environ.get("LOCALAPPDATA")
    winget_candidates = []
    if local_app_data:
        winget_bin = os.path.join(
            local_app_data,
            "Microsoft", "WinGet", "Packages",
            "BrechtSanders.WinLibs.MCF.UCRT_*", "mingw64", "bin"
        )
        winget_candidates.extend(glob.glob(os.path.join(winget_bin, "g++.exe")))
        winget_candidates.extend(glob.glob(os.path.join(winget_bin, "clang++.exe")))
    for c in winget_candidates:
        if os.path.exists(c):
            return c

    # 3. 既知のインストール先
    candidates = [
        r"C:\tools\llvm-mingw\bin\clang++.exe",
        r"C:\Program Files\LLVM\bin\clang++.exe",
        r"C:\Program Files (x86)\LLVM\bin\clang++.exe",
        r"C:\msys64\ucrt64\bin\g++.exe",
        r"C:\msys64\mingw64\bin\g++.exe",
        r"C:\tools\llvm\bin\clang++.exe",
    ]
    for c in candidates:
        if os.path.exists(c):
            return c
    return None


def run(cmd, cwd=None, label=""):
    print(f"\n[{label}] {' '.join(cmd)}")
    result = subprocess.run(cmd, capture_output=True, text=True, cwd=cwd)
    if result.returncode != 0:
        print(f"[失敗] {label}:")
        print(result.stdout)
        print(result.stderr)
        return False
    if result.stdout.strip():
        print(result.stdout)
    return True


def build():
    compiler = find_compiler()
    if not compiler:
        print("[エラー] C++ コンパイラ (g++ または clang++) が見つかりませんでした。")
        return False

    print(f"[発見] 使用コンパイラ: {compiler}")
    compiler_dir = os.path.dirname(compiler)

    repo_root = os.path.dirname(script_dir)
    src_dir = os.path.join(repo_root, "src")
    app_dir = os.path.join(src_dir, "app")
    cli_dir = os.path.join(src_dir, "cli")
    engine_dir = os.path.join(src_dir, "engine")
    intermediate_dir = os.path.join(repo_root, "build", "intermediate")
    dist_dir = os.path.join(repo_root, "dist")

    os.makedirs(intermediate_dir, exist_ok=True)
    os.makedirs(dist_dir, exist_ok=True)

    webview_include = os.environ.get("WEBVIEW2_INCLUDE", r"C:\tools\webview2\build\native\include")
    if not os.path.isdir(webview_include):
        local_cand = os.path.join(repo_root, "third_party", "webview2", "build", "native", "include")
        if os.path.isdir(local_cand):
            webview_include = local_cand
        else:
            print(f"[エラー] WebView2 SDK headers not found: {webview_include}")
            return False

    # 1. バンドルHTML生成
    bundle_html.bundle(intermediate_dir)
    bundled_index = os.path.join(intermediate_dir, "index.html")
    embed_html = os.path.join(intermediate_dir, "index_embed.html")
    if os.path.exists(bundled_index):
        if os.path.exists(embed_html):
            os.remove(embed_html)
        os.replace(bundled_index, embed_html)

    # 2. リソースコンパイラ特定
    windres = os.path.join(compiler_dir, "llvm-windres.exe")
    if not os.path.exists(windres):
        windres = os.path.join(compiler_dir, "windres.exe")
    if not os.path.exists(windres):
        windres = shutil.which("windres") or shutil.which("llvm-windres")
    if not windres:
        print("[エラー] Windows resource compiler (llvm-windres/windres) が見つかりませんでした。")
        return False

    # 3. リソースコンパイル
    resource_src = os.path.join(app_dir, "QuickFolderSize.rc")
    resource_obj = os.path.join(intermediate_dir, "QuickFolderSize_res.o")
    cmd_res = [
        windres,
        f"-I{app_dir}",
        f"-I{intermediate_dir}",
        resource_src,
        "-O", "coff",
        "-o", resource_obj,
    ]
    if not run(cmd_res, cwd=app_dir, label="1/4 GUIリソースをビルド中"):
        return False

    cli_resource_src = os.path.join(cli_dir, "QuickFolderSize_cli.rc")
    cli_resource_obj = os.path.join(intermediate_dir, "QuickFolderSize_cli_res.o")
    cmd_cli_res = [
        windres,
        f"-I{app_dir}",
        f"-I{cli_dir}",
        f"-I{intermediate_dir}",
        cli_resource_src,
        "-O", "coff",
        "-o", cli_resource_obj,
    ]
    if not run(cmd_cli_res, cwd=cli_dir, label="2/4 CLIリソースをビルド中"):
        return False

    # 4. GUI本体 (QuickFolderSize.exe)
    out_gui_exe = os.path.join(dist_dir, "QuickFolderSize.exe")
    cmd_gui = [
        compiler,
        "-O3",
        "-mwindows",
        "-std=c++17",
        "-static",
        f"-I{webview_include}",
        f"-I{engine_dir}",
        f"-I{app_dir}",
        os.path.join(engine_dir, "engine.cpp"),
        os.path.join(app_dir, "main_gui.cpp"),
        resource_obj,
        "-o", out_gui_exe,
        "-lkernel32",
        "-luser32",
        "-lgdi32",
        "-ldwmapi",
        "-lole32",
        "-loleaut32",
        "-luuid",
        "-lcomctl32",
        "-lshell32",
    ]
    if not run(cmd_gui, label="3/4 GUI (dist/QuickFolderSize.exe) ビルド中"):
        return False
    print(f"[成功] QuickFolderSize.exe を生成しました ({os.path.getsize(out_gui_exe)} bytes)")

    # 5. CLI本体 (QuickFolderSize_cli.exe)
    out_cli_exe = os.path.join(dist_dir, "QuickFolderSize_cli.exe")
    cmd_cli = [
        compiler,
        "-O3",
        "-std=c++17",
        "-static",
        f"-I{engine_dir}",
        f"-I{cli_dir}",
        os.path.join(engine_dir, "engine.cpp"),
        os.path.join(cli_dir, "main_cli.cpp"),
        cli_resource_obj,
        "-o", out_cli_exe,
        "-lkernel32",
        "-lshell32",
    ]
    if not run(cmd_cli, label="4/4 CLI (dist/QuickFolderSize_cli.exe) ビルド中"):
        return False
    print(f"[成功] QuickFolderSize_cli.exe を生成しました ({os.path.getsize(out_cli_exe)} bytes)")

    # 6. WebView2Loader.dll のコピー
    wv_loader = os.environ.get("WEBVIEW2_LOADER")
    if not wv_loader:
        wv_loader = os.path.join(os.path.dirname(webview_include), "x64", "WebView2Loader.dll")
    if not os.path.exists(wv_loader):
        wv_loader = r"C:\tools\webview2\build\native\x64\WebView2Loader.dll"
    if not os.path.exists(wv_loader):
        wv_loader = os.path.join(repo_root, "third_party", "webview2", "runtimes", "win-x64", "native", "WebView2Loader.dll")

    if os.path.exists(wv_loader):
        shutil.copy2(wv_loader, os.path.join(dist_dir, "WebView2Loader.dll"))
        print(f"[コピー] WebView2Loader.dll -> dist/")
    else:
        print(f"[警告] WebView2Loader.dll が見つかりませんでした: {wv_loader}")

    print(f"\n[完成] 配布用バイナリを dist/ フォルダに生成完了: {dist_dir}")
    return True


if __name__ == "__main__":
    success = build()
    sys.exit(0 if success else 1)

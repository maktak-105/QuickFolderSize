import os
import sys
import subprocess
import shutil
import glob


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


def build():
    compiler = find_compiler()
    if not compiler:
        print("[エラー] C++ コンパイラ (g++ または clang++) が見つかりませんでした。")
        return False

    print(f"[発見] 使用コンパイラ: {compiler}")

    base_dir = os.path.dirname(__file__)
    native_dir = os.path.join(base_dir, "core", "native")
    engine_src = os.path.join(native_dir, "engine.cpp")
    webview_src = os.path.join(native_dir, "webview_main.cpp")
    resource_src = os.path.join(native_dir, "QuickFolderSize.rc")
    resource_obj = os.path.join(native_dir, "QuickFolderSize_res.o")
    webview_include = os.environ.get("WEBVIEW2_INCLUDE", r"C:\tools\webview2\build\native\include")
    if not os.path.isdir(webview_include):
        print(f"[エラー] WebView2 SDK headers not found: {webview_include}")
        return False

    dist_dir = os.path.join(base_dir, "dist")
    binary_dir = os.path.join(dist_dir, "binary")
    os.makedirs(binary_dir, exist_ok=True)
    os.makedirs(os.path.join(binary_dir, "templates"), exist_ok=True)
    os.makedirs(os.path.join(binary_dir, "static", "css"), exist_ok=True)
    os.makedirs(os.path.join(binary_dir, "static", "js"), exist_ok=True)

    import bundle_html
    bundle_html.bundle(binary_dir)

    out_dll = os.path.join(native_dir, "engine_x64.dll")
    out_gui_exe = os.path.join(binary_dir, "QuickFolderSize.exe")

    windres = os.path.join(os.path.dirname(compiler), "llvm-windres.exe")
    if not os.path.exists(windres):
        windres = shutil.which("windres") or shutil.which("llvm-windres")
    if not windres:
        print("[エラー] Windows resource compiler (llvm-windres/windres) が見つかりませんでした。")
        return False
    cmd_res = [windres, resource_src, "-O", "coff", "-o", resource_obj]
    print(f"\n[0/2] アイコンリソースをビルド中: {' '.join(cmd_res)}")
    res_res = subprocess.run(cmd_res, capture_output=True, text=True, cwd=native_dir)
    if res_res.returncode != 0 or not os.path.exists(resource_obj):
        print("[失敗] アイコンリソースのビルドに失敗しました:")
        print(res_res.stderr)
        return False

    # 1. スキャンエンジンDLL(engine_x64.dll)をビルド
    cmd_dll = [
        compiler,
        "-O3",
        "-shared",
        "-std=c++17",
        "-static",
        engine_src,
        "-o", out_dll,
        "-lkernel32",
    ]
    print(f"\n[1/2] エンジンDLL ビルド中: {' '.join(cmd_dll)}")
    res_dll = subprocess.run(cmd_dll, capture_output=True, text=True)
    if res_dll.returncode == 0 and os.path.exists(out_dll):
        print(f"[成功] engine_x64.dll を生成しました ({os.path.getsize(out_dll)} bytes)")
        shutil.copy2(out_dll, os.path.join(binary_dir, "engine_x64.dll"))
    else:
        print("[失敗] DLL ビルドに失敗しました:")
        print(res_dll.stderr)
        return False

    # 2. GUI本体(QuickFolderSize.exe)。engine.cppは静的リンクで直接コンパイルする
    cmd_gui = [
        compiler,
        "-O3",
        "-mwindows",
        "-std=c++17",
        "-static",
        f"-I{webview_include}",
        engine_src,
        webview_src,
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
    print(f"\n[2/2] GUI (dist/binary/QuickFolderSize.exe) ビルド中: {' '.join(cmd_gui)}")
    res_gui = subprocess.run(cmd_gui, capture_output=True, text=True)
    if res_gui.returncode == 0 and os.path.exists(out_gui_exe):
        print(f"[成功] QuickFolderSize.exe を生成しました ({os.path.getsize(out_gui_exe)} bytes)")
    else:
        print("[失敗] GUI ビルドに失敗しました:")
        print(res_gui.stderr)
        return False

    # 3. WebView2Loader.dll と開発用テンプレート一式をコピー
    wv_loader = os.environ.get("WEBVIEW2_LOADER")
    if not wv_loader:
        wv_loader = os.path.join(os.path.dirname(webview_include), "x64", "WebView2Loader.dll")
    if not os.path.exists(wv_loader):
        wv_loader = r"C:\tools\webview2\build\native\x64\WebView2Loader.dll"
    if os.path.exists(wv_loader):
        shutil.copy2(wv_loader, os.path.join(binary_dir, "WebView2Loader.dll"))
    else:
        print(f"[警告] WebView2Loader.dll が見つかりませんでした: {wv_loader}")

    shutil.copy2(os.path.join(base_dir, "templates", "index.html"), os.path.join(binary_dir, "templates", "index.html"))
    shutil.copy2(os.path.join(base_dir, "static", "css", "style.css"), os.path.join(binary_dir, "static", "css", "style.css"))
    shutil.copy2(os.path.join(base_dir, "static", "js", "app.js"), os.path.join(binary_dir, "static", "js", "app.js"))

    print(f"\n[完成] 配布用バイナリを dist/binary フォルダに生成完了: {binary_dir}")
    return True


if __name__ == "__main__":
    success = build()
    sys.exit(0 if success else 1)

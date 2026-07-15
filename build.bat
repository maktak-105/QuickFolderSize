@echo off
echo === Phase 2: PyInstaller Build ===

echo [1/3] Installing dependencies...
pip install -r requirements.txt
if errorlevel 1 (
    echo ERROR: pip install failed
    exit /b 1
)

echo [2/3] Building EXE...
pyinstaller --onedir --windowed --name folder_viewer --icon=python/resources/icon.ico --add-data "python/resources;resources" --distpath dist_tmp --workpath build --noconfirm python/main.py
if errorlevel 1 (
    echo ERROR: PyInstaller build failed
    exit /b 1
)

echo [3/3] Moving output to dist\...
xcopy /s /y /q dist_tmp\folder_viewer\* dist\
rmdir /s /q dist_tmp

echo.
echo === Build complete ===
echo Output: dist\folder_viewer.exe

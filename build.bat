@echo off
echo === QuickFolderSize Native Build ===

python build-tools\build_native.py
if errorlevel 1 (
    echo ERROR: build_native.py failed
    exit /b 1
)

echo.
echo === Build complete ===
echo Output: dist\binary\QuickFolderSize.exe

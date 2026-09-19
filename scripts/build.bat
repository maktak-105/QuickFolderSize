@echo off
setlocal
cd /d "%~dp0\.."
echo [Build] Building QuickFolderSize...
python scripts\build.py %*


# -*- mode: python ; coding: utf-8 -*-

# 未使用のPyQt6サブモジュールを除外してビルドサイズを削減。
# アプリはQtWidgets/QtCore/QtGuiのみ使用（QtNetwork/QtPdf/QtSvg等は未参照）。
_UNUSED_PYQT6_MODULES = [
    'PyQt6.QtNetwork',
    'PyQt6.QtPdf', 'PyQt6.QtPdfWidgets',
    'PyQt6.QtQml', 'PyQt6.QtQuick', 'PyQt6.QtQuickWidgets',
    'PyQt6.QtMultimedia', 'PyQt6.QtMultimediaWidgets',
    'PyQt6.QtSql', 'PyQt6.QtTest',
    'PyQt6.QtOpenGL', 'PyQt6.QtOpenGLWidgets',
    'PyQt6.QtBluetooth', 'PyQt6.QtNfc', 'PyQt6.QtPositioning', 'PyQt6.QtSensors',
    'PyQt6.QtSerialPort', 'PyQt6.QtWebSockets', 'PyQt6.QtDBus',
    'PyQt6.QtDesigner', 'PyQt6.QtHelp', 'PyQt6.QtPrintSupport',
    'PyQt6.QtXml', 'PyQt6.QtSvgWidgets',
]

a = Analysis(
    ['python\\main.py'],
    pathex=[],
    binaries=[],
    datas=[('python/resources', 'resources')],
    hiddenimports=[],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=_UNUSED_PYQT6_MODULES,
    noarchive=False,
    optimize=0,
)

# モジュール除外(_UNUSED_PYQT6_MODULES)では落ちない生バイナリ・データも除去する。
# Qt6のプラグインDLL（styles配下など）はPyInstallerのバイナリ/データ再分類で
# a.binaries 側に入るため、拡張子ではなくファイル名・パスで判定する。
# - opengl32sw.dll: 未使用のMesaソフトウェアOpenGLレンダラ（約20MB）
# - Qt6Network.dll / Qt6Pdf.dll: モジュールexcludesでは落ちない実体DLL
# - Qt6/plugins/styles: app.setStyle("Fusion")はQt組み込みでプラグイン不要
# - Qt6/translations: QTranslator未使用（独自i18n.pyで対応済み）
_DROP_BINARY_BASENAMES = {'opengl32sw.dll', 'qt6network.dll', 'qt6pdf.dll'}
_DROP_PATH_MARKERS = ('/plugins/styles/', '/translations/')


def _norm(path: str) -> str:
    return path.replace('\\', '/').lower()


def _keep_binary(entry) -> bool:
    dest = _norm(entry[0])
    if dest.rsplit('/', 1)[-1] in _DROP_BINARY_BASENAMES:
        return False
    return not any(marker in dest for marker in _DROP_PATH_MARKERS)


def _keep_data(entry) -> bool:
    dest = _norm(entry[0])
    return not any(marker in dest for marker in _DROP_PATH_MARKERS)


a.binaries = [b for b in a.binaries if _keep_binary(b)]
a.datas = [d for d in a.datas if _keep_data(d)]

pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name='folder_viewer',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
    icon=['python\\resources\\icon.ico'],
)
coll = COLLECT(
    exe,
    a.binaries,
    a.datas,
    strip=False,
    upx=True,
    upx_exclude=[],
    name='folder_viewer',
)

@echo off
rem Build tools\dist\suota.exe (standalone, no Python needed on the target PC).
rem Needs Python 3 on PATH; bleak + PyInstaller go into a private venv (tools\.venv).
cd /d "%~dp0"
if not exist .venv\Scripts\python.exe (
    python -m venv .venv || exit /b 1
    .venv\Scripts\python -m pip install -q bleak pyinstaller || exit /b 1
)
.venv\Scripts\python suota.py --selftest || exit /b 1
.venv\Scripts\python -m PyInstaller --noconfirm --onefile --windowed --name suota ^
    --distpath dist --workpath build --specpath build ^
    --add-data "%~dp0suota_protocol.json;." ^
    --collect-submodules winrt --collect-submodules bleak suota.py || exit /b 1
echo.
echo Built %~dp0dist\suota.exe

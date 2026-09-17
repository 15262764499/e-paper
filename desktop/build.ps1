$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
$pulsePython = Join-Path $PSScriptRoot '.venv/Scripts/python.exe'
if (-not (Test-Path -LiteralPath $pulsePython)) {
    python -m venv .venv
    if ($LASTEXITCODE -ne 0) { throw 'Cannot create Python environment' }
}
& $pulsePython -m pip install -r requirements-build.txt
if ($LASTEXITCODE -ne 0) { throw 'Dependency installation failed' }
& $pulsePython -m pytest tests -q
if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
New-Item -ItemType Directory -Force -Path assets | Out-Null
& $pulsePython main.py --export-icon assets/pulse.ico
if ($LASTEXITCODE -ne 0) { throw 'Icon generation failed' }
& $pulsePython -m PyInstaller --noconfirm --windowed --onedir --name Pulse --icon assets/pulse.ico --exclude-module PyQt5 --exclude-module PyQt6 --exclude-module PySide2 --exclude-module numpy --exclude-module pandas --exclude-module matplotlib main.py
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
& $pulsePython collect_licenses.py
if ($LASTEXITCODE -ne 0) { throw 'License collection failed' }
Write-Output 'Ready: dist/Pulse/Pulse.exe (keep the entire Pulse folder together)'

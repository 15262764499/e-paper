param([string]$ToolchainBin, [switch]$Clean, [switch]$ApplicationOnly)
$ErrorActionPreference = 'Stop'
if (-not $ToolchainBin) {
    $compiler = Get-Command arm-none-eabi-gcc.exe -ErrorAction SilentlyContinue
    if ($compiler) { $ToolchainBin = Split-Path $compiler.Source }
    else {
        $base = Join-Path $env:USERPROFILE '.local/arm-tools'
        if (Test-Path (Join-Path $base 'bin/arm-none-eabi-gcc.exe')) {
            $ToolchainBin = Join-Path $base 'bin'
        }
        $found = Get-ChildItem -LiteralPath $base -Directory -ErrorAction SilentlyContinue |
            Where-Object { Test-Path (Join-Path $_.FullName 'bin/arm-none-eabi-gcc.exe') } |
            Sort-Object Name -Descending | Select-Object -First 1
        if ($found) { $ToolchainBin = Join-Path $found.FullName 'bin' }
    }
}
if (-not $ToolchainBin -or -not (Test-Path (Join-Path $ToolchainBin 'arm-none-eabi-gcc.exe'))) {
    throw 'Arm GNU compiler not found. Supply -ToolchainBin <toolchain/bin>.'
}
$make = Get-Command mingw32-make.exe -ErrorAction SilentlyContinue
if (-not $make) { throw 'mingw32-make.exe not found in PATH.' }
$previousPath = $env:PATH
Push-Location $PSScriptRoot
try {
    $env:PATH = "$ToolchainBin;$previousPath"
    & arm-none-eabi-gcc.exe --version | Select-Object -First 1
    if ($Clean) {
        & $make.Source clean
        if ($LASTEXITCODE) { throw 'Application clean failed.' }
        if (-not $ApplicationOnly) {
            & $make.Source -f Bootloader/Makefile clean
            if ($LASTEXITCODE) { throw 'Bootloader clean failed.' }
        }
    }
    $target = if ($ApplicationOnly) { 'all' } else { 'firmware' }
    & $make.Source -j4 $target
    if ($LASTEXITCODE) { throw "Firmware build failed: $LASTEXITCODE" }
    Write-Host 'Application: build/epaper_project.hex (base 0x08006000)'
    if (-not $ApplicationOnly) { Write-Host 'Bootloader: build_bootloader/epaper_bootloader.hex (base 0x08000000)' }
} finally {
    $env:PATH = $previousPath
    Pop-Location
}

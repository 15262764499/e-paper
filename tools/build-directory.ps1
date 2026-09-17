param([Parameter(Mandatory=$true)][string]$Path, [switch]$Clean)
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$target = [IO.Path]::GetFullPath((Join-Path $repo $Path))
if (-not $target.StartsWith($repo.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw "Build directory must be inside repository: $target"
}
if ($Clean) {
    if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Recurse -Force }
} else {
    New-Item -ItemType Directory -Path $target -Force | Out-Null
}

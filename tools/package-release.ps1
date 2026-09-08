[CmdletBinding()]
param(
    [string]$Version = $env:GITHUB_REF_NAME
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ([string]::IsNullOrWhiteSpace($Version)) { $Version = 'local' }

$dll = Join-Path $root 'src\proxy\DINPUT8.dll'
if (-not (Test-Path -LiteralPath $dll -PathType Leaf)) {
    throw "Build output not found: $dll"
}

$dist = Join-Path $root 'dist'
$name = "DS2LE-DLSS5-Integration-$Version"
$stage = Join-Path $dist $name
$zip = Join-Path $dist "$name.zip"

New-Item -ItemType Directory -Force -Path $dist | Out-Null
if (Test-Path -LiteralPath $stage) {
    Remove-Item -LiteralPath $stage -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $stage | Out-Null

Copy-Item -LiteralPath $dll -Destination (Join-Path $stage 'DINPUT8.dll')
Copy-Item -LiteralPath (Join-Path $root 'config\ReShade.ini.example') -Destination (Join-Path $stage 'ReShade.ini.example')
Copy-Item -LiteralPath (Join-Path $root 'README.md') -Destination (Join-Path $stage 'README.md')
Copy-Item -LiteralPath (Join-Path $root 'INSTALL.txt') -Destination (Join-Path $stage 'INSTALL.txt')
Copy-Item -LiteralPath (Join-Path $root 'LICENSE') -Destination (Join-Path $stage 'LICENSE')
Copy-Item -LiteralPath (Join-Path $root 'NOTICE.md') -Destination (Join-Path $stage 'NOTICE.md')

Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip -Force
$hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
"$hash  $([IO.Path]::GetFileName($zip))" | Set-Content -LiteralPath (Join-Path $dist 'SHA256SUMS.txt') -Encoding ascii

Write-Output "PACKAGE OK: $zip"
Write-Output "SHA256: $hash"

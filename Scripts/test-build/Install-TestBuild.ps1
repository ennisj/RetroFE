# Installs a test build that push_to_share.py put in builds\ next to this script:
# retrofe\retrofe.exe and layouts\Aeon Nox go over an install that already has
# RetroFE's DLLs. Files it replaces are kept in backups\ first; nothing is deleted.
#   powershell -ExecutionPolicy Bypass -File Install-TestBuild.ps1 [-Install E:\Storage\OneSauce] [-Build 2026-10-28-38454b9a]
# See README.md beside push_to_share.py in the repository (Scripts/test-build).
param(
    [string]$Install = 'E:\Storage\OneSauce',
    [string]$Build
)
$ErrorActionPreference = 'Stop'

$builds = Join-Path $PSScriptRoot 'builds'
if (-not $Build) {
    # Folder names start with the commit's date, so the newest sorts last
    $Build = Get-ChildItem -Directory $builds | Where-Object { $_.Name -notlike '*.partial' } |
        Sort-Object Name | Select-Object -Last 1 -ExpandProperty Name
    if (-not $Build) { throw "No builds in $builds" }
}
$from = Join-Path $builds $Build
if (-not (Test-Path (Join-Path $from 'retrofe\retrofe.exe'))) { throw "$from has no retrofe\retrofe.exe" }
$exe = Join-Path $Install 'retrofe\retrofe.exe'
if (-not (Test-Path $exe)) { throw "$Install isn't a RetroFE install: no retrofe\retrofe.exe" }
$running = Get-Process retrofe -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe }
if ($running) { throw "RetroFE is running from $Install; close it first" }

Get-Content (Join-Path $from 'BUILD.txt')

# Every file of the build that's already in the install is backed up first
$backup = Join-Path $PSScriptRoot ("backups\" + (Get-Date -Format 'yyyy-MM-dd-HHmmss'))
$files = Get-ChildItem -Recurse -File (Join-Path $from 'retrofe'), (Join-Path $from 'layouts')
foreach ($file in $files) {
    $relative = $file.FullName.Substring($from.Length).TrimStart('\')
    $old = Join-Path $Install $relative
    if (Test-Path $old) {
        $saved = Join-Path $backup $relative
        New-Item -ItemType Directory -Force (Split-Path $saved) | Out-Null
        Copy-Item $old $saved
    }
}
foreach ($file in $files) {
    $relative = $file.FullName.Substring($from.Length).TrimStart('\')
    $to = Join-Path $Install $relative
    New-Item -ItemType Directory -Force (Split-Path $to) | Out-Null
    Copy-Item $file.FullName $to -Force
}
"Installed $Build in $Install ($($files.Count) files)."
if (Test-Path $backup) { "The files it replaced are in $backup (same paths as in the install)." }

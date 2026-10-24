# Unpacks GStreamer's MSVC x86_64 development files (headers and import libraries,
# enough to build RetroFE) and sets GSTREAMER_ROOT for the following steps.
#
# GStreamer prunes older releases from gstreamer.freedesktop.org, which is what broke
# Chocolatey's gstreamer-devel 1.24.11; bump $Version when this one goes too. The DLLs
# RetroFE ships with come from the Package/Environment/Windows/retrofe submodule, not
# from here.
#
# Usage: install-gstreamer-windows.ps1 [-Version 1.24.13] [-Downloads dir] [-Destination dir]
param(
    [string]$Version = '1.24.13',
    [string]$Downloads = "$env:RUNNER_TEMP\gstreamer-downloads",
    [string]$Destination = 'D:\gstreamer'
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

New-Item -ItemType Directory -Force $Downloads | Out-Null
$name = "gstreamer-1.0-devel-msvc-x86_64-$Version.msi"
$msi = Join-Path $Downloads $name
if (-not (Test-Path $msi)) {
    Invoke-WebRequest -UseBasicParsing "https://gstreamer.freedesktop.org/data/pkg/windows/$Version/msvc/$name" -OutFile $msi
}
# An administrative install only unpacks the files, without touching the system
$process = Start-Process msiexec.exe -ArgumentList '/a', "`"$msi`"", '/qn', "TARGETDIR=`"$Destination`"" -Wait -PassThru
if ($process.ExitCode -ne 0) { throw "Unpacking $name failed ($($process.ExitCode))" }

# The root is the folder holding include\gstreamer-1.0\gst\gst.h
$header = Get-ChildItem -Path $Destination -Recurse -Filter gst.h |
    Where-Object { $_.Directory.Name -eq 'gst' -and $_.Directory.Parent.Name -eq 'gstreamer-1.0' } |
    Select-Object -First 1
if (-not $header) { throw "No gst.h under $Destination" }
$root = $header.Directory.Parent.Parent.Parent.FullName
"GStreamer $Version is in $root"
"GSTREAMER_ROOT=$root" >> $env:GITHUB_ENV

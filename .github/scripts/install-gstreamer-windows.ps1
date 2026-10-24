# Unpacks GStreamer's MSVC x86_64 development files (headers and import libraries,
# enough to build RetroFE) and sets GSTREAMER_ROOT for the following steps.
#
# GStreamer prunes older releases from gstreamer.freedesktop.org, which is what broke
# Chocolatey's gstreamer-devel 1.24.11; bump $Version when this one goes too. The DLLs
# RetroFE ships with come from the Package/Environment/Windows/retrofe submodule, not
# from here.
#
# The MSI is checked against $Sha256 whether it was just downloaded or came from the
# Actions cache. When bumping $Version, take the new hash from the .sha256sum file
# GStreamer publishes beside the MSI, and check it against the file you download.
#
# Usage: install-gstreamer-windows.ps1 [-Downloads dir] [-Destination dir]
param(
    [string]$Downloads = "$env:RUNNER_TEMP\gstreamer-downloads",
    [string]$Destination = 'D:\gstreamer'
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$Version = '1.24.13'
# gstreamer-1.0-devel-msvc-x86_64-1.24.13.msi.sha256sum
$Sha256 = '0afb4394c2cba3999c0f5e74f28c2ac5d98f03131140e09246d4dcd60a0a0395'

function Test-Msi([string]$path) {
    (Get-FileHash $path -Algorithm SHA256).Hash.ToLowerInvariant() -eq $Sha256
}

New-Item -ItemType Directory -Force $Downloads | Out-Null
$name = "gstreamer-1.0-devel-msvc-x86_64-$Version.msi"
$msi = Join-Path $Downloads $name
if ((Test-Path $msi) -and -not (Test-Msi $msi)) {
    # A cached copy that isn't the file we expect: never used
    Remove-Item -Force $msi
    throw "The cached $name doesn't match its SHA-256; it was deleted, run the job again to download it afresh"
}
if (-not (Test-Path $msi)) {
    Invoke-WebRequest -UseBasicParsing "https://gstreamer.freedesktop.org/data/pkg/windows/$Version/msvc/$name" -OutFile $msi
    if (-not (Test-Msi $msi)) {
        Remove-Item -Force $msi
        throw "The downloaded $name doesn't match its SHA-256"
    }
}
"$name matches SHA-256 $Sha256"
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

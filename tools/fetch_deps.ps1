# Download the third-party binaries needed to build on Windows.
#
#   FFmpeg  headers, import libraries and runtime DLLs
#   SDL3    headers, import libraries and runtime DLL
#
# Both archives ship prebuilt binaries only; no third-party source is vendored in
# this repository. Everything lands in third_party\ and is not committed.
#
# On Linux use the distribution packages instead:
#   sudo apt install libavcodec-dev libswscale-dev libavutil-dev libsdl3-dev
param([switch]$Force)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$deps = @(
    @{
        Name    = 'ffmpeg'
        Url     = 'https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-lgpl-shared.zip'
        Dest    = 'third_party/ffmpeg'
        Lib     = 'lib/avcodec.lib'
        Include = 'include/libavcodec/avcodec.h'
    },
    @{
        Name    = 'sdl3'
        Url     = 'https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-devel-3.4.18-VC.zip'
        Dest    = 'third_party/sdl3'
        Lib     = 'lib/SDL3.lib'
        Include = 'include/SDL3/SDL.h'
    }
)

foreach ($d in $deps) {
    $dest = Join-Path $root $d.Dest
    if (-not $Force -and (Test-Path (Join-Path $dest $d.Lib)) -and (Test-Path (Join-Path $dest $d.Include))) {
        Write-Host "[ok] $($d.Name) already fetched"
        continue
    }

    Write-Host "==> fetching $($d.Name)"
    Write-Host "    $($d.Url)"

    $tmp = Join-Path $env:TEMP "scrcpy-cam-$($d.Name)"
    $zip = "$tmp.zip"
    if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }

    try {
        Invoke-WebRequest -Uri $d.Url -OutFile $zip -UseBasicParsing
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        [IO.Compression.ZipFile]::ExtractToDirectory($zip, $tmp)
    } catch {
        Write-Host "[!!] $($d.Name): $($_.Exception.Message)" -ForegroundColor Red
        Write-Host "     You can also supply this manually and pass -DFFMPEG_ROOT / -DSDL3_ROOT."
        exit 1
    }

    $src = Get-ChildItem -Directory $tmp | Select-Object -First 1
    if (-not $src) { Write-Host "[!!] $($d.Name): unexpected archive layout"; exit 1 }

    New-Item -ItemType Directory -Force -Path (Join-Path $dest 'include'),
                                            (Join-Path $dest 'lib'),
                                            (Join-Path $dest 'bin') | Out-Null

    foreach ($pair in @(@('include', 'include'), @('lib', 'lib'), @('lib/x64', 'lib'), @('bin', 'bin'))) {
        $from = Join-Path $src.FullName $pair[0]
        if (Test-Path $from) {
            Copy-Item -Recurse -Force "$from\*" (Join-Path $dest $pair[1])
        }
    }
    # loose DLLs / libs at the root of the archive
    Get-ChildItem $src.FullName -Filter '*.dll' -ErrorAction SilentlyContinue |
        Copy-Item -Force -Destination (Join-Path $dest 'bin')
    Get-ChildItem $src.FullName -Filter '*.lib' -ErrorAction SilentlyContinue |
        Copy-Item -Force -Destination (Join-Path $dest 'lib')

    Remove-Item -Recurse -Force $tmp, $zip -ErrorAction SilentlyContinue

    if (-not (Test-Path (Join-Path $dest $d.Lib)) -or -not (Test-Path (Join-Path $dest $d.Include))) {
        Write-Host "[!!] $($d.Name): archive did not contain $($d.Lib) / $($d.Include)" -ForegroundColor Red
        exit 1
    }
    Write-Host "[ok] $($d.Name)"
}

Write-Host ''
Write-Host 'All dependencies are present.'
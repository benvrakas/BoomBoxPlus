# Downloads the pinned yt-dlp and ffmpeg builds into Resources/Tools, verifying SHA-256 checksums.
# Run before packaging: powershell -ExecutionPolicy Bypass -File Tools\FetchTools.ps1

$ErrorActionPreference = 'Stop'
$PluginDir = Split-Path -Parent $PSScriptRoot
$ToolsDir = Join-Path $PluginDir 'Resources\Tools'
$TempDir = Join-Path $env:TEMP 'BoomBoxPlusTools'

$YtDlpVersion = '2026.08.19'
$YtDlpUrl = "https://github.com/yt-dlp/yt-dlp/releases/download/$YtDlpVersion/yt-dlp.exe"
$YtDlpSha256 = '66674953fe251b89f4d08c5f0e35e0728679bd67ab3d7d05c0562af101dd3e7a'

$FfmpegTag = 'autobuild-2026-09-23-14-55'
$FfmpegAsset = 'ffmpeg-n8.1.3-win64-lgpl-shared-8.1'
$FfmpegUrl = "https://github.com/BtbN/FFmpeg-Builds/releases/download/$FfmpegTag/$FfmpegAsset.zip"
$FfmpegSha256 = '60a055792e88524db437a78c2fcd4471536abf3b30d34fae1249591c00e13432'

# Downloads a file and stops if its SHA-256 doesn't match.
function Get-VerifiedFile([string]$Url, [string]$Destination, [string]$ExpectedSha256) {
    Write-Host "Downloading $Url"
    Invoke-WebRequest -Uri $Url -OutFile $Destination -UseBasicParsing
    $Actual = (Get-FileHash -Algorithm SHA256 $Destination).Hash.ToLower()
    if ($Actual -ne $ExpectedSha256) {
        throw "Checksum mismatch for $Destination (expected $ExpectedSha256, got $Actual)"
    }
}

New-Item -ItemType Directory -Force -Path $ToolsDir, $TempDir | Out-Null

Get-VerifiedFile $YtDlpUrl (Join-Path $ToolsDir 'yt-dlp.exe') $YtDlpSha256

$FfmpegZip = Join-Path $TempDir "$FfmpegAsset.zip"
Get-VerifiedFile $FfmpegUrl $FfmpegZip $FfmpegSha256
Expand-Archive -Path $FfmpegZip -DestinationPath $TempDir -Force
$FfmpegOut = Join-Path $ToolsDir 'ffmpeg'
Remove-Item -Recurse -Force $FfmpegOut -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $FfmpegOut | Out-Null
Get-ChildItem (Join-Path $TempDir "$FfmpegAsset\bin") | Where-Object { $_.Name -ne 'ffplay.exe' } | Copy-Item -Destination $FfmpegOut
Copy-Item (Join-Path $TempDir "$FfmpegAsset\LICENSE.txt") (Join-Path $FfmpegOut 'LICENSE.txt') -ErrorAction SilentlyContinue
# Where the matching FFmpeg source and build scripts are, as the LGPL asks.
Set-Content -Encoding utf8 -Path (Join-Path $FfmpegOut 'SOURCE.txt') -Value @(
    'FFmpeg n8.1.3, LGPL v3 (built with --enable-version3), shared build by BtbN/FFmpeg-Builds.',
    'FFmpeg source: https://github.com/FFmpeg/FFmpeg/tree/n8.1.3',
    'Build scripts and release: https://github.com/BtbN/FFmpeg-Builds/releases/tag/autobuild-2026-09-23-14-55',
    'yt-dlp 2026.08.19 (Unlicense): https://github.com/yt-dlp/yt-dlp/releases/tag/2026.08.19'
)

Write-Host "Tools ready in $ToolsDir"

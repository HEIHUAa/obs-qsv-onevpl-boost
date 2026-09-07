#Requires -Version 5.1
<#
.SYNOPSIS
  One-time setup of the local Windows build environment for obs-qsv-onevpl.

.DESCRIPTION
  Downloads all build-environment files into <repo>\build-env and wires the
  plugin into an OBS Studio source tree via NTFS junctions, mirroring the CI
  workflow (.github/workflows/build-uhd700.yml):

    - OBS Studio <OBSVersion> source clone (shallow, with submodules)
      -> build-env\obs-studio
    - oneVPL <OneVPLVersion> source clone            -> build-env\libvpl
    - Intel MediaSDK api/include sparse clone        -> build-env\MediaSDK
    - FFmpeg headers moved from <repo>\log or cloned (release/8.1)
                                                     -> build-env\log\FFmpeg
    - junction build-env\obs-studio\plugins\obs-qsv-onevpl -> this repo
    - junction build-env\obs-studio\plugins\log            -> build-env\log
    - junction <repo>\libvpl -> build-env\libvpl
    - appends add_subdirectory(obs-qsv-onevpl) to OBS's plugins/CMakeLists.txt

  The first build (scripts\Build-Local.ps1) then automatically downloads the
  prebuilt OBS dependencies (obs-deps + Qt6) into build-env\obs-studio\.deps.
#>
[CmdletBinding()]
param(
  [string]$OBSVersion = "32.2.0",
  [string]$OneVPLVersion = "v2.14.0",
  [string]$FFmpegRef = "release/8.1",
  [string]$EnvDir = ""
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not $EnvDir) { $EnvDir = Join-Path $RepoRoot "build-env" }
$ObsDir      = Join-Path $EnvDir "obs-studio"
$LibVplDir   = Join-Path $EnvDir "libvpl"
$MediaSdkDir = Join-Path $EnvDir "MediaSDK"
$PluginName  = "obs-qsv-onevpl"

Write-Host "=== obs-qsv-onevpl local build environment setup ==="
Write-Host "Repo root : $RepoRoot"
Write-Host "Env dir   : $EnvDir"

# ---------------------------------------------------------------------------
# 1. Locate Visual Studio Build Tools and its bundled CMake
# ---------------------------------------------------------------------------
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found; please install Visual Studio Build Tools." }

$vsPath = (& $vswhere -latest -products * -property installationPath | Select-Object -First 1)
if (-not $vsPath -or -not (Test-Path (Join-Path $vsPath "VC\Tools\MSVC"))) {
  throw "No Visual Studio instance with MSVC found. Install 'Visual Studio Build Tools' with the C++ workload."
}

$cmake = $null
try {
  $cmake = (& $vswhere -latest -products * -find "Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" |
            Select-Object -First 1)
} catch { $cmake = $null }
if (-not $cmake -or -not (Test-Path $cmake)) {
  $cmd = Get-Command cmake.exe -ErrorAction SilentlyContinue
  if ($cmd) { $cmake = $cmd.Source }
}
if (-not $cmake -or -not (Test-Path $cmake)) {
  throw "CMake not found (neither VS-bundled nor on PATH)."
}
Write-Host ("VS path    : {0}" -f $vsPath)
Write-Host ("CMake      : {0}" -f $cmake)

# ---------------------------------------------------------------------------
# 2. Sanity checks
# ---------------------------------------------------------------------------
$sdkDir = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\Include"
$hasSdk = $false
if (Test-Path $sdkDir) {
  $hasSdk = ((Get-ChildItem $sdkDir -Directory | Where-Object { $_.Name -like "10.0.26100*" }).Count -gt 0)
}
if (-not $hasSdk) {
  Write-Warning "Windows SDK 10.0.26100 not found; the OBS windows-x64 preset requires it."
}

# ---------------------------------------------------------------------------
# 3. Clones (idempotent: existing clones are reused)
# ---------------------------------------------------------------------------
function Ensure-GitClone {
  param([string]$Url, [string]$Dir, [string]$Ref, [string[]]$CloneArgs = @(), [switch]$Submodules)
  if (Test-Path (Join-Path $Dir ".git")) {
    Write-Host ("[skip] already cloned: {0}" -f $Dir)
  } else {
    Write-Host ("[clone] {0} -> {1}" -f $Url, $Dir)
    & git clone @CloneArgs --branch $Ref $Url $Dir
    if ($LASTEXITCODE -ne 0) { throw "git clone failed for $Url" }
  }
  if ($Submodules) {
    Write-Host "[submodules] updating (depth 1): $Dir"
    & git -C $Dir submodule update --init --recursive --depth 1
    if ($LASTEXITCODE -ne 0) { throw "git submodule update failed for $Dir" }
  }
}

New-Item -ItemType Directory -Force -Path $EnvDir | Out-Null

Ensure-GitClone -Url "https://github.com/obsproject/obs-studio.git" -Dir $ObsDir -Ref $OBSVersion `
  -CloneArgs @("--depth", "1", "--recurse-submodules", "--shallow-submodules") -Submodules

Ensure-GitClone -Url "https://github.com/oneapi-src/oneVPL.git" -Dir $LibVplDir -Ref $OneVPLVersion `
  -CloneArgs @("--depth", "1")

if (Test-Path (Join-Path $MediaSdkDir ".git")) {
  Write-Host ("[skip] already cloned: {0}" -f $MediaSdkDir)
} else {
  Write-Host "[clone] https://github.com/Intel-Media-SDK/MediaSDK.git (sparse: api/include)"
  & git clone --depth 1 --filter=blob:none --sparse https://github.com/Intel-Media-SDK/MediaSDK.git $MediaSdkDir
  if ($LASTEXITCODE -ne 0) { throw "git clone failed for MediaSDK" }
}
& git -C $MediaSdkDir sparse-checkout set api/include
if ($LASTEXITCODE -ne 0) { throw "git sparse-checkout failed for MediaSDK" }
if (-not (Test-Path (Join-Path $MediaSdkDir "api\include"))) {
  throw "MediaSDK api/include missing after sparse checkout"
}

# ---------------------------------------------------------------------------
# 4. FFmpeg headers (referenced by the plugin as ../log/FFmpeg)
#    Migrated from the old <repo>\log location when present, otherwise cloned.
#    Default ref: release/8.1 - matches the FFmpeg version shipped in the
#    OBS prebuilt deps (obs-deps 2026-07-15) that libobs links against.
# ---------------------------------------------------------------------------
$EnvLogDir = Join-Path $EnvDir "log"
if ((Test-Path (Join-Path $RepoRoot "log\FFmpeg")) -and -not (Test-Path (Join-Path $EnvLogDir "FFmpeg"))) {
  Write-Host "[migrate] moving log\FFmpeg into build-env\log\FFmpeg ..."
  New-Item -ItemType Directory -Force -Path $EnvLogDir | Out-Null
  Move-Item (Join-Path $RepoRoot "log\FFmpeg") (Join-Path $EnvLogDir "FFmpeg")
}
$FFmpegDir = Join-Path $EnvLogDir "FFmpeg"
if (Test-Path $FFmpegDir) {
  Write-Host ("[skip] FFmpeg headers present: {0}" -f $FFmpegDir)
} else {
  Write-Host ("[clone] https://github.com/FFmpeg/FFmpeg.git ({0}) -> {1}" -f $FFmpegRef, $FFmpegDir)
  & git clone --depth 1 --branch $FFmpegRef https://github.com/FFmpeg/FFmpeg.git $FFmpegDir
  if ($LASTEXITCODE -ne 0) {
    throw ("git clone failed for FFmpeg (ref '{0}'). The FFmpeg headers are required to " -f $FFmpegRef) +
      "compile the reencoder sources (see obs-qsvonevpl\CMakeLists.txt ../log/FFmpeg)."
  }
}

# ---------------------------------------------------------------------------
# 5. Junctions (existing junctions are always re-created so the target is
#    guaranteed to be up to date)
# ---------------------------------------------------------------------------
function Ensure-Junction {
  param([string]$Link, [string]$Target)
  if (Test-Path -LiteralPath $Link) {
    $item = Get-Item -LiteralPath $Link -Force
    if ($item.LinkType -ne "Junction") {
      throw "Path exists and is NOT a junction, refusing to touch it: $Link"
    }
    & cmd /c rmdir "$($item.FullName)" 2>$null
    if (Test-Path -LiteralPath $Link) {
      throw "Failed to remove existing junction: $Link"
    }
    Write-Host ("[relink] {0} -> {1}" -f $Link, $Target)
  } else {
    Write-Host ("[junction] {0} -> {1}" -f $Link, $Target)
  }
  try {
    New-Item -ItemType Junction -Path $Link -Value $Target -ErrorAction Stop | Out-Null
  } catch {
    throw "Failed to create junction '$Link' -> '$Target': $($_.Exception.Message)"
  }
}

Ensure-Junction -Link (Join-Path $ObsDir "plugins\$PluginName") -Target $RepoRoot
Ensure-Junction -Link (Join-Path $ObsDir "plugins\log")          -Target $EnvLogDir
Ensure-Junction -Link (Join-Path $RepoRoot "libvpl")             -Target $LibVplDir

# ---------------------------------------------------------------------------
# 6. Register the plugin in OBS's plugins/CMakeLists.txt (same as CI)
# ---------------------------------------------------------------------------
$line = "add_subdirectory($PluginName)"
$obsPluginsCmake = Join-Path $ObsDir "plugins\CMakeLists.txt"
if (-not (Test-Path $obsPluginsCmake)) { throw "OBS plugins/CMakeLists.txt not found at $obsPluginsCmake" }
if (-not (Select-String -Path $obsPluginsCmake -Pattern $line -SimpleMatch -Quiet)) {
  Add-Content -Path $obsPluginsCmake -Value $line -Encoding ASCII
  Write-Host "[patch] added '$line' to $obsPluginsCmake"
} else {
  Write-Host "[skip] OBS plugins/CMakeLists.txt already patched"
}

# ---------------------------------------------------------------------------
# 7. Done
# ---------------------------------------------------------------------------
Write-Host ""
Write-Host "Setup complete. Build with:"
Write-Host "  powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Build-Local.ps1"
Write-Host "Add -UHD600 to also build the UHD600 variant."
Write-Host "The first build downloads the OBS prebuilt deps (obs-deps / Qt6) into"
Write-Host "  $ObsDir\.deps"

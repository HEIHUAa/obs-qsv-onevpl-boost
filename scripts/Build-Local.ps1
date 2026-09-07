#Requires -Version 5.1
<#
.SYNOPSIS
  Configure, build and package obs-qsvonevpl.dll locally (mirrors CI build-uhd700.yml).

.EXAMPLE
  powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Build-Local.ps1
  powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Build-Local.ps1 -UHD600
  powershell -NoProfile -ExecutionPolicy Bypass -File scripts\Build-Local.ps1 -NoConfigure
#>
[CmdletBinding()]
param(
  [switch]$UHD600,         # also build the obs-qsvonevpl-uhd600 variant
  [switch]$NoConfigure,    # skip the cmake configure step
  [string]$Configuration = "Release",
  [string]$EnvDir = ""
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not $EnvDir) { $EnvDir = Join-Path $RepoRoot "build-env" }
$ObsDir = Join-Path $EnvDir "obs-studio"
$PluginDir = "obs-qsv-onevpl"   # source dir name under obs-studio/plugins
$TargetName = "obs-qsvonevpl"   # cmake target name / output dll name

# ---------------------------------------------------------------------------
# 1. Locate CMake (VS-bundled first, then PATH)
# ---------------------------------------------------------------------------
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$cmake = $null
if (Test-Path $vswhere) {
  try {
    $cmake = (& $vswhere -latest -products * -find "Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" |
              Select-Object -First 1)
  } catch { $cmake = $null }
}
if (-not $cmake -or -not (Test-Path $cmake)) {
  $cmd = Get-Command cmake.exe -ErrorAction SilentlyContinue
  if ($cmd) { $cmake = $cmd.Source }
}
if (-not $cmake -or -not (Test-Path $cmake)) { throw "CMake not found." }
Write-Host ("CMake      : {0}" -f $cmake)

if (-not (Test-Path (Join-Path $ObsDir "plugins\$PluginDir"))) {
  throw "Build environment not found under '$ObsDir'. Run scripts\Setup-Local-Build.ps1 first."
}

# ---------------------------------------------------------------------------
# 2. FFmpeg header change detection
#    MSBuild misses wholesale FFmpeg header replacements behind the
#    plugins\log junction, so fingerprint the header tree and force a plugin
#    recompile whenever it changed.
# ---------------------------------------------------------------------------
$buildDir = Join-Path $ObsDir "build_x64"
$FFmpegDir = Join-Path $EnvDir "log\FFmpeg"
$stampFile = Join-Path $buildDir ".ffmpeg-headers.stamp"

function Get-FFmpegFingerprint {
  param([string]$Dir)
  if (-not (Test-Path $Dir)) { return "missing" }
  $parts = @()
  $prevEap = $ErrorActionPreference
  $ErrorActionPreference = "Continue"
  $head = & git -C $Dir rev-parse HEAD 2>$null
  $ErrorActionPreference = $prevEap
  if ($LASTEXITCODE -eq 0 -and $head) { $parts += "git:$head" }
  $releaseFile = Join-Path $Dir "RELEASE"
  if (Test-Path $releaseFile) {
    $parts += "release:" + (Get-Content $releaseFile -Raw).Trim()
  }
  if ($parts.Count -eq 0) {
    # non-git tree without a RELEASE file: fall back to newest header probe
    $probe = Get-ChildItem $Dir -Filter "*.h" -Recurse -ErrorAction SilentlyContinue |
             Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($probe) { $parts += ("t:{0}:{1}" -f $probe.LastWriteTimeUtc.Ticks, $probe.Length) }
  }
  if ($parts.Count -eq 0) { $parts += "unknown" }
  return ($parts -join "|")
}

$ffmpegNow = Get-FFmpegFingerprint -Dir $FFmpegDir
if ($ffmpegNow -eq "missing") {
  Write-Warning "FFmpeg headers not found at '$FFmpegDir' (run scripts\Setup-Local-Build.ps1 to clone them)."
}
$ffmpegPrev = $null
if (Test-Path $stampFile) {
  $ffmpegPrev = (Get-Content $stampFile -Raw).Trim()
}
if ($ffmpegPrev -and $ffmpegNow -ne "missing" -and $ffmpegNow -ne $ffmpegPrev) {
  Write-Host "[detect] FFmpeg headers changed ($ffmpegPrev -> $ffmpegNow); forcing plugin recompile"
  foreach ($t in @($TargetName, "$TargetName-uhd600")) {
    $objDir = Join-Path $buildDir "plugins\$PluginDir\$t\$t.dir"
    if (Test-Path $objDir) {
      Remove-Item $objDir -Recurse -Force
      Write-Host "[clean]  $objDir"
    }
  }
}

# ---------------------------------------------------------------------------
# 3. Configure + build (run inside the OBS source tree, like CI)
# ---------------------------------------------------------------------------
Push-Location $ObsDir
try {
  if (-not $NoConfigure) {
    $mediaSdkInc = (Join-Path $EnvDir "MediaSDK\api\include") -replace "\\", "/"
    $uhd600Arg = "OFF"
    if ($UHD600) { $uhd600Arg = "ON" }
    Write-Host "=== Configure (preset windows-x64) ==="
    & $cmake --preset windows-x64 `
      "-DMEDIASDK_INCLUDE_DIR=$mediaSdkInc" `
      "-DENABLE_BROWSER=OFF" `
      "-DQSV_UHD600_SUPPORT=$uhd600Arg"
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed (exit $LASTEXITCODE)" }
  }

  Write-Host "=== Build obs-qsvonevpl ($Configuration) ==="
  & $cmake --build --preset windows-x64 --config $Configuration --target $TargetName
  if ($LASTEXITCODE -ne 0) { throw "Build failed for $TargetName" }

  if ($UHD600) {
    Write-Host "=== Build obs-qsvonevpl-uhd600 ($Configuration) ==="
    & $cmake --build --preset windows-x64 --config $Configuration --target "$TargetName-uhd600"
    if ($LASTEXITCODE -ne 0) { throw "Build failed for $TargetName-uhd600" }
  }

  # Record the FFmpeg fingerprint only after a successful build
  Set-Content -Path $stampFile -Value $ffmpegNow -Encoding ASCII
} finally { Pop-Location }

# ---------------------------------------------------------------------------
# 4. Package -> <repo>\release\<variant> (same layout as the CI artifact)
# ---------------------------------------------------------------------------

$targets = @()
$stdDir = Join-Path $buildDir "plugins\$PluginDir\$TargetName\$Configuration"
$stdDll = Join-Path $stdDir "$TargetName.dll"
if (Test-Path $stdDll) {
  $targets += [pscustomobject]@{ Variant = "uhd700"; Dll = $stdDll }
} else {
  Write-Warning "$TargetName.dll not found at expected path: $stdDll"
}

if ($UHD600) {
  # The uhd600 variant builds into the same directory as the std target but
  # with a distinct file name (CMakeLists OUTPUT_NAME "obs-qsvonevpl-uhd600")
  # so the two targets never clobber each other's DLL.  The packaging step
  # below renames it to obs-qsvonevpl.dll inside the package.
  $u600Dll = Join-Path $stdDir "$TargetName-uhd600.dll"
  if (Test-Path $u600Dll) {
    $targets += [pscustomobject]@{ Variant = "uhd600"; Dll = $u600Dll }
  } else {
    Write-Warning "$TargetName-uhd600 output not found at expected path: $u600Dll"
  }
}

if ($targets.Count -eq 0) {
  # Fallback: search the build tree once
  Write-Host "Searching build tree for $TargetName.dll ..."
  $hit = Get-ChildItem -Path $buildDir -Recurse -Filter "$TargetName.dll" -File -ErrorAction SilentlyContinue |
         Where-Object { $_.FullName -like "*\$Configuration\*" } | Select-Object -First 1
  if (-not $hit) {
    $hit = Get-ChildItem -Path $buildDir -Recurse -Filter "$TargetName.dll" -File -ErrorAction SilentlyContinue |
           Select-Object -First 1
  }
  if ($hit) { $targets += [pscustomobject]@{ Variant = "uhd700"; Dll = $hit.FullName } }
}
if ($targets.Count -eq 0) { throw "No $TargetName.dll produced under $buildDir" }

$localeSrc = Join-Path $RepoRoot "obs-qsvonevpl\data\locale"

# libvpl.dll is produced by the oneVPL sub-build; ship it alongside if present
$vplDll = Join-Path $buildDir "libvpl\$Configuration\libvpl.dll"
if (-not (Test-Path $vplDll)) {
  $hit = Get-ChildItem -Path $buildDir -Recurse -Filter "libvpl.dll" -File -ErrorAction SilentlyContinue |
         Select-Object -First 1
  if ($hit) { $vplDll = $hit.FullName } else { $vplDll = $null }
}

foreach ($t in $targets) {
  $dest = Join-Path $RepoRoot ("release\" + $t.Variant)
  New-Item -ItemType Directory -Force -Path (Join-Path $dest "obs-plugins\64bit") | Out-Null
  New-Item -ItemType Directory -Force -Path (Join-Path $dest "data\obs-plugins\$TargetName\locale") | Out-Null
  Copy-Item $t.Dll (Join-Path $dest "obs-plugins\64bit\$TargetName.dll") -Force
  Copy-Item (Join-Path $localeSrc "*.ini") (Join-Path $dest "data\obs-plugins\$TargetName\locale") -Force
  if ($vplDll) {
    Copy-Item $vplDll (Join-Path $dest "obs-plugins\64bit\libvpl.dll") -Force
  }
  Write-Host ("[package] {0} -> {1}" -f $t.Variant, $dest)
}

Write-Host ""
Write-Host "Done. Artifacts:"
foreach ($t in $targets) {
  Write-Host ("  release\{0}\obs-plugins\64bit\{1}.dll" -f $t.Variant, $TargetName)
}

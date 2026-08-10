#Requires -Version 5.1
<#
.SYNOPSIS
  Generate compile_commands.json for Sourcetrail (and clangd).

.DESCRIPTION
  Visual Studio CMake generators do not export a compilation database.
  This script configures a dedicated Ninja build tree (build-sourcetrail/)
  with CMAKE_EXPORT_COMPILE_COMMANDS=ON, then copies/symlinks the database
  to the repo root as compile_commands.json.

  After this succeeds, open DarkEngine6.srctrlprj in Sourcetrail and index.
#>
[CmdletBinding()]
param(
    [string]$BuildDir = "build-sourcetrail",
    [switch]$SkipInstallCheck
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $RepoRoot

function Find-Ninja {
    $cmd = Get-Command ninja -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    $candidates = @(
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe",
        "${env:ProgramFiles}\Microsoft Visual Studio\18\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe",
        "${env:ProgramFiles}\Microsoft Visual Studio\18\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe",
        "${env:ProgramFiles}\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) { return $c }
    }

    # Deep search under VS install (slow path).
    $vsRoots = @(
        "${env:ProgramFiles}\Microsoft Visual Studio",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio"
    )
    foreach ($root in $vsRoots) {
        if (-not (Test-Path $root)) { continue }
        $hit = Get-ChildItem -Path $root -Filter ninja.exe -Recurse -ErrorAction SilentlyContinue |
            Select-Object -First 1 -ExpandProperty FullName
        if ($hit) { return $hit }
    }
    return $null
}

function Test-SourcetrailInstalled {
    $paths = @(
        "${env:ProgramFiles}\Sourcetrail\Sourcetrail.exe",
        "${env:ProgramFiles(x86)}\Sourcetrail\Sourcetrail.exe",
        "${env:LocalAppData}\Programs\Sourcetrail\Sourcetrail.exe"
    )
    foreach ($p in $paths) {
        if (Test-Path $p) { return $p }
    }
    $cmd = Get-Command Sourcetrail -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    return $null
}

function Find-VsDevCmd {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $found = & $vswhere -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -find "**/VsDevCmd.bat" 2>$null
        if ($found) {
            if ($found -is [array]) { return $found[0] }
            return $found
        }
    }
    $fallback = @(
        "${env:ProgramFiles}\Microsoft Visual Studio\18\Professional\Common7\Tools\VsDevCmd.bat",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
    )
    foreach ($f in $fallback) {
        if (Test-Path $f) { return $f }
    }
    return $null
}

function Invoke-InVsDevEnv {
    param(
        [Parameter(Mandatory = $true)][string]$CommandLine
    )
    $vsDev = Find-VsDevCmd
    if (-not $vsDev) {
        Write-Error "VsDevCmd.bat not found. Install VS Desktop development with C++."
    }
    Write-Host "    VsDevCmd: $vsDev"
    # Import MSVC env then run the command in the same cmd session.
    # Capture output so it does not pollute the PowerShell return pipeline.
    # CMake writes warnings to stderr; with $ErrorActionPreference=Stop that
    # becomes a terminating NativeCommandError — temporarily relax it.
    $full = "`"$vsDev`" -arch=amd64 -host_arch=amd64 >nul && $CommandLine"
    $prevEap = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $output = & cmd.exe /c $full 2>&1
    $exitCode = $LASTEXITCODE
    $ErrorActionPreference = $prevEap
    foreach ($line in $output) {
        Write-Host "$line"
    }
    if ($null -eq $exitCode) { $exitCode = 0 }
    return [int]$exitCode
}

Write-Host "==> DarkEngine6 Sourcetrail setup" -ForegroundColor Cyan
Write-Host "    Repo: $RepoRoot"

$ninja = Find-Ninja
if (-not $ninja) {
    Write-Error "ninja.exe not found. Install Visual Studio C++ CMake tools, or add ninja to PATH."
}
Write-Host "    Ninja: $ninja"

if (-not $SkipInstallCheck) {
    $st = Test-SourcetrailInstalled
    if ($st) {
        Write-Host "    Sourcetrail: $st"
    }
    else {
        Write-Host "    Sourcetrail: not found on PATH / Program Files" -ForegroundColor Yellow
        Write-Host "    Install with: winget install CoatiSoftware.Sourcetrail" -ForegroundColor Yellow
    }
}

$buildPath = Join-Path $RepoRoot $BuildDir
Write-Host "==> Configuring Ninja build in $BuildDir ..."

# Ninja needs cl.exe on PATH — import the VS x64 developer environment first.
$cmakeCmd = @(
    "cmake -S . -B `"$BuildDir`""
    "-G Ninja"
    "-DCMAKE_BUILD_TYPE=Debug"
    "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
    "-DDE_BUILD_TESTS=ON"
    "-DCMAKE_MAKE_PROGRAM=`"$ninja`""
) -join " "

$cfgExit = Invoke-InVsDevEnv -CommandLine $cmakeCmd
if ($cfgExit -ne 0) {
    Write-Host ""
    Write-Host "CMake Ninja configure failed (exit $cfgExit)." -ForegroundColor Yellow
    Write-Host "Ensure 'Desktop development with C++' is installed in Visual Studio." -ForegroundColor Yellow
    exit $cfgExit
}

$generated = Join-Path $buildPath "compile_commands.json"
if (-not (Test-Path $generated)) {
    Write-Error "Expected compile_commands.json at $generated but it was not created."
}

$dest = Join-Path $RepoRoot "compile_commands.json"
# Prefer a hard copy so tools that resolve relative paths next to the file still work;
# paths inside the JSON use absolute/build-relative directories from CMake.
Copy-Item -Path $generated -Destination $dest -Force
Write-Host "==> Wrote $dest" -ForegroundColor Green

# Filter out third-party noise for a cleaner Sourcetrail index (optional second file).
try {
    $raw = Get-Content $dest -Raw | ConvertFrom-Json
    $engine = @(
        $raw | Where-Object {
            $f = $_.file -replace '\\', '/'
            ($f -notmatch '/_deps/') -and
            ($f -notmatch '/build-sourcetrail/') -and
            ($f -notmatch '/imgui-src/') -and
            ($f -notmatch '/googletest')
        }
    )
    $filteredPath = Join-Path $RepoRoot "compile_commands.engine.json"
    # ConvertTo-Json can mangle on older PS; write carefully.
    $json = $engine | ConvertTo-Json -Depth 8
    # PowerShell may emit a single object if one entry; ensure array.
    if ($engine.Count -eq 1) { $json = "[ $json ]" }
    [System.IO.File]::WriteAllText($filteredPath, $json)
    Write-Host "==> Wrote filtered engine-only DB: $filteredPath ($($engine.Count) TUs)" -ForegroundColor Green
}
catch {
    Write-Host "    (optional filter skipped: $_)" -ForegroundColor DarkYellow
}

Write-Host ""
Write-Host "Next steps:" -ForegroundColor Cyan
Write-Host "  1. Install Sourcetrail if needed:  winget install CoatiSoftware.Sourcetrail"
Write-Host "  2. Open:  $RepoRoot\DarkEngine6.srctrlprj"
Write-Host "  3. Click Start to index (first run can take a few minutes)."
Write-Host "  4. Re-run this script after adding/removing source files, then Refresh in Sourcetrail."
Write-Host ""
Write-Host "Done." -ForegroundColor Green

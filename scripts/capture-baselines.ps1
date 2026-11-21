#Requires -Version 5.1
<#
.SYNOPSIS
    Record the render graph Phase 0 baselines: one PNG + timing JSON per camera preset.

.DESCRIPTION
    Runs Sandbox.exe and Editor.exe once per preset from content/perf/capture_cameras.json with
    -capture-frame, collects the output into docs/perf/baselines/<date>-<sha>-fxc-<config>/ and
    writes summary.md. PIX captures and the GBV list are manual (docs/perf/README.md).

.EXAMPLE
    .\scripts\capture-baselines.ps1 -Config Release
.EXAMPLE
    .\scripts\capture-baselines.ps1 -Config Debug -Gbv -Frame 30 -Average 10 -Preset default
#>
[CmdletBinding()]
param(
    [ValidateSet("Release", "Debug", "RelWithDebInfo")]
    [string]$Config = "Release",
    [string]$BuildDir = "build",
    [int]$Frame = 600,
    [int]$Average = 300,
    [string]$Window = "2560x1440",
    [string[]]$Hosts = @("sandbox", "editor"),
    [string[]]$Preset,
    [string]$SandboxScene,
    [string]$EditorScene,
    [switch]$Gbv,
    [int]$TimeoutSeconds = 900,
    [string]$OutDir
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$BinDir = Join-Path $RepoRoot (Join-Path $BuildDir (Join-Path "bin" $Config))
$CamerasFile = Join-Path $RepoRoot "content\perf\capture_cameras.json"

if (-not (Test-Path -LiteralPath $CamerasFile)) { throw "Missing $CamerasFile" }
$cameras = Get-Content -LiteralPath $CamerasFile -Raw | ConvertFrom-Json

$sha = (& git -C $RepoRoot rev-parse --short HEAD).Trim()
$dirty = (& git -C $RepoRoot status --porcelain --untracked-files=no)
if ($dirty) { $sha = "$sha-dirty" }
if (-not $OutDir) {
    $date = Get-Date -Format "yyyy-MM-dd"
    $OutDir = Join-Path $RepoRoot "docs\perf\baselines\$date-$sha-fxc-$($Config.ToLower())"
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path

$exeFor = @{ sandbox = "Sandbox.exe"; editor = "Editor.exe" }
$sceneFor = @{ sandbox = $SandboxScene; editor = $EditorScene }
$rows = New-Object System.Collections.Generic.List[object]
$failed = 0

if ($Gbv) { $env:DE_D3D12_GBV = "1" } else { Remove-Item Env:DE_D3D12_GBV -ErrorAction SilentlyContinue }

foreach ($h in $Hosts) {
    $exe = Join-Path $BinDir $exeFor[$h]
    if (-not (Test-Path -LiteralPath $exe)) { throw "Missing $exe (build $Config first)" }
    $presets = @($cameras.presets | Where-Object { (-not $_.host) -or ($_.host -eq $h) })
    if ($Preset) { $presets = @($presets | Where-Object { $Preset -contains $_.name }) }

    foreach ($p in $presets) {
        $argList = @("-capture-frame", $Frame, "-capture-avg", $Average, "-camera", $p.name,
                     "-capture-out", "`"$OutDir`"", "-no-vsync", "-window", $Window)
        if ($sceneFor[$h]) { $argList += "-scene:$($sceneFor[$h])" }
        Write-Host "[$h/$($p.name)] $($exeFor[$h]) $($argList -join ' ')"
        $proc = Start-Process -FilePath $exe -ArgumentList $argList -WorkingDirectory $BinDir -PassThru
        if (-not $proc.WaitForExit($TimeoutSeconds * 1000)) {
            Write-Warning "[$h/$($p.name)] timed out after $TimeoutSeconds s; killing"
            $proc.Kill()
        }

        $stem = "$($h)_$($p.name)"
        $jsonPath = Join-Path $OutDir "$stem.json"
        $logSrc = Join-Path $BinDir "DarkEngine6.log"
        if (Test-Path -LiteralPath $logSrc) { Copy-Item -LiteralPath $logSrc -Destination (Join-Path $OutDir "$stem.log") -Force }

        if (-not (Test-Path -LiteralPath $jsonPath)) {
            Write-Warning "[$h/$($p.name)] no report written (exit code $($proc.ExitCode)); see $stem.log"
            $rows.Add([pscustomobject]@{ Stem = $stem; Valid = "NO REPORT"; Gpu = ""; GpuP95 = ""; Cpu = ""; Draws = ""; PassFail = ""; Frames = "" })
            $failed++
            continue
        }
        $r = Get-Content -LiteralPath $jsonPath -Raw | ConvertFrom-Json
        if (-not $r.baselineValid) { $failed++ }
        $rows.Add([pscustomobject]@{
            Stem     = $stem
            Valid    = $(if ($r.baselineValid) { "yes" } else { "NO" })
            Gpu      = "{0:N3}" -f $r.gpuMs.mean
            GpuP95   = "{0:N3}" -f $r.gpuMs.p95
            Cpu      = "{0:N3}" -f $r.cpuMs.mean
            Draws    = "{0:N0}" -f $r.drawCalls.mean
            PassFail = $r.passSequenceFailures
            Frames   = $r.framesAveraged
        })
    }
}

$gpuName = (Get-CimInstance Win32_VideoController | Select-Object -First 1 -ExpandProperty Name)
$cpuName = (Get-CimInstance Win32_Processor | Select-Object -First 1 -ExpandProperty Name)
$lines = @(
    "# Baselines $(Split-Path $OutDir -Leaf)",
    "",
    "- Commit: ``$sha``, config $Config, shader compiler FXC$(if ($Gbv) { ', GBV on (timings not representative)' })",
    "- Window $Window, no vsync, shot at frame $Frame, timings over $Average frames",
    "- GPU: $gpuName; CPU: $cpuName",
    "- Recorded $(Get-Date -Format 'yyyy-MM-dd HH:mm zzz') by scripts/capture-baselines.ps1",
    "- PIX captures: (add paths)",
    "",
    "| Capture | Valid | GPU mean ms | GPU p95 ms | CPU mean ms | Draw calls | Pass-order failures | Frames |",
    "|---------|-------|-------------|------------|-------------|------------|---------------------|--------|"
)
foreach ($row in $rows) {
    $lines += "| $($row.Stem) | $($row.Valid) | $($row.Gpu) | $($row.GpuP95) | $($row.Cpu) | $($row.Draws) | $($row.PassFail) | $($row.Frames) |"
}
$summary = Join-Path $OutDir "summary.md"
Set-Content -LiteralPath $summary -Value $lines -Encoding UTF8
Write-Host ""
Write-Host "Wrote $summary"
if ($failed -gt 0) {
    Write-Warning "$failed capture(s) are not valid baselines"
    exit 1
}

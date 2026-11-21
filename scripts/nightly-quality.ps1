#Requires -Version 5.1
<#
.SYNOPSIS
  Nightly code-quality gate: exception scan, Debug build, unit tests, clang-tidy, clang analyzer.

.DESCRIPTION
  Hard failures (exit 1):
    exception keywords in engine / Sandbox / Editor sources
    Debug build of UnitTests, Sandbox, Sandbox2D, Editor, VisualDebugger
    UnitTests.exe
    a clang-tidy or analyzer finding that is not in a gated baseline

  scripts/quality/tidy-baseline.txt and analyzer-baseline.txt start advisory.
  Advisory means the step still runs and writes a report, and new findings do
  not fail the night. After the first report looks right:

    .\scripts\nightly-quality.ps1 -UpdateBaseline -SkipBuild -SkipTests

  That marks the baselines gated. Later nights fail when a file/check/message
  group has a higher count than the baseline. Fixing an old finding does not
  fail the night. Line numbers are not stored, so inserting a line does not
  either.

  clang-tidy uses .clang-tidy (bugprone, performance, and the small extra set).
  The analyzer pass is separate so Visual Studio's clang-tidy stays fast.
  It skips UnitTests. Include-cleaner stays in the tidy pass; known
  *_More.inl false positives belong in the baseline, and this script never
  applies -Fix.

  The tidy/analyzer steps need compile_commands.json. This script refreshes
  it with scripts/setup-sourcetrail.ps1 unless -SkipCompileDbRefresh is set.
  That Ninja tree is only a compile database. The build step uses -BuildDir
  (the Visual Studio tree, default "build").

  Reports land in build/reports/nightly/<stamp>/.

  Register while you are logged on, so D3D/WARP tests keep a user session.
  Do not register from an elevated prompt if you want /IT (interactive only):

    schtasks /Create /F /TN "DarkEngine6 Nightly Quality" /SC DAILY /ST 02:00 /RL LIMITED /IT /TR "pwsh -NoProfile -ExecutionPolicy Bypass -File D:\DarkDev\DarkEngine6\scripts\nightly-quality.ps1"

.EXAMPLE
  .\scripts\nightly-quality.ps1
.EXAMPLE
  .\scripts\nightly-quality.ps1 -SkipAnalyzer
.EXAMPLE
  .\scripts\nightly-quality.ps1 -UpdateBaseline -SkipBuild -SkipTests
#>
[CmdletBinding()]
param(
    [switch]$SkipExceptions,
    [switch]$SkipBuild,
    [switch]$SkipTests,
    [switch]$SkipTidy,
    [switch]$SkipAnalyzer,
    [switch]$SkipCompileDbRefresh,
    [switch]$UpdateBaseline,

    # Forwarded to clang-tidy. Refused together with -UpdateBaseline, which replaces a whole baseline.
    [string[]]$Path = @(),

    [string[]]$Targets = @("UnitTests", "Sandbox", "Sandbox2D", "Editor", "VisualDebugger"),
    [string]$Config = "Debug",
    [string]$BuildDir = "build",
    [string]$CompileDbDir = "build-sourcetrail",
    [int]$Jobs = [Math]::Max(1, [Environment]::ProcessorCount - 1),
    # 0 picks a small pool. Each analyzer process holds a lot of RAM.
    [int]$AnalyzerJobs = 0,
    [string]$ReportRoot = "build/reports/nightly"
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $RepoRoot

if ($UpdateBaseline -and $Path.Count -gt 0) {
    Write-Error "-UpdateBaseline replaces the whole baseline. Run it without -Path."
}

if ($AnalyzerJobs -le 0) {
    $AnalyzerJobs = [Math]::Max(1, [Math]::Min(4, [int][Math]::Floor([Environment]::ProcessorCount / 2)))
}

# Core analyzer checkers, plus C++ move/delete/virtual-call and dead stores.
# Leaks and UninitializedObject false-positive on COM and on aggregates this
# project already initializes. unix/osx/webkit and the C insecure-API set do
# not match this codebase.
$AnalyzerChecks = @(
    "-*",
    "clang-analyzer-core*",
    "clang-analyzer-cplusplus*",
    "clang-analyzer-deadcode*",
    "clang-analyzer-nullability*",
    "clang-analyzer-security.FloatLoopCounter",
    "clang-analyzer-optin.cplusplus.VirtualCall",
    "-clang-analyzer-cplusplus.NewDeleteLeaks",
    "-clang-analyzer-optin.cplusplus.UninitializedObject",
    "-clang-analyzer-unix*",
    "-clang-analyzer-osx*",
    "-clang-analyzer-webkit*",
    "-clang-analyzer-optin.osx*",
    "-clang-analyzer-optin.performance*",
    "-clang-analyzer-security.insecureAPI*"
) -join ","

$stamp = Get-Date -Format "yyyy-MM-dd_HHmmss"
$relReport = ($ReportRoot.TrimEnd('\', '/') + "/" + $stamp) -replace '\\', '/'
$reportDir = Join-Path $RepoRoot ($relReport -replace '/', '\')
New-Item -ItemType Directory -Force -Path $reportDir | Out-Null
$latestPath = Join-Path $RepoRoot (($ReportRoot.TrimEnd('\', '/') + "/latest.txt") -replace '/', '\')
[System.IO.File]::WriteAllText($latestPath, $reportDir)

$failures = New-Object System.Collections.Generic.List[string]
$summary = New-Object System.Collections.Generic.List[string]

function Add-Summary([string]$line) {
    Write-Host $line
    [void]$summary.Add($line)
}

function Invoke-Step([string]$name, [scriptblock]$body) {
    Add-Summary ""
    Add-Summary "==> $name"
    $sw = [Diagnostics.Stopwatch]::StartNew()
    try {
        & $body
    }
    catch {
        $failures.Add("${name}: $($_.Exception.Message)")
        Add-Summary "FAIL  $name : $($_.Exception.Message)"
    }
    $sw.Stop()
    Add-Summary ("    {0:n0}s" -f $sw.Elapsed.TotalSeconds)
}

function Get-FindingKey($row) {
    $msg = ([string]$row.Message) -replace '\s+', ' '
    return ("{0}`t{1}`t{2}" -f $row.File, $row.Check, $msg.Trim())
}

function Read-Baseline([string]$path) {
    # key -> count. Same file/check/message on several lines counts once per line,
    # so a new constructor with the same warning still fails the night, and a
    # blank line inserted above a finding does not.
    $counts = @{}
    $gated = $false
    if (Test-Path -LiteralPath $path) {
        foreach ($line in [System.IO.File]::ReadAllLines($path)) {
            $t = $line.Trim()
            if ($t -eq '# status: gated') { $gated = $true; continue }
            if (-not $t -or $t.StartsWith('#')) { continue }
            $parts = $t -split "`t", 2
            if ($parts.Count -eq 2 -and $parts[0] -match '^\d+$') {
                $counts[$parts[1]] = [int]$parts[0]
            }
        }
    }
    return [pscustomobject]@{ Gated = $gated; Counts = $counts }
}

function Write-Baseline([string]$path, [hashtable]$counts, [string]$regenHint) {
    $dir = Split-Path -Parent $path
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    $sb = New-Object System.Text.StringBuilder
    [void]$sb.AppendLine("# status: gated")
    [void]$sb.AppendLine("# count, path, check, and message, tab-separated. Line numbers are not included.")
    [void]$sb.AppendLine("# $regenHint")
    foreach ($key in @($counts.Keys) | Sort-Object) {
        [void]$sb.AppendLine(("{0}`t{1}" -f $counts[$key], $key))
    }
    [System.IO.File]::WriteAllText($path, $sb.ToString())
}

function Import-FindingCounts([string]$csvPath) {
    $counts = @{}
    $total = 0
    if (Test-Path -LiteralPath $csvPath) {
        foreach ($row in @(Import-Csv -LiteralPath $csvPath)) {
            if ($row.Check -eq 'clang-diagnostic-error') { continue }
            $total++
            $key = Get-FindingKey $row
            if ($counts.ContainsKey($key)) { $counts[$key] = [int]$counts[$key] + 1 }
            else { $counts[$key] = 1 }
        }
    }
    return [pscustomobject]@{ Total = $total; Counts = $counts }
}

# Returns $true when the step should fail the process.
function Compare-Findings([string]$title, [string]$csvPath, [string]$baselinePath, [string]$regenHint) {
    $run = Import-FindingCounts $csvPath
    Add-Summary "$title findings: $($run.Total)"
    if (-not (Test-Path -LiteralPath $csvPath)) {
        Add-Summary "  No CSV at $csvPath"
    }
    else {
        $rows = @(Import-Csv -LiteralPath $csvPath | Where-Object { $_.Check -ne 'clang-diagnostic-error' })
        $rows | Group-Object Check | Sort-Object Count -Descending | Select-Object -First 12 | ForEach-Object {
            Add-Summary ("    {0,6}  {1}" -f $_.Count, $_.Name)
        }
        $logGuess = $csvPath -replace '\.csv$', '.log'
        if (Test-Path -LiteralPath $logGuess) {
            $parseFails = @(Select-String -LiteralPath $logGuess -Pattern '^Error while processing' -ErrorAction SilentlyContinue)
            if ($parseFails.Count -gt 0) {
                Add-Summary "  $($parseFails.Count) translation unit(s) did not parse under clang. Their results are incomplete."
            }
        }
    }

    if ($UpdateBaseline) {
        Write-Baseline $baselinePath $run.Counts $regenHint
        Add-Summary "  Baseline gated: $baselinePath ($($run.Total) finding(s))"
        return $false
    }

    $base = Read-Baseline $baselinePath
    if (-not $base.Gated) {
        Add-Summary "  Advisory only. Baseline is not gated: $baselinePath"
        Add-Summary "  Re-run with -UpdateBaseline once this report looks right."
        return $false
    }

    $fresh = New-Object System.Collections.Generic.List[string]
    foreach ($key in @($run.Counts.Keys)) {
        $have = 0
        if ($base.Counts.ContainsKey($key)) { $have = [int]$base.Counts[$key] }
        $delta = [int]$run.Counts[$key] - $have
        if ($delta -gt 0) { [void]$fresh.Add(("{0} new`t{1}" -f $delta, $key)) }
    }
    $cleared = 0
    foreach ($key in @($base.Counts.Keys)) {
        $now = 0
        if ($run.Counts.ContainsKey($key)) { $now = [int]$run.Counts[$key] }
        $drop = [int]$base.Counts[$key] - $now
        if ($drop -gt 0) { $cleared += $drop }
    }
    Add-Summary "  New: $($fresh.Count) group(s). Cleared since baseline: $cleared."
    $show = [Math]::Min(30, $fresh.Count)
    for ($i = 0; $i -lt $show; $i++) {
        Add-Summary ("    {0}" -f $fresh[$i])
    }
    if ($fresh.Count -gt $show) {
        Add-Summary "    ... $($fresh.Count - $show) more in $csvPath"
    }
    if ($cleared -gt 0) {
        Add-Summary "  Cleared findings stay in the baseline until -UpdateBaseline."
    }
    return ($fresh.Count -gt 0)
}

$head = "unknown"
try {
    $head = (& git -C $RepoRoot rev-parse --short HEAD 2>$null)
    if (-not $head) { $head = "unknown" }
}
catch { $head = "unknown" }

Add-Summary "DarkEngine6 nightly quality  $stamp"
Add-Summary "Commit $head    Config $Config    Jobs $Jobs    Analyzer jobs $AnalyzerJobs"
Add-Summary "Report $reportDir"

if (-not $SkipExceptions) {
    Invoke-Step "Exception scan" {
        & (Join-Path $PSScriptRoot "check-no-exceptions.ps1")
        if ($LASTEXITCODE -ne 0) { throw "check-no-exceptions.ps1 exited $LASTEXITCODE" }
    }
}

$wantTidy = -not ($SkipTidy -and $SkipAnalyzer)
if ($wantTidy -and -not $SkipCompileDbRefresh) {
    Invoke-Step "Compile database" {
        & (Join-Path $PSScriptRoot "setup-sourcetrail.ps1") -BuildDir $CompileDbDir -SkipInstallCheck
        if ($LASTEXITCODE -ne 0) { throw "setup-sourcetrail.ps1 exited $LASTEXITCODE" }
        $db = Join-Path (Join-Path $RepoRoot $CompileDbDir) "compile_commands.json"
        if (-not (Test-Path -LiteralPath $db)) { throw "No compile database at $db" }
    }
}

if (-not $SkipBuild) {
    Invoke-Step "Debug build" {
        $cmakeArgs = @("--build", $BuildDir, "--config", $Config)
        foreach ($target in $Targets) { $cmakeArgs += @("--target", $target) }
        $log = Join-Path $reportDir "build.log"
        $prev = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        # Capture first. Piping a native command into Tee-Object clears $LASTEXITCODE on Windows PowerShell 5.1.
        $out = & cmake @cmakeArgs 2>&1
        $code = $LASTEXITCODE
        $ErrorActionPreference = $prev
        $out | Tee-Object -FilePath $log | Out-Host
        if ($code -ne 0) { throw "cmake --build exited $code. See $log" }
        Add-Summary "  Built: $($Targets -join ', ')"
    }
}

if (-not $SkipTests) {
    $buildFailed = @($failures | Where-Object { $_.StartsWith("Debug build:") }).Count -gt 0
    if ($buildFailed) {
        Add-Summary ""
        Add-Summary "==> Unit tests skipped because the build failed."
    }
    else {
        Invoke-Step "Unit tests" {
            $exe = Join-Path $RepoRoot (Join-Path $BuildDir "bin\$Config\UnitTests.exe")
            if (-not (Test-Path -LiteralPath $exe)) { throw "UnitTests.exe not found at $exe" }
            $log = Join-Path $reportDir "tests.log"
            $exeDir = Split-Path -Parent $exe
            $xmlName = "nightly-tests.xml"
            $prev = $ErrorActionPreference
            $ErrorActionPreference = "Continue"
            Push-Location $exeDir
            try {
                # gtest treats the colon in "xml:D:\..." as part of the format, so the
                # file is written next to the exe under a name with no drive letter.
                $out = & $exe --gtest_brief=1 --gtest_color=no --gtest_print_time=1 "--gtest_output=xml:$xmlName" 2>&1
                $code = $LASTEXITCODE
            }
            finally {
                Pop-Location
                $ErrorActionPreference = $prev
            }
            $out | Tee-Object -FilePath $log | Out-Host
            $written = Join-Path $exeDir $xmlName
            if (Test-Path -LiteralPath $written) {
                Move-Item -LiteralPath $written -Destination (Join-Path $reportDir "tests.xml") -Force
            }
            if ($code -ne 0) { throw "UnitTests.exe exited $code. See $log" }
            Add-Summary "  UnitTests passed."
        }
    }
}

$hygiene = Join-Path $PSScriptRoot "code-hygiene.ps1"
$pathArgs = @{}
if ($Path.Count -gt 0) { $pathArgs.Path = $Path }

if (-not $SkipTidy) {
    Invoke-Step "clang-tidy" {
        & $hygiene -Mode Tidy -Jobs $Jobs -ReportDir $relReport -BuildDir $CompileDbDir @pathArgs
        if ($LASTEXITCODE -ne 0) { throw "code-hygiene.ps1 (tidy) exited $LASTEXITCODE" }
        $csv = Join-Path $reportDir "clang-tidy-tidy.csv"
        $failed = Compare-Findings "clang-tidy" $csv (Join-Path $PSScriptRoot "quality\tidy-baseline.txt") `
            "Regenerate: .\scripts\nightly-quality.ps1 -UpdateBaseline -SkipBuild -SkipTests -SkipAnalyzer"
        if ($failed) { throw "new clang-tidy findings are not in the gated baseline" }
    }
}

if (-not $SkipAnalyzer) {
    Invoke-Step "clang analyzer" {
        & $hygiene -Mode Tidy -Checks $AnalyzerChecks -Label analyzer -Exclude UnitTests `
            -Jobs $AnalyzerJobs -ReportDir $relReport -BuildDir $CompileDbDir @pathArgs
        if ($LASTEXITCODE -ne 0) { throw "code-hygiene.ps1 (analyzer) exited $LASTEXITCODE" }
        $csv = Join-Path $reportDir "clang-tidy-analyzer.csv"
        $failed = Compare-Findings "analyzer" $csv (Join-Path $PSScriptRoot "quality\analyzer-baseline.txt") `
            "Regenerate: .\scripts\nightly-quality.ps1 -UpdateBaseline -SkipBuild -SkipTests -SkipTidy -SkipExceptions"
        if ($failed) { throw "new analyzer findings are not in the gated baseline" }
    }
}

Add-Summary ""
if ($failures.Count -eq 0) {
    Add-Summary "PASS"
}
else {
    Add-Summary "FAIL ($($failures.Count))"
    foreach ($item in $failures) { Add-Summary "  $item" }
}

$summaryPath = Join-Path $reportDir "summary.txt"
[System.IO.File]::WriteAllLines($summaryPath, $summary)
Write-Host ""
Write-Host "Summary: $summaryPath"

if ($failures.Count -gt 0) { exit 1 }
exit 0

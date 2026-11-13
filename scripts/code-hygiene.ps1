#Requires -Version 5.1
<#
.SYNOPSIS
  Find unused #includes (clang-tidy misc-include-cleaner), run the wider
  clang-tidy check set, and find copy-pasted code (PMD CPD).

.DESCRIPTION
  Modes:
    Includes    (default) misc-include-cleaner only, using .clang-tidy options.
    Tidy        Every check listed in .clang-tidy.
    Duplicates  PMD CPD over engine C++ and content/shaders HLSL.
    All         Includes + Duplicates.

  Uses the Ninja compile database from scripts/setup-sourcetrail.ps1
  (build-sourcetrail/compile_commands.json) and creates it if it is missing.
  Reports go to build/reports/ (git-ignored via build/).

  Keeping an include the tool calls unused (e.g. an intentional umbrella header):
    #include "Render/D3D12Common.h" // IWYU pragma: keep     (on the include line)
    // IWYU pragma: export                                   (in a header, on includes it re-exports)
  Whole SDK / third-party headers are already ignored in .clang-tidy (IgnoreHeaders).
  Only .cpp files in the compile database are analysed, so unused includes inside
  headers show up only when a .cpp is checked against them (clangd shows them live).

  Requirements:
    clang-tidy 18+   VS "C++ Clang tools for Windows" component, or winget install LLVM.LLVM
    PMD 7 + Java 11+ only for -Mode Duplicates/All. Set PMD_HOME or put pmd.bat on PATH.
                     https://pmd.github.io/  (winget install EclipseAdoptium.Temurin.21.JRE)

.EXAMPLE
  .\scripts\code-hygiene.ps1                          # unused includes, whole engine
.EXAMPLE
  .\scripts\code-hygiene.ps1 -Path Render,Water -Fix  # remove unused includes (and add direct ones) in two folders
.EXAMPLE
  .\scripts\code-hygiene.ps1 -MissingIncludes         # also report symbols only reached transitively
.EXAMPLE
  .\scripts\code-hygiene.ps1 -Mode Duplicates -MinTokens 75 -IgnoreIdentifiers
.EXAMPLE
  .\scripts\code-hygiene.ps1 -Mode All -FailOnFindings  # CI: exit 1 if anything is found
#>
[CmdletBinding()]
param(
    [ValidateSet("Includes", "Tidy", "Duplicates", "All")]
    [string]$Mode = "Includes",

    # Only check translation units whose repo-relative path starts with one of these (e.g. Render, Core/Json.cpp).
    [string[]]$Path = @(),

    # Apply clang-tidy fixes in place (removes unused includes). Review the diff before committing.
    [switch]$Fix,

    # Also report symbols that are used but only reach the file through another header.
    [switch]$MissingIncludes,

    [int]$Jobs = [Math]::Max(1, [Environment]::ProcessorCount - 1),
    [string]$BuildDir = "build-sourcetrail",
    [string]$ReportDir = "build/reports",

    # CPD: smallest duplicate to report, in tokens (about 8-10 tokens per line of C++).
    [int]$MinTokens = 100,
    # CPD: also match copies where variable names or literal values differ.
    [switch]$IgnoreIdentifiers,

    [switch]$FailOnFindings
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$ReportPath = Join-Path $RepoRoot $ReportDir
New-Item -ItemType Directory -Force -Path $ReportPath | Out-Null

# Top-level folders that are not engine source.
$NonEngineDirs = @("third_party", "content", "docs", "scripts", "cmake", "Saves", "out", ".git", ".vs", ".vscode", ".github")
$ThirdPartyRegex = '[\\/](third_party|_deps|imgui-src|googletest[^\\/]*|build[^\\/]*)[\\/]'

$totalFindings = 0

# ---------------------------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------------------------
function Get-RelPath([string]$full) {
    $f = [System.IO.Path]::GetFullPath($full)
    if ($f.StartsWith($RepoRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $f.Substring($RepoRoot.Length).TrimStart('\', '/') -replace '\\', '/'
    }
    return $f -replace '\\', '/'
}

function Find-ClangTidy {
    $cmd = Get-Command clang-tidy -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $candidates = @()
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vsPaths = & $vswhere -all -products * -property installationPath 2>$null
        foreach ($vs in @($vsPaths)) {
            if ($vs) { $candidates += (Join-Path $vs "VC\Tools\Llvm\x64\bin\clang-tidy.exe") }
        }
    }
    $candidates += "${env:ProgramFiles}\LLVM\bin\clang-tidy.exe"
    foreach ($c in $candidates) { if (Test-Path $c) { return $c } }
    return $null
}

function Get-ClangTidyMajor([string]$exe) {
    $text = (& $exe --version 2>&1 | Out-String)
    if ($text -match 'version\s+(\d+)\.') { return [int]$Matches[1] }
    return 0
}

function Find-Pmd {
    if ($env:PMD_HOME) {
        $names = if ($env:OS -eq "Windows_NT") { @("pmd.bat") } else { @("pmd") }
        foreach ($n in $names) {
            $p = Join-Path (Join-Path $env:PMD_HOME "bin") $n
            if (Test-Path $p) { return $p }
        }
    }
    $cmd = Get-Command pmd.bat -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    $cmd = Get-Command pmd -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    foreach ($root in @("C:\tools", "${env:ProgramFiles}", "$env:LOCALAPPDATA\Programs")) {
        if (-not (Test-Path $root)) { continue }
        $hit = Get-ChildItem -Path $root -Directory -Filter "pmd*" -ErrorAction SilentlyContinue |
            ForEach-Object { Join-Path $_.FullName "bin\pmd.bat" } |
            Where-Object { Test-Path $_ } | Select-Object -First 1
        if ($hit) { return $hit }
    }
    return $null
}

function Get-EngineDirs {
    Get-ChildItem -Path $RepoRoot -Directory |
        Where-Object { ($NonEngineDirs -notcontains $_.Name) -and ($_.Name -notlike "build*") } |
        Where-Object {
            @(Get-ChildItem -Path $_.FullName -Recurse -File -Include *.cpp, *.h, *.hpp, *.inl -ErrorAction SilentlyContinue |
                Select-Object -First 1).Count -gt 0
        } |
        ForEach-Object { $_.FullName }
}

# ---------------------------------------------------------------------------------------------
# clang-tidy (Includes / Tidy)
# ---------------------------------------------------------------------------------------------
function Invoke-ClangTidy([bool]$includesOnly) {
    $exe = Find-ClangTidy
    if (-not $exe) {
        Write-Error "clang-tidy not found. Install the VS component 'C++ Clang tools for Windows' or: winget install LLVM.LLVM"
    }
    $major = Get-ClangTidyMajor $exe
    Write-Host "clang-tidy: $exe (LLVM $major)"
    if ($major -lt 18) { Write-Error "clang-tidy 18 or newer is required (found $major)." }

    $buildPath = Join-Path $RepoRoot $BuildDir
    $db = Join-Path $buildPath "compile_commands.json"
    if (-not (Test-Path $db)) {
        Write-Host "==> No compile database at $db; running setup-sourcetrail.ps1 ..."
        & (Join-Path $PSScriptRoot "setup-sourcetrail.ps1") -BuildDir $BuildDir -SkipInstallCheck
        if (-not (Test-Path $db)) { Write-Error "setup-sourcetrail.ps1 did not produce $db" }
    }

    # Engine translation units only, de-duplicated, optionally filtered by -Path.
    $entries = Get-Content $db -Raw | ConvertFrom-Json
    $files = @(
        $entries | ForEach-Object {
            $f = $_.file
            if (-not [System.IO.Path]::IsPathRooted($f)) { $f = Join-Path $_.directory $f }
            [System.IO.Path]::GetFullPath($f)
        } |
        Where-Object { $_.StartsWith($RepoRoot, [System.StringComparison]::OrdinalIgnoreCase) -and ($_ -notmatch $ThirdPartyRegex) } |
        Sort-Object -Unique
    )
    if ($Path.Count -gt 0) {
        $prefixes = $Path | ForEach-Object { ($_ -replace '\\', '/').TrimStart('./').TrimEnd('/') }
        $files = @($files | Where-Object {
                $rel = Get-RelPath $_
                @($prefixes | Where-Object { $rel -eq $_ -or $rel.StartsWith("$_/", [System.StringComparison]::OrdinalIgnoreCase) }).Count -gt 0
            })
    }
    if ($files.Count -eq 0) { Write-Error "No translation units matched." }

    $label = if ($includesOnly) { "includes" } else { "tidy" }
    $jobsToUse = $Jobs
    if ($Fix -and -not $includesOnly -and $jobsToUse -gt 1) {
        # Wider checks can edit shared headers; parallel -fix would race on them.
        Write-Host "    -Fix with -Mode Tidy runs one file at a time so header edits don't collide."
        $jobsToUse = 1
    }
    Write-Host "==> clang-tidy ($label) on $($files.Count) translation unit(s), $jobsToUse job(s)"

    $common = @("-p", "`"$buildPath`"", "--quiet", "--extra-arg=/w")
    if ($includesOnly) { $common += "--checks=-*,misc-include-cleaner" }
    # Removing an include that only supplied symbols transitively breaks the build unless the
    # direct header is added in the same pass, so -Fix always adds missing includes too.
    $addMissing = $MissingIncludes -or ($Fix -and $includesOnly)
    if ($Fix -and $includesOnly -and -not $MissingIncludes) {
        Write-Host "    -Fix also adds missing direct includes so removals don't break the build."
    }
    if ($addMissing) {
        $common += "--config=`"{InheritParentConfig: true, CheckOptions: {misc-include-cleaner.MissingIncludes: true}}`""
    }
    if ($major -ge 19) { $common += "--exclude-header-filter=`".*[\\/](third_party|_deps|imgui-src)[\\/].*`"" }
    if ($Fix) { $common += "--fix" }

    $tmp = Join-Path $ReportPath "tidy-tmp"
    if (Test-Path $tmp) { Remove-Item $tmp -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $tmp | Out-Null

    # Simple process pool (works on Windows PowerShell 5.1 and PowerShell 7).
    $running = New-Object System.Collections.ArrayList
    $i = 0
    $done = 0
    foreach ($file in $files) {
        while ($running.Count -ge $jobsToUse) {
            Start-Sleep -Milliseconds 100
            foreach ($p in @($running)) { if ($p.HasExited) { [void]$running.Remove($p); $done++ } }
        }
        $i++
        $out = Join-Path $tmp ("{0:D5}.out" -f $i)
        $err = Join-Path $tmp ("{0:D5}.err" -f $i)
        $argLine = ($common + "`"$file`"") -join " "
        $proc = Start-Process -FilePath $exe -ArgumentList $argLine -NoNewWindow -PassThru `
            -RedirectStandardOutput $out -RedirectStandardError $err
        [void]$running.Add($proc)
        if (($i % 25) -eq 0) { Write-Host ("    {0}/{1} started" -f $i, $files.Count) }
    }
    foreach ($p in @($running)) { $p.WaitForExit() }

    # Collect diagnostics: path:line:col: warning|error: message [check]
    $diagRegex = '^(?<file>[A-Za-z]:[\\/].+?|/.+?):(?<line>\d+):(?<col>\d+):\s+(?<sev>warning|error):\s+(?<msg>.+?)\s+\[(?<check>[^\]]+)\]\s*$'
    $seen = @{}
    $rows = New-Object System.Collections.ArrayList
    $failedTUs = New-Object System.Collections.ArrayList
    $logPath = Join-Path $ReportPath "clang-tidy-$label.log"
    $log = New-Object System.Text.StringBuilder
    foreach ($f in (Get-ChildItem $tmp -File | Sort-Object Name)) {
        $text = Get-Content $f.FullName -Raw -ErrorAction SilentlyContinue
        if (-not $text) { continue }
        [void]$log.Append($text)
        foreach ($line in ($text -split "`r?`n")) {
            if ($line -match '^Error while processing (.+)\.$') { [void]$failedTUs.Add((Get-RelPath $Matches[1])); continue }
            if ($line -notmatch $diagRegex) { continue }
            $rel = Get-RelPath $Matches['file']
            if ($rel -match '^(third_party|_deps)/') { continue }
            $key = "$rel|$($Matches['line'])|$($Matches['check'])|$($Matches['msg'])"
            if ($seen.ContainsKey($key)) { continue }   # headers are reported once per including TU
            $seen[$key] = $true
            [void]$rows.Add([pscustomobject]@{
                    File     = $rel
                    Line     = [int]$Matches['line']
                    Severity = $Matches['sev']
                    Check    = $Matches['check']
                    Message  = $Matches['msg']
                })
        }
    }
    [System.IO.File]::WriteAllText($logPath, $log.ToString())
    Remove-Item $tmp -Recurse -Force

    $csvPath = Join-Path $ReportPath "clang-tidy-$label.csv"
    $rows | Sort-Object File, Line | Export-Csv -Path $csvPath -NoTypeInformation -Encoding UTF8

    $findings = @($rows | Where-Object { $_.Check -ne 'clang-diagnostic-error' })
    Write-Host ""
    Write-Host "clang-tidy ($label): $($findings.Count) finding(s) in $(@($findings | Select-Object -Unique -ExpandProperty File).Count) file(s)"
    $findings | Group-Object Check | Sort-Object Count -Descending | Select-Object -First 15 |
        ForEach-Object { Write-Host ("    {0,6}  {1}" -f $_.Count, $_.Name) }
    if ($includesOnly -and $findings.Count -gt 0) {
        Write-Host "  Files with the most include findings:"
        $findings | Group-Object File | Sort-Object Count -Descending | Select-Object -First 10 |
            ForEach-Object { Write-Host ("    {0,4}  {1}" -f $_.Count, $_.Name) }
    }
    if ($failedTUs.Count -gt 0) {
        Write-Host ""
        Write-Host "  $($failedTUs.Count) file(s) did not parse under clang; their results are incomplete:" -ForegroundColor Yellow
        $failedTUs | Sort-Object -Unique | Select-Object -First 10 | ForEach-Object { Write-Host "    $_" -ForegroundColor Yellow }
        Write-Host "  See $logPath for the compiler errors." -ForegroundColor Yellow
    }
    Write-Host "  Details: $csvPath"
    if ($Fix) { Write-Host "  Fixes were applied in place. Build Editor and Sandbox, then review 'git diff' before committing." -ForegroundColor Cyan }
    return $findings.Count
}

# ---------------------------------------------------------------------------------------------
# PMD CPD (Duplicates)
# ---------------------------------------------------------------------------------------------
function Invoke-Cpd {
    $pmd = Find-Pmd
    if (-not $pmd) {
        Write-Error "PMD not found. Download PMD 7 from https://pmd.github.io/, unzip (e.g. C:\tools\pmd-bin-7.x), and set PMD_HOME. Needs Java 11+."
    }
    if (-not (Get-Command java -ErrorAction SilentlyContinue) -and -not $env:JAVA_HOME) {
        Write-Error "Java not found. Install a JRE 11+ (winget install EclipseAdoptium.Temurin.21.JRE) or set JAVA_HOME."
    }
    Write-Host "PMD: $pmd"

    $common = @("cpd", "--minimum-tokens", "$MinTokens", "--format", "text", "--no-fail-on-violation", "--language", "cpp")
    if ($IgnoreIdentifiers) { $common += @("--ignore-identifiers", "--ignore-literals") }

    # CPD's C++ lexer only picks up C/C++ file extensions and has no HLSL lexer, so the
    # shaders are copied to a staging folder as <name>.hlsl.h and the paths are mapped
    # back in the report. HLSL is close enough to C++ for copy detection.
    $shaderDir = Join-Path (Join-Path $RepoRoot "content") "shaders"
    $stage = Join-Path $ReportPath "cpd-hlsl-stage"
    if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
    if ($Path.Count -eq 0 -and (Test-Path $shaderDir)) {
        New-Item -ItemType Directory -Force -Path $stage | Out-Null
        Get-ChildItem -Path $shaderDir -Recurse -File -Include *.hlsl, *.hlsli | ForEach-Object {
            $rel = $_.FullName.Substring($shaderDir.Length).TrimStart('\', '/')
            $dest = Join-Path $stage ($rel + ".h")
            New-Item -ItemType Directory -Force -Path (Split-Path $dest) | Out-Null
            Copy-Item $_.FullName $dest
        }
    }

    $cppDirs = if ($Path.Count -gt 0) {
        @($Path | ForEach-Object { Join-Path $RepoRoot $_ } | Where-Object { Test-Path $_ })
    } else { @(Get-EngineDirs) }

    $sets = @(
        @{ Name = "cpp";  Dirs = $cppDirs },
        @{ Name = "hlsl"; Dirs = @($stage | Where-Object { Test-Path $_ }) }
    )

    $count = 0
    foreach ($set in $sets) {
        $dirs = @($set.Dirs)
        if ($dirs.Count -eq 0) { continue }
        $report = Join-Path $ReportPath "cpd-$($set.Name).txt"
        $errFile = Join-Path $ReportPath "cpd-$($set.Name).err.txt"
        $cpdArgs = @($common)
        foreach ($d in $dirs) { $cpdArgs += @("--dir", "`"$d`"") }

        Write-Host "==> CPD ($($set.Name)) over $($dirs.Count) folder(s), min $MinTokens tokens"
        $p = Start-Process -FilePath $pmd -ArgumentList ($cpdArgs -join " ") -NoNewWindow -PassThru -Wait `
            -RedirectStandardOutput $report -RedirectStandardError $errFile
        if ($p.ExitCode -ne 0 -and $p.ExitCode -ne 4) {
            Write-Error "CPD failed (exit $($p.ExitCode)). See $errFile"
        }

        $text = Get-Content $report -Raw
        if (-not $text) { $text = "" }
        if ($set.Name -eq "hlsl") {
            # Point the report back at the real shader files.
            $text = $text.Replace($stage, $shaderDir) -replace '(\.hlsli?)\.h\b', '$1'
            [System.IO.File]::WriteAllText($report, $text)
            Remove-Item $stage -Recurse -Force
        }
        $hits = [regex]::Matches($text, 'Found a (\d+) line \((\d+) tokens\) duplication')
        $lines = 0
        foreach ($h in $hits) { $lines += [int]$h.Groups[1].Value }
        Write-Host ("CPD ({0}): {1} duplicate block(s), {2} duplicated line(s). Report: {3}" -f $set.Name, $hits.Count, $lines, $report)
        $count += $hits.Count
    }
    return $count
}

# ---------------------------------------------------------------------------------------------
switch ($Mode) {
    "Includes"   { $totalFindings += Invoke-ClangTidy $true }
    "Tidy"       { $totalFindings += Invoke-ClangTidy $false }
    "Duplicates" { $totalFindings += Invoke-Cpd }
    "All"        { $totalFindings += Invoke-ClangTidy $true; $totalFindings += Invoke-Cpd }
}

Write-Host ""
if ($FailOnFindings -and $totalFindings -gt 0) {
    Write-Host "$totalFindings finding(s); failing because -FailOnFindings was set." -ForegroundColor Yellow
    exit 1
}
exit 0

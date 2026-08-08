#Requires -Version 5.1
<#
.SYNOPSIS
    Format (or check) all DarkEngine6 C/C++ sources with clang-format.

.DESCRIPTION
    Uses the .clang-format at the repo root. Discovers clang-format from PATH or
    common Visual Studio / LLVM install locations (prefers runnable x64 on this host).

.PARAMETER Check
    Do not write files. Exit 1 if any file would change (CI-friendly).

.PARAMETER Path
    Optional files or directories to format instead of the whole engine tree.

.EXAMPLE
    .\scripts\format-all.ps1
    .\scripts\format-all.ps1 -Check
    .\scripts\format-all.ps1 -Path Render, Math\Vector3f.h
    .\scripts\format-all.ps1 -Verbose
#>
[CmdletBinding()]
param(
    [switch]$Check,
    [string[]]$Path
)

$ErrorActionPreference = "Stop"

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$StyleFile = Join-Path $RepoRoot ".clang-format"

if (-not (Test-Path $StyleFile)) {
    Write-Error "Missing .clang-format at $StyleFile"
}

function Test-ClangFormatBinary {
    param([string]$ExePath)
    if (-not $ExePath -or -not (Test-Path -LiteralPath $ExePath)) { return $false }
    $oldEap = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $null = & $ExePath --version 2>&1
        return ($LASTEXITCODE -eq 0)
    } catch {
        return $false
    } finally {
        $ErrorActionPreference = $oldEap
    }
}

function Find-ClangFormat {
    $isArmHost = $env:PROCESSOR_ARCHITECTURE -match "ARM" -or $env:PROCESSOR_IDENTIFIER -match "ARM"

    $fromPath = Get-Command clang-format -ErrorAction SilentlyContinue
    if ($fromPath -and (Test-ClangFormatBinary $fromPath.Source)) {
        return $fromPath.Source
    }

    $candidates = New-Object System.Collections.Generic.List[string]
    $vsRoots = @(
        "${env:ProgramFiles}\Microsoft Visual Studio",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio"
    )

    $allFound = New-Object System.Collections.Generic.List[string]
    foreach ($root in $vsRoots) {
        if (-not (Test-Path $root)) { continue }
        Get-ChildItem -Path $root -Recurse -Filter "clang-format.exe" -ErrorAction SilentlyContinue |
            ForEach-Object { [void]$allFound.Add($_.FullName) }
    }

    $orderedPatterns = if ($isArmHost) {
        @("\\ARM64\\bin\\clang-format\.exe$", "\\x64\\bin\\clang-format\.exe$", "\\bin\\clang-format\.exe$")
    } else {
        @("\\x64\\bin\\clang-format\.exe$", "\\bin\\clang-format\.exe$")
    }

    foreach ($pat in $orderedPatterns) {
        foreach ($f in $allFound) {
            if ($f -match $pat) {
                if (-not $isArmHost -and $f -match "\\ARM64\\") { continue }
                if (-not $candidates.Contains($f)) { [void]$candidates.Add($f) }
            }
        }
    }
    foreach ($f in $allFound) {
        if (-not $isArmHost -and $f -match "\\ARM64\\") { continue }
        if (-not $candidates.Contains($f)) { [void]$candidates.Add($f) }
    }

    foreach ($extra in @(
        "${env:ProgramFiles}\LLVM\bin\clang-format.exe",
        "${env:ProgramFiles(x86)}\LLVM\bin\clang-format.exe"
    )) {
        if (-not $candidates.Contains($extra)) { [void]$candidates.Add($extra) }
    }

    foreach ($c in $candidates) {
        if (Test-ClangFormatBinary $c) { return $c }
    }
    return $null
}

$ClangFormat = Find-ClangFormat
if (-not $ClangFormat) {
    Write-Error @"
clang-format not found (or no runnable binary for this CPU).
Install LLVM, or use Visual Studio's bundled tool:
  VC\Tools\Llvm\x64\bin\clang-format.exe
"@
}

Write-Host "clang-format: $ClangFormat"
& $ClangFormat --version
Write-Host "style:       $StyleFile"
Write-Host ""

$extensions = @("*.h", "*.hpp", "*.hh", "*.inl", "*.c", "*.cc", "*.cpp", "*.cxx")
$engineFolders = @(
    "AI", "Assets", "Collision", "Core", "ECS", "Geometry",
    "Math", "Network", "Render", "Sandbox", "Shaders", "UnitTests"
)

function Get-SourceFiles {
    param([string[]]$Roots)

    $files = New-Object System.Collections.Generic.List[string]
    foreach ($root in $Roots) {
        $full = if ([System.IO.Path]::IsPathRooted($root)) { $root } else { Join-Path $RepoRoot $root }
        if (-not (Test-Path -LiteralPath $full)) {
            Write-Warning "Path not found: $full"
            continue
        }
        $item = Get-Item -LiteralPath $full
        if (-not $item.PSIsContainer) {
            [void]$files.Add($item.FullName)
            continue
        }
        foreach ($ext in $extensions) {
            Get-ChildItem -Path $item.FullName -Recurse -File -Filter $ext -ErrorAction SilentlyContinue |
                ForEach-Object { [void]$files.Add($_.FullName) }
        }
    }
    return @($files | Sort-Object -Unique)
}

if ($Path -and $Path.Count -gt 0) {
    $targets = Get-SourceFiles -Roots $Path
} else {
    $targets = Get-SourceFiles -Roots $engineFolders
}

if (-not $targets -or $targets.Count -eq 0) {
    Write-Error "No source files found to format."
}

$changed = 0
$checked = 0
$failed = 0
$repoPrefix = $RepoRoot.Path.TrimEnd("\", "/")

Push-Location $RepoRoot
try {
    foreach ($file in $targets) {
        $checked++
        $rel = if ($file.StartsWith($repoPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            $file.Substring($repoPrefix.Length).TrimStart("\", "/")
        } else {
            $file
        }

        $oldEap = $ErrorActionPreference
        $ErrorActionPreference = "Continue"

        if ($Check) {
            $null = & $ClangFormat -style=file --dry-run --Werror -- $file 2>&1
            $exit = $LASTEXITCODE
            $ErrorActionPreference = $oldEap
            if ($exit -ne 0) {
                $changed++
                Write-Host "[would change] $rel"
            } elseif ($VerbosePreference -eq "Continue") {
                Write-Host "[ok] $rel"
            }
        } else {
            try {
                $before = [System.IO.File]::ReadAllBytes($file)
                $null = & $ClangFormat -style=file -i -- $file 2>&1
                $exit = $LASTEXITCODE
                $ErrorActionPreference = $oldEap
                if ($exit -ne 0) {
                    $failed++
                    Write-Host "[error] $rel (exit $exit)"
                    continue
                }
                $after = [System.IO.File]::ReadAllBytes($file)
                $same = ($before.Length -eq $after.Length)
                if ($same) {
                    for ($i = 0; $i -lt $before.Length; $i++) {
                        if ($before[$i] -ne $after[$i]) { $same = $false; break }
                    }
                }
                if (-not $same) {
                    $changed++
                    Write-Host "[formatted] $rel"
                } elseif ($VerbosePreference -eq "Continue") {
                    Write-Host "[unchanged] $rel"
                }
            } catch {
                $ErrorActionPreference = $oldEap
                $failed++
                Write-Host "[error] $rel : $_"
            }
        }
    }
} finally {
    Pop-Location
}

Write-Host ""
if ($Check) {
    Write-Host "Checked $checked file(s); $changed would change."
    if ($changed -gt 0) { exit 1 }
    Write-Host "All files match .clang-format."
    exit 0
} else {
    Write-Host "Processed $checked file(s); formatted $changed; errors $failed."
    if ($failed -gt 0) { exit 1 }
    exit 0
}
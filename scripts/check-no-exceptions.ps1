#Requires -Version 5.1
<#
.SYNOPSIS
    Fail if engine/Sandbox sources introduce try/catch/throw (or std exception types).

.DESCRIPTION
    Scans DarkEngine + Sandbox (+ optional paths) for exception keywords.
    Intended for local pre-commit or CI. Does not scan UnitTests or third-party.

.EXAMPLE
    .\scripts\check-no-exceptions.ps1
#>
[CmdletBinding()]
param(
    [string[]]$Path
)

$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")

$defaultRoots = @(
    "AI", "Assets", "Audio", "Collision", "Core", "ECS", "Editor", "Geometry",
    "Math", "Network", "Render", "Sandbox", "Sandbox2D", "Shaders"
)

$roots = if ($Path -and $Path.Count -gt 0) { $Path } else { $defaultRoots }

# Match control-flow exception usage; allow the word noexcept.
$pattern = '(?i)(?<!no)\b(try|catch|throw)\b|\bstd::(exception|runtime_error|invalid_argument|logic_error|out_of_range)\b'

$extensions = @("*.cpp", "*.cxx", "*.cc", "*.c", "*.h", "*.hpp", "*.hh", "*.inl")
$hits = New-Object System.Collections.Generic.List[string]

foreach ($root in $roots) {
    $full = if ([System.IO.Path]::IsPathRooted($root)) { $root } else { Join-Path $RepoRoot $root }
    if (-not (Test-Path -LiteralPath $full)) { continue }

    foreach ($ext in $extensions) {
        Get-ChildItem -Path $full -Recurse -File -Filter $ext -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -notmatch '\\build\\|\\_deps\\' } |
            ForEach-Object {
                $lines = Get-Content -LiteralPath $_.FullName
                for ($i = 0; $i -lt $lines.Count; $i++) {
                    $line = $lines[$i]
                    # Skip obvious comments-only lines that mention the policy.
                    if ($line -match '^\s*//') { continue }
                    if ($line -match $pattern) {
                        $rel = $_.FullName.Substring($RepoRoot.Path.Length).TrimStart("\", "/")
                        [void]$hits.Add(("{0}:{1}: {2}" -f $rel, ($i + 1), $line.Trim()))
                    }
                }
            }
    }
}

if ($hits.Count -eq 0) {
    Write-Host "OK: no try/catch/throw (or std exception types) in scanned engine/Sandbox sources."
    exit 0
}

Write-Host "FAIL: exception usage found ($($hits.Count) hit(s)):"
$hits | ForEach-Object { Write-Host "  $_" }
Write-Host ""
Write-Host "DarkEngine/Sandbox must use return codes + logging, not exceptions. See AGENTS.md."
exit 1

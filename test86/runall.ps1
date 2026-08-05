#!/usr/bin/env pwsh
#Requires -Version 7
<#
.SYNOPSIS
Runs every test86 JSON test file, one worker per core, and prints a summary.

.DESCRIPTION
Cross-platform (Linux, Windows, macOS) equivalent of runall.sh. Runs each
*.json file found in the test folder -- and its undocumented/ subfolder, if
present -- against ./test86 (test86.exe on Windows), in parallel across all
CPU cores, and prints a pass/fail summary. Exits with the number of failed
files (0 if all passed).

.PARAMETER TestDir
  Folder holding the JSON test files, overriding the default "tests"
  (resolved relative to this script's directory unless given as an absolute
  path). Files are also picked up from <TestDir>/undocumented if it exists.

.EXAMPLE
  pwsh ./runall.ps1
  pwsh ./runall.ps1 tests/undocumented
  pwsh ./runall.ps1 -TestDir /some/other/tests

.NOTES
  tests/*.json (~10,000 hardware-validated cases per opcode) are NOT checked
  into this repo (several GB -- see .gitignore) and must be fetched and
  converted separately. That corpus originates from Tom Harte's
  ProcessorTests project (github.com/TomHarte/ProcessorTests, now archived,
  redirects to github.com/SingleStepTests/ProcessorTests), generated with
  Folkert van Heusden's assistance; its current, actively-maintained
  standalone home is github.com/SingleStepTests/8088, the `v1` folder
  specifically (NOT `v2`, which is a different, incompatible format from
  newer/different hardware). See convert_singlesteptests.py in this
  directory for the exact conversion required and a clone-to-running-tests
  example.
#>

param(
    [Parameter(Position = 0)]
    [string]$TestDir = "tests"
)

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

$exeName = if ($IsWindows) { "test86.exe" } else { "test86" }
$exePath = Join-Path (Get-Location) $exeName
if (-not (Test-Path -LiteralPath $exePath -PathType Leaf)) {
    Write-Error "Executable not found: $exePath (build it first)"
    exit 1
}

if (-not (Test-Path -LiteralPath $TestDir -PathType Container)) {
    Write-Error "Test directory not found: $TestDir"
    exit 1
}

$testFiles = @(Get-ChildItem -LiteralPath $TestDir -Filter *.json -File)
$undocDir = Join-Path $TestDir "undocumented"
if (Test-Path -LiteralPath $undocDir -PathType Container) {
    $testFiles += @(Get-ChildItem -LiteralPath $undocDir -Filter *.json -File)
}

if ($testFiles.Count -eq 0) {
    Write-Error "No *.json test files found under $TestDir"
    exit 1
}

$workerCount = [Environment]::ProcessorCount
$outDir = Join-Path ([System.IO.Path]::GetTempPath()) ("test86-runall-" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $outDir -Force | Out-Null

$failed = 0
$total = 0
try {
    $sw = [System.Diagnostics.Stopwatch]::StartNew()

    $testFiles | ForEach-Object -Parallel {
        $outFile = Join-Path $using:outDir ($_.Name + ".out")
        & $using:exePath $_.FullName *> $outFile
    } -ThrottleLimit $workerCount

    $sw.Stop()

    foreach ($f in Get-ChildItem -LiteralPath $outDir -Filter *.out | Sort-Object Name) {
        $total++
        $content = Get-Content -LiteralPath $f.FullName -Raw
        if ($null -eq $content -or $content -notmatch "great success") {
            $failed++
            Write-Host "=== $($f.BaseName) ==="
            Get-Content -LiteralPath $f.FullName | Select-Object -Last 5 | ForEach-Object { Write-Host $_ }
        }
    }

    $elapsed = "{0:N1}" -f $sw.Elapsed.TotalSeconds
    Write-Host "----------------------------------------"
    Write-Host "$total files, $($total - $failed) passed, $failed failed, ${elapsed}s wall clock, $workerCount workers"
}
finally {
    Remove-Item -LiteralPath $outDir -Recurse -Force -ErrorAction SilentlyContinue
}

exit $failed

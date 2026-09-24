<#
.SYNOPSIS
  Build, flash and test Ammonite from PowerShell.

.DESCRIPTION
  The Daisy Makefiles use POSIX shell commands, so the firmware steps hand the
  work to Git Bash. The Python steps use $env:AMMONITE_PYTHON if it is set
  (a python.exe with numpy, pygame, sounddevice and ziglang), else "python".

.EXAMPLE
  .\ammonite.ps1 libs
  Compile libDaisy and DaisySP (once after cloning, and after updating them).

.EXAMPLE
  .\ammonite.ps1 build
  Compile the firmware into firmware\build\ammonite.bin

.EXAMPLE
  .\ammonite.ps1 flash
  Compile and flash over USB. Put the Seed in bootloader mode first:
  hold BOOT, tap RESET, release BOOT.

.EXAMPLE
  .\ammonite.ps1 sim
  Build synthcore.dll and start the PC simulator (same as simulator.bat).

.EXAMPLE
  .\ammonite.ps1 test
  Build synthcore.dll and run every headless test.

.EXAMPLE
  .\ammonite.ps1 manual
  Re-capture the screen images and rebuild manual\Ammonite_Manual.pdf.
#>
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('libs', 'build', 'flash', 'clean', 'sim', 'test', 'manual')]
    [string]$Action
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$py = if ($env:AMMONITE_PYTHON) { $env:AMMONITE_PYTHON } else { 'python' }

function Get-Bash {
    $bash = 'C:\Program Files\Git\bin\bash.exe'
    if (-not (Test-Path $bash)) {
        throw "Git Bash not found at $bash. Install Git for Windows from https://gitforwindows.org/"
    }
    return $bash
}

function Convert-ToPosixPath([string]$WinPath) {
    $full = (Resolve-Path $WinPath).Path
    $drive = $full.Substring(0, 1).ToLower()
    return '/' + $drive + $full.Substring(2).Replace('\', '/')
}

function Invoke-Make([string]$WorkDir, [string]$MakeArgs) {
    $bash = Get-Bash
    $posix = Convert-ToPosixPath $WorkDir
    Write-Host "==> make $MakeArgs   (in $WorkDir)" -ForegroundColor Cyan
    & $bash -lc "cd '$posix' && make $MakeArgs"
    if ($LASTEXITCODE -ne 0) { throw "make $MakeArgs failed with exit code $LASTEXITCODE" }
}

function Build-Dll {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'sim\build_dll.ps1')
    if ($LASTEXITCODE -ne 0) { throw 'synthcore.dll build failed' }
}

$fw = Join-Path $root 'firmware'

switch ($Action) {
    'libs' {
        Invoke-Make (Join-Path $root 'lib\libDaisy') ''
        Invoke-Make (Join-Path $root 'lib\DaisySP') ''
        Write-Host 'Libraries built.' -ForegroundColor Green
    }
    'clean' { Invoke-Make $fw 'clean' }
    'build' { Invoke-Make $fw 'clean'; Invoke-Make $fw '' }
    'flash' {
        Invoke-Make $fw 'clean'
        Invoke-Make $fw ''
        Write-Host "Flashing. If this hangs on 'No DFU capable USB device available'," -ForegroundColor Yellow
        Write-Host 'the Seed is not in bootloader mode: hold BOOT, tap RESET, release BOOT.' -ForegroundColor Yellow
        $bash = Get-Bash
        $posix = Convert-ToPosixPath $fw
        & $bash -lc "cd '$posix' && make program-dfu 2>&1"
        # dfu-util returns a non-zero exit code after a successful flash because
        # the board reboots before it can check status. If the output contains
        # "File downloaded successfully" the flash worked.
    }
    'sim' {
        Build-Dll
        & $py (Join-Path $root 'sim\panel_sim.py')
    }
    'test' {
        Build-Dll
        $failed = @()
        foreach ($t in Get-ChildItem (Join-Path $root 'tests') -Filter 'test_*.py') {
            Write-Host "==> $($t.Name)" -ForegroundColor Cyan
            & $py $t.FullName
            if ($LASTEXITCODE -ne 0) { $failed += $t.Name }
        }
        if ($failed) { throw "failed: $($failed -join ', ')" }
        Write-Host 'All tests passed.' -ForegroundColor Green
    }
    'manual' {
        & $py (Join-Path $root 'manual\build.py')
        if ($LASTEXITCODE -ne 0) { throw 'manual build failed' }
    }
}

Write-Host 'Done.' -ForegroundColor Green

<#
.SYNOPSIS
  Build, install and test the Ammonite Desktop phase 1 test plugins
  (Ammonite Test Web / Ammonite Test GL: VST3 + CLAP, Windows x64).

.DESCRIPTION
  Everything here stays inside desktop\. Python: $env:AMMONITE_PYTHON if set
  (a python.exe with numpy and Pillow), else "python".

.EXAMPLE
  .\desktop\desktop.ps1 build
  MSVC + CMake + Ninja from Visual Studio Build Tools 2022 ->
  desktop\build\cmake\bin\AmmoniteTestWeb.vst3, AmmoniteTestWeb.clap,
  AmmoniteTestGL.vst3, AmmoniteTestGL.clap.

.EXAMPLE
  .\desktop\desktop.ps1 install
  Copy them into C:\Program Files\Common Files\VST3 and ...\CLAP. Needs a
  PowerShell opened with "Run as administrator". Then rescan in the DAW.

.EXAMPLE
  .\desktop\desktop.ps1 test
  The headless checks: test_plugin.py (a CLAP host plays both variants,
  automation, two instances, state, transport) and ui_check.py (opens each
  UI in a window, screenshots it, drags the knob with the mouse). Keep your
  hands off the mouse while the UI windows are up.

.EXAMPLE
  .\desktop\desktop.ps1 test -Only plugin     # or -Only ui, -Only host

.EXAMPLE
  .\desktop\desktop.ps1 run
  Builds first (seconds when nothing changed: what runs is always the
  source as it is, the page bundled in included; -NoBuild skips it), then
  Ammonite Desktop in our own standalone host (no DAW): plays through the
  default output, Space starts and stops the transport, + / - set the tempo.
  .\desktop\desktop.ps1 run -Variant web            # the phase 1 test plugins
  .\desktop\desktop.ps1 run -Variant gl -Instances 4 -Tempo 100
#>
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('build', 'install', 'test', 'run')]
    [string]$Action,
    [ValidateSet('all', 'plugin', 'ui', 'host', 'engine')]
    [string]$Only = 'all',
    [ValidateSet('ammonite', 'web', 'gl')]
    [string]$Variant = 'ammonite',
    [int]$Instances = 1,
    [double]$Tempo = 120,
    [string]$Out = '',
    [switch]$NoBuild
)

$ErrorActionPreference = 'Stop'
$py    = if ($env:AMMONITE_PYTHON) { $env:AMMONITE_PYTHON } else { 'python' }
$build = Join-Path $PSScriptRoot 'build'
$bin   = Join-Path $build 'cmake\bin'
$plugins = @('AmmoniteTestWeb', 'AmmoniteTestGL')

# MSVC + CMake + Ninja from Visual Studio Build Tools 2022. vcvars64 sets up
# the environment inside a batch file (one command per line: cmd expands
# %PATH% when it reads a line, so it cannot be chained after vcvars).
function Invoke-MsvcBuild {
    $vs = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools'
    $cm = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake"
    if (-not (Test-Path "$vs\VC\Auxiliary\Build\vcvars64.bat")) {
        throw "Visual Studio Build Tools 2022 not found at $vs"
    }
    $bat = Join-Path $build 'build_desktop.bat'
    $src = $PSScriptRoot
    $out = Join-Path $build 'cmake'
    @(
        '@echo off',
        "call `"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul || exit /b 1",
        "set `"PATH=$cm\CMake\bin;$cm\Ninja;%PATH%`"",
        # cl explicitly: another g++ on PATH (the Daisy ARM toolchain) would win
        "cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl -S `"$src`" -B `"$out`" || exit /b 1",
        "cmake --build `"$out`" || exit /b 1"
    ) | Set-Content -Encoding ascii $bat
    & cmd /c $bat | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'desktop build failed' }
}

New-Item -ItemType Directory -Force $build | Out-Null

switch ($Action) {
    'build' {
        Invoke-MsvcBuild
        Write-Host ''
        Get-ChildItem $bin | ForEach-Object { Write-Host "built $($_.FullName)" -ForegroundColor Green }
    }
    'install' {
        # The standard system folders every host scans. Writing there needs
        # an administrator PowerShell.
        if (-not (Test-Path (Join-Path $bin 'AmmoniteTestWeb.clap'))) { throw 'not built yet: .\desktop\desktop.ps1 build' }
        $admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
            [Security.Principal.WindowsBuiltInRole]::Administrator)
        if (-not $admin) { throw 'run this in a PowerShell opened with "Run as administrator"' }
        $vst3 = 'C:\Program Files\Common Files\VST3'
        $clap = 'C:\Program Files\Common Files\CLAP'
        New-Item -ItemType Directory -Force $vst3, $clap | Out-Null
        foreach ($name in $plugins) {
            Remove-Item -Recurse -Force (Join-Path $vst3 "$name.vst3") -ErrorAction SilentlyContinue
            Copy-Item -Recurse (Join-Path $bin "$name.vst3") $vst3
            Copy-Item -Force (Join-Path $bin "$name.clap") $clap
            Write-Host "installed $vst3\$name.vst3" -ForegroundColor Green
            Write-Host "installed $clap\$name.clap" -ForegroundColor Green
        }
    }
    'test' {
        $failed = @()
        if ($Only -in @('all', 'engine')) {
            # the engine, headless: ammonite_engine.dll through its C API
            foreach ($t in Get-ChildItem (Join-Path $PSScriptRoot 'test\tests\engine') -Filter 'test_*.py') {
                Write-Host "==> engine\$($t.Name)" -ForegroundColor Cyan
                & $py $t.FullName
                if ($LASTEXITCODE -ne 0) { $failed += "engine\$($t.Name)" }
            }
        }
        $tests = @()
        if ($Only -in @('all', 'plugin')) { $tests += @('test_plugin.py') }
        if ($Only -in @('all', 'ui'))     { $tests += @('ui_check.py') }
        if ($Only -in @('all', 'host'))   { $tests += @('host_check.py') }
        foreach ($t in $tests) {
            $variants = if ($t -eq 'host_check.py') { @('ammonite', 'web', 'gl') } else { @('web', 'gl') }
            foreach ($v in $variants) {
                Write-Host "==> $t $v" -ForegroundColor Cyan
                & $py (Join-Path $PSScriptRoot "test\tests\$t") $v
                if ($LASTEXITCODE -ne 0) { $failed += "$t $v" }
            }
        }
        if ($failed) { throw "failed: $($failed -join ', ')" }
        Write-Host 'All desktop test plugin checks passed.' -ForegroundColor Green
    }
    'run' {
        # The test plugin without a DAW: our own host plays it through the
        # default output and runs a transport. Space: play / stop, + / -: tempo.
        # build first: a stale binary (the page is bundled into it at build time) once ran in place of the source
        if (-not $NoBuild) { Invoke-MsvcBuild }
        $exe  = Join-Path $bin 'AmmoniteTestHost.exe'
        $clap = Join-Path $bin $(switch ($Variant) { 'web' { 'AmmoniteTestWeb.clap' } 'gl' { 'AmmoniteTestGL.clap' } default { 'AmmoniteDesktop.clap' } })
        if (-not (Test-Path $exe) -or -not (Test-Path $clap)) { throw 'not built yet: .\desktop\desktop.ps1 build' }
        Write-Host "running $clap in $exe ($Instances instance(s), $Tempo BPM)" -ForegroundColor Cyan
        $outArgs = if ($Out) { @('-out', $Out) } else { @() } # an output device by part of its name; else the one chosen last
        & $exe $clap -n $Instances -t $Tempo @outArgs
    }
}

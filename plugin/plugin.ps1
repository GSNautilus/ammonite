<#
.SYNOPSIS
  Build, install and test the Ammonite plugin (VST3 + CLAP, Windows x64).

.DESCRIPTION
  Everything here stays inside plugin\: the hardware project (core\,
  firmware\, sim\, tests\, ammonite.ps1) is never modified. Python:
  $env:AMMONITE_PYTHON if set (a python.exe with numpy and ziglang), else
  "python".

.EXAMPLE
  .\plugin\plugin.ps1 build
  Build plugin\build\cmake\bin\Ammonite.vst3 and Ammonite.clap with MSVC,
  CMake and Ninja from Visual Studio Build Tools 2022.

.EXAMPLE
  .\plugin\plugin.ps1 install
  Copy them into C:\Program Files\Common Files\VST3 and ...\CLAP. Needs a
  PowerShell opened with "Run as administrator".

.EXAMPLE
  .\plugin\plugin.ps1 test
  Build two DLLs with the same compiler and flags as the simulator:
  plugin\build\core_ref.dll from core\ (the hardware engine) and
  plugin\build\ammonite_engine.dll from plugin\engine. Then run the whole
  hardware test suite (tests\) against the plugin engine, and
  plugin\tests\ (bit-identical to core\, several instances side by side,
  sample rates, and the built Ammonite.clap in a small CLAP host).
#>
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('build', 'install', 'test')]
    [string]$Action
)

$ErrorActionPreference = 'Stop'
$root  = Split-Path $PSScriptRoot -Parent
$py    = if ($env:AMMONITE_PYTHON) { $env:AMMONITE_PYTHON } else { 'python' }
$dsp   = Join-Path $root 'lib\DaisySP\Source'
$lgpl  = Join-Path $root 'lib\DaisySP\DaisySP-LGPL\Source'
$build = Join-Path $PSScriptRoot 'build'

# One zig build of an engine DLL, same flags as sim\build_dll.ps1 (core\
# takes ReverbSc from DaisySP-LGPL, plugin\engine has its own copy). Output
# is shown live and returned so a stale zig cache entry can be detected.
function Invoke-Zig([string]$srcDir, [string[]]$files, [string]$out, [string[]]$extra) {
    $sources = @($files | ForEach-Object {
            if ([IO.Path]::IsPathRooted($_)) { $_ } else { Join-Path $srcDir $_ } }) + @(
        (Join-Path $dsp  'Synthesis\oscillator.cpp'),
        (Join-Path $dsp  'Filters\svf.cpp'),
        (Join-Path $dsp  'Control\phasor.cpp'))
    $log = New-Object System.Collections.Generic.List[string]
    & $py -m ziglang c++ -shared -O2 -std=c++14 -w `
        -I $srcDir -I $dsp -I "$dsp\Synthesis" -I "$dsp\Filters" -I "$dsp\Utility" `
        -I "$dsp\Control" -I $lgpl `
        @extra @sources -o $out 2>&1 | ForEach-Object {
            $line = "$_"
            $log.Add($line)
            Write-Host $line
        }
    return @{ Code = $LASTEXITCODE; Log = ($log -join "`n") }
}

function Build-EngineDll([string]$srcDir, [string[]]$files, [string]$out, [string[]]$flags) {
    Write-Host "==> $out" -ForegroundColor Cyan
    $r = Invoke-Zig $srcDir $files $out $flags
    # A zig process killed mid-build leaves a manifest without its object
    # file; a one-off define changes the cache key (see sim\build_dll.ps1).
    if ($r.Code -ne 0 -and $r.Log -match "could not open '.*\\zig\\o\\") {
        Write-Host 'zig cache entry is missing its object file; rebuilding with a fresh cache key...' -ForegroundColor Yellow
        $r = Invoke-Zig $srcDir $files $out (@($flags) + @('-DSYNTH_CACHE_NONCE=' + [DateTime]::Now.Ticks))
    }
    if ($r.Code -ne 0) { throw "build of $out failed" }
}

# MSVC + CMake + Ninja from Visual Studio Build Tools 2022. vcvars64 sets up
# the environment inside a batch file (one command per line: cmd expands
# %PATH% when it reads a line, so it cannot be chained after vcvars).
function Invoke-MsvcBuild {
    $vs = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools'
    $cm = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake"
    if (-not (Test-Path "$vs\VC\Auxiliary\Build\vcvars64.bat")) {
        throw "Visual Studio Build Tools 2022 not found at $vs"
    }
    $bat = Join-Path $build 'build_plugin.bat'
    $src = $PSScriptRoot
    $bin = Join-Path $build 'cmake'
    @(
        '@echo off',
        "call `"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul || exit /b 1",
        "set `"PATH=$cm\CMake\bin;$cm\Ninja;%PATH%`"",
        # cl explicitly: another g++ on PATH (the Daisy ARM toolchain) would win
        "cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl -S `"$src`" -B `"$bin`" || exit /b 1",
        "cmake --build `"$bin`" || exit /b 1"
    ) | Set-Content -Encoding ascii $bat
    & cmd /c $bat | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'plugin build failed' }
    return Join-Path $bin 'bin'
}

New-Item -ItemType Directory -Force $build | Out-Null

switch ($Action) {
    'build' {
        $out = Invoke-MsvcBuild
        Write-Host ''
        Get-ChildItem $out | ForEach-Object { Write-Host "built $($_.FullName)" -ForegroundColor Green }
    }
    'install' {
        # The standard system folders every host scans. Writing there needs
        # an administrator PowerShell.
        $out = Join-Path $build 'cmake\bin'
        if (-not (Test-Path (Join-Path $out 'Ammonite.vst3'))) { throw 'not built yet: .\plugin\plugin.ps1 build' }
        $admin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
            [Security.Principal.WindowsBuiltInRole]::Administrator)
        if (-not $admin) { throw 'run this in a PowerShell opened with "Run as administrator"' }
        $vst3 = 'C:\Program Files\Common Files\VST3'
        $clap = 'C:\Program Files\Common Files\CLAP'
        New-Item -ItemType Directory -Force $vst3, $clap | Out-Null
        Remove-Item -Recurse -Force (Join-Path $vst3 'Ammonite.vst3') -ErrorAction SilentlyContinue
        Copy-Item -Recurse (Join-Path $out 'Ammonite.vst3') $vst3
        Copy-Item -Force (Join-Path $out 'Ammonite.clap') $clap
        Write-Host "installed $vst3\Ammonite.vst3" -ForegroundColor Green
        Write-Host "installed $clap\Ammonite.clap" -ForegroundColor Green
    }
    'test' {
        $ref = Join-Path $build 'core_ref.dll'
        $eng = Join-Path $build 'ammonite_engine.dll'
        Build-EngineDll (Join-Path $root 'core') @('synth_core.cpp', 'api.cpp',
            (Join-Path $lgpl 'Effects\reverbsc.cpp')) $ref @('-DUSE_DAISYSP_LGPL')
        Build-EngineDll (Join-Path $PSScriptRoot 'engine') @('engine.cpp', 'api.cpp',
            'reverbsc.cpp') $eng @()

        $failed = @()
        $env:SYNTH_DLL = $eng # the hardware suite, run against the plugin engine
        $tests = @(Get-ChildItem (Join-Path $root 'tests') -Filter 'test_*.py') +
                 @(Get-ChildItem (Join-Path $PSScriptRoot 'tests') -Filter 'test_*.py')
        foreach ($t in $tests) {
            Write-Host "==> $($t.Directory.Name)\$($t.Name)" -ForegroundColor Cyan
            & $py $t.FullName
            if ($LASTEXITCODE -ne 0) { $failed += $t.Name }
        }
        $env:SYNTH_DLL = $null
        if ($failed) { throw "failed: $($failed -join ', ')" }
        Write-Host 'All plugin engine tests passed.' -ForegroundColor Green
    }
}

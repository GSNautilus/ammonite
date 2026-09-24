<#
.SYNOPSIS
  Build and test the Ammonite plugin's engine (plugin\engine).

.DESCRIPTION
  Everything here stays inside plugin\: the hardware project (core\,
  firmware\, sim\, tests\, ammonite.ps1) is never modified. Python:
  $env:AMMONITE_PYTHON if set (a python.exe with numpy and ziglang), else
  "python".

.EXAMPLE
  .\plugin\plugin.ps1 test
  Build two DLLs with the same compiler and flags as the simulator:
  plugin\build\core_ref.dll from core\ (the hardware engine) and
  plugin\build\ammonite_engine.dll from plugin\engine. Then run the whole
  hardware test suite (tests\) against the plugin engine, and
  plugin\tests\ (bit-identical to core\, several instances side by side).
#>
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('test')]
    [string]$Action
)

$ErrorActionPreference = 'Stop'
$root  = Split-Path $PSScriptRoot -Parent
$py    = if ($env:AMMONITE_PYTHON) { $env:AMMONITE_PYTHON } else { 'python' }
$dsp   = Join-Path $root 'lib\DaisySP\Source'
$lgpl  = Join-Path $root 'lib\DaisySP\DaisySP-LGPL\Source'
$build = Join-Path $PSScriptRoot 'build'

# One zig build of an engine DLL, same flags as sim\build_dll.ps1. Output is
# shown live and returned so a stale zig cache entry can be detected.
function Invoke-Zig([string]$srcDir, [string[]]$files, [string]$out, [string[]]$extra) {
    $sources = @($files | ForEach-Object { Join-Path $srcDir $_ }) + @(
        (Join-Path $dsp  'Synthesis\oscillator.cpp'),
        (Join-Path $dsp  'Filters\svf.cpp'),
        (Join-Path $lgpl 'Effects\reverbsc.cpp'),
        (Join-Path $dsp  'Control\phasor.cpp'))
    $log = New-Object System.Collections.Generic.List[string]
    & $py -m ziglang c++ -shared -O2 -std=c++14 -w -DUSE_DAISYSP_LGPL `
        -I $srcDir -I $dsp -I "$dsp\Synthesis" -I "$dsp\Filters" -I "$dsp\Utility" `
        -I "$dsp\Control" -I $lgpl `
        @extra @sources -o $out 2>&1 | ForEach-Object {
            $line = "$_"
            $log.Add($line)
            Write-Host $line
        }
    return @{ Code = $LASTEXITCODE; Log = ($log -join "`n") }
}

function Build-EngineDll([string]$srcDir, [string[]]$files, [string]$out) {
    Write-Host "==> $out" -ForegroundColor Cyan
    $r = Invoke-Zig $srcDir $files $out @()
    # A zig process killed mid-build leaves a manifest without its object
    # file; a one-off define changes the cache key (see sim\build_dll.ps1).
    if ($r.Code -ne 0 -and $r.Log -match "could not open '.*\\zig\\o\\") {
        Write-Host 'zig cache entry is missing its object file; rebuilding with a fresh cache key...' -ForegroundColor Yellow
        $r = Invoke-Zig $srcDir $files $out @('-DSYNTH_CACHE_NONCE=' + [DateTime]::Now.Ticks)
    }
    if ($r.Code -ne 0) { throw "build of $out failed" }
}

New-Item -ItemType Directory -Force $build | Out-Null

switch ($Action) {
    'test' {
        $ref = Join-Path $build 'core_ref.dll'
        $eng = Join-Path $build 'ammonite_engine.dll'
        Build-EngineDll (Join-Path $root 'core') @('synth_core.cpp', 'api.cpp') $ref
        Build-EngineDll (Join-Path $PSScriptRoot 'engine') @('engine.cpp', 'api.cpp') $eng

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

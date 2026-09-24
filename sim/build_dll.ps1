# Rebuild synthcore.dll after editing anything in core\.
# Add more DaisySP .cpp files to $sources as the engine grows.
# Python: $env:AMMONITE_PYTHON if set (a python.exe with the ziglang package),
# else whatever "python" is on PATH.
$py     = if ($env:AMMONITE_PYTHON) { $env:AMMONITE_PYTHON } else { "python" }
$dsp    = Join-Path $PSScriptRoot "..\lib\DaisySP\Source"
$lgpl   = Join-Path $PSScriptRoot "..\lib\DaisySP\DaisySP-LGPL\Source"
$core   = Join-Path $PSScriptRoot "..\core"
$out    = Join-Path $PSScriptRoot "synthcore.dll"

$sources = @(
    (Join-Path $core "synth_core.cpp"),
    (Join-Path $core "api.cpp"),
    (Join-Path $dsp  "Synthesis\oscillator.cpp"),
    (Join-Path $dsp  "Filters\svf.cpp"),
    (Join-Path $lgpl "Effects\reverbsc.cpp"),
    (Join-Path $dsp  "Control\phasor.cpp")
)

# One zig build. Output is shown live and also returned so it can be checked.
# Extra arguments (e.g. a cache-busting define) are appended.
function Invoke-Build([string[]]$extra) {
    $log = New-Object System.Collections.Generic.List[string]
    & $py -m ziglang c++ -shared -O2 -std=c++14 -w -DUSE_DAISYSP_LGPL `
        -I $core -I $dsp -I "$dsp\Synthesis" -I "$dsp\Filters" -I "$dsp\Utility" `
        -I "$dsp\Control" -I $lgpl `
        @extra @sources -o $out 2>&1 | ForEach-Object {
            $line = "$_"
            $log.Add($line)
            Write-Host $line
        }
    return @{ Code = $LASTEXITCODE; Log = ($log -join "`n") }
}

$r = Invoke-Build @()

# Zig's cache can hold a manifest whose object file is gone (typically a zig
# process killed mid-build, e.g. the console closed during the long first
# build). Zig then trusts the manifest and the link fails with "could not
# open ...\zig\o\<hash>\*.obj". A one-off define changes the cache key, so
# our sources recompile (seconds); the cached libc++ runtime is untouched.
if ($r.Code -ne 0 -and $r.Log -match "could not open '.*\\zig\\o\\") {
    Write-Host ""
    Write-Host "zig cache entry is missing its object file; rebuilding with a fresh cache key..." -ForegroundColor Yellow
    $nonce = "-DSYNTH_CACHE_NONCE=" + [DateTime]::Now.Ticks
    $r = Invoke-Build @($nonce)
}

if ($r.Code -eq 0) { Write-Host "built $out" -ForegroundColor Green }
else { Write-Host "build failed" -ForegroundColor Red }
exit $r.Code   # so synth.bat can tell

param(
    [Parameter(Mandatory = $true)]
    [string]$Rom,
    [int]$Frames = 300,
    [ValidateSet(0, 1)]
    [int]$Regions = 1,
    [switch]$Hash,
    [ValidateSet('Ref', 'Mcu', 'McuCache')]
    [string]$Only
)

$ErrorActionPreference = 'Stop'
$xmake = 'D:\Program Files\xmake\xmake.exe'
Set-Location $PSScriptRoot

& $xmake f -m release -y | Out-Null
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
foreach ($target in @('nes-bench-ref', 'nes-bench-mcu', 'nes-bench-mcu-cache')) {
    & $xmake build $target | Out-Null
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

function Invoke-Benchmark([string]$Target) {
    $arguments = @($Rom, '--frames', "$Frames", '--regions', "$Regions")
    if ($Hash) { $arguments += @('--hash', '1') }
    & (Join-Path $PSScriptRoot "out/bin/$Target.exe") @arguments
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

if ($Only -eq 'McuCache') {
    Invoke-Benchmark 'nes-bench-mcu-cache'
} elseif ($Only -eq 'Ref') {
    Invoke-Benchmark 'nes-bench-ref'
} elseif ($Only -eq 'Mcu') {
    Invoke-Benchmark 'nes-bench-mcu'
} else {
    Invoke-Benchmark 'nes-bench-ref'
    Invoke-Benchmark 'nes-bench-mcu'
}

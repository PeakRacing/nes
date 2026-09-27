param(
    [ValidateSet("all", "cpu", "ppu", "apu", "rom", "mapper", "stress", "corpus")][string]$Filter = "all",
    [string]$Report = "out/report.csv",
    [switch]$Stream,            # build/run the NES_ROM_STREAM=1 target
    [switch]$FrameSkip,         # build/run the NES_FRAME_SKIP=1 target
    [switch]$Isolated,          # corpus only: one process per ROM, so a crash costs one image
    [int]$Frames = 180,         # corpus: frames per image
    [int]$Mapper = -1,          # corpus: only images whose folder is mapperN
    [int]$MaxRoms = 0,          # corpus: stop after N images
    [string]$RomDir = "",       # corpus: override the default <root>/rom
    [switch]$UpdateBaseline,    # corpus: rewrite test/baseline/corpus.csv
    [switch]$Strict             # corpus: frame-hash mismatch is a failure
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Push-Location $root
try {
    $target = if ($Stream) { "nes-tests-stream" } else { "nes-tests" }
    if ($FrameSkip) { $target = "nes-tests-frameskip" }
    $exe = "out/bin/$target.exe"
    xmake f -m debug -y | Out-Null
    xmake build $target
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

    $extra = @()
    if ($RomDir) { $extra += @("--rom-dir", $RomDir) }
    if ($Frames -gt 0) { $extra += @("--frames", $Frames) }
    if ($Mapper -ge 0) { $extra += @("--mapper", $Mapper) }
    if ($MaxRoms -gt 0) { $extra += @("--max-roms", $MaxRoms) }
    if ($UpdateBaseline) { $extra += "--update-baseline" }
    if ($Strict) { $extra += "--strict" }

    if (-not ($Isolated -and $Filter -eq "corpus")) {
        & $exe --filter $Filter --report $Report @extra
        exit $LASTEXITCODE
    }

    # Isolated corpus run: a crash (or a bad mapper) only loses one image.
    $romsRoot = if ($RomDir) { $RomDir } else { Join-Path (Split-Path -Parent $root) "rom" }
    $roms = @(Get-ChildItem -LiteralPath $romsRoot -Recurse -File -ErrorAction SilentlyContinue |
              Where-Object { $_.Extension -match '^\.nes$' } |
              Where-Object { $Mapper -lt 0 -or $_.Directory.Name -eq "mapper$Mapper" } |
              Sort-Object FullName)
    if ($MaxRoms -gt 0) { $roms = @($roms | Select-Object -First $MaxRoms) }
    if ($roms.Count -eq 0) {
        Write-Host "no .nes images under $romsRoot"
        exit 0
    }
    $results = "out/corpus.csv"
    "status,verdict,sha256,label,dir_mapper,header_mapper,effective_mapper,frames,hash_chain,first_render" |
        Set-Content -Path $results
    $failed = 0
    foreach ($rom in $roms) {
        $line = & $exe --rom $rom.FullName @extra
        $code = $LASTEXITCODE
        if ($line) { $line | Add-Content -Path $results }
        if ($code -ne 0) { $failed++; Write-Host "REGRESSED: $($rom.Name)" }
    }
    Write-Host "isolated run: $($roms.Count) image(s), $failed failure(s)"
    & $exe --analyze $results @extra
    if ($LASTEXITCODE -ne 0 -or $failed -ne 0) { exit 1 }
    exit 0
} finally { Pop-Location }

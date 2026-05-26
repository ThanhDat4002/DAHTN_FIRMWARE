param(
    [string] $BuildDir = "build_fresh"
)

$rulesFile = Join-Path $BuildDir "CMakeFiles/rules.ninja"
if (-not (Test-Path -LiteralPath $rulesFile)) {
    throw "rules.ninja not found: $rulesFile"
}

$oldAr = 'C:\Users\Admin\.espressif\tools\xtensa-esp-elf\esp-14.2.0_20251107\xtensa-esp-elf\bin\xtensa-esp32-elf-ar.exe'
$newAr = (Resolve-Path 'tools\evion-ar-wrapper.cmd').Path
$content = Get-Content -LiteralPath $rulesFile -Raw
$updated = $content.Replace($oldAr, $newAr)

# Some stale Windows build dirs end up with `$in_newline in linker rsp rules.
# That breaks linker search paths in the old `build` cache, while fresh builds use `$in`.
$updated = $updated.Replace('rspfile_content = $in_newline $LINK_PATH $LINK_LIBRARIES',
    'rspfile_content = $in $LINK_PATH $LINK_LIBRARIES')

if ($updated -ne $content) {
    Set-Content -LiteralPath $rulesFile -Value $updated
    Write-Host "Patched $rulesFile"
} else {
    Write-Host "Archiver already patched in $rulesFile"
}

$objdumpPath = 'C:/Users/Admin/.espressif/tools/xtensa-esp-elf/esp-14.2.0_20251107/xtensa-esp-elf/bin/xtensa-esp32-elf-objdump.exe'
Get-ChildItem -LiteralPath (Join-Path $BuildDir 'CMakeFiles') -Filter 'sections.ld-*.bat' -File -ErrorAction SilentlyContinue | ForEach-Object {
    $batch = Get-Content -LiteralPath $_.FullName -Raw
    $fixedBatch = $batch -replace [regex]::Escape('--objdump ""'), "--objdump $objdumpPath"
    if ($fixedBatch -ne $batch) {
        Set-Content -LiteralPath $_.FullName -Value $fixedBatch
        Write-Host "Patched $($_.FullName)"
    }
}

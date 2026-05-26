param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $ToolArgs
)

$rspIndex = -1
for ($i = 0; $i -lt $ToolArgs.Length; $i++) {
    if ($ToolArgs[$i].StartsWith("@")) {
        $rspIndex = $i
        break
    }
}

if ($rspIndex -lt 0) {
    & xtensa-esp32-elf-ar.exe @ToolArgs
    exit $LASTEXITCODE
}

$rspPath = $ToolArgs[$rspIndex].Substring(1)
$tmpRsp = Join-Path $env:TEMP ("evion-ar-{0}-{1}.rsp" -f $PID, [guid]::NewGuid().ToString("N"))

try {
    (Get-Content -LiteralPath $rspPath) -replace "\\", "/" |
        Set-Content -LiteralPath $tmpRsp

    $patchedArgs = [System.Collections.Generic.List[string]]::new()
    for ($i = 0; $i -lt $ToolArgs.Length; $i++) {
        if ($i -eq $rspIndex) {
            $patchedArgs.Add("@$tmpRsp")
        } else {
            $patchedArgs.Add($ToolArgs[$i])
        }
    }

    & xtensa-esp32-elf-ar.exe @patchedArgs
    exit $LASTEXITCODE
} finally {
    Remove-Item -LiteralPath $tmpRsp -ErrorAction SilentlyContinue
}

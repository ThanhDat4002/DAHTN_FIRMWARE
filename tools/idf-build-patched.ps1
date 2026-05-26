param(
    [string] $BuildDir = "build_fresh"
)

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Push-Location $repoRoot
try {
    $pythonExe = $null
    if ($env:IDF_PYTHON_ENV_PATH) {
        $candidate = Join-Path $env:IDF_PYTHON_ENV_PATH "Scripts\python.exe"
        if (Test-Path -LiteralPath $candidate) {
            $pythonExe = $candidate
        }
    }
    if (-not $pythonExe) {
        $python = Get-Command python -ErrorAction SilentlyContinue
        if ($python) {
            $pythonExe = $python.Source
        }
    }
    if (-not $pythonExe) {
        throw "Python for ESP-IDF not found. Open an ESP-IDF shell or VS Code ESP-IDF terminal first."
    }

    $idfPy = $null
    if ($env:IDF_PATH) {
        $candidate = Join-Path $env:IDF_PATH "tools\idf.py"
        if (Test-Path -LiteralPath $candidate) {
            $idfPy = $candidate
        }
    }
    if (-not $idfPy) {
        $idf = Get-Command idf.py -ErrorAction SilentlyContinue
        if ($idf) {
            $idfPy = $idf.Source
        }
    }
    if (-not $idfPy) {
        throw "idf.py not found. Set IDF_PATH or open an ESP-IDF shell first."
    }

    $ninja = Get-Command ninja -ErrorAction SilentlyContinue
    if (-not $ninja) {
        throw "ninja not found in PATH. Open an ESP-IDF shell or VS Code ESP-IDF terminal first."
    }

    & $pythonExe $idfPy -B $BuildDir reconfigure
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "patch-ar-rules.ps1") -BuildDir $BuildDir
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    & $ninja.Source -C $BuildDir
    exit $LASTEXITCODE
} finally {
    Pop-Location
}

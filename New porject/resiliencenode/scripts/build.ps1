<#
.SYNOPSIS
    Compiles the ResilienceNode ESP32 firmware using ESP-IDF.

.DESCRIPTION
    Automates environment verification, target configuration, clean builds,
    and compilation of the ResilienceNode embedded firmware.

.PARAMETER Clean
    If specified, performs a full clean (idf.py fullclean) before compiling.

.PARAMETER Target
    ESP32 target chip family (default: 'esp32', supports 'esp32s3', 'esp32c3', 'esp32c5').

.PARAMETER Config
    Optional sdkconfig configuration preset.

.EXAMPLE
    .\scripts\build.ps1
    .\scripts\build.ps1 -Clean -Target esp32
#>

[CmdletBinding()]
param(
    [switch]$Clean,
    [string]$Target = "esp32",
    [string]$Config = ""
)

$ErrorActionPreference = "Stop"
$sw = [System.Diagnostics.Stopwatch]::StartNew()

function Write-Step {
    param([string]$Message)
    Write-Host "`n[STEP] " -ForegroundColor Cyan -NoNewline
    Write-Host $Message -ForegroundColor White
}

function Write-Success {
    param([string]$Message)
    Write-Host "[SUCCESS] " -ForegroundColor Green -NoNewline
    Write-Host $Message -ForegroundColor White
}

function Write-Warn {
    param([string]$Message)
    Write-Host "[WARN] " -ForegroundColor Yellow -NoNewline
    Write-Host $Message -ForegroundColor White
}

function Write-Fail {
    param([string]$Message)
    Write-Host "[ERROR] " -ForegroundColor Red -NoNewline
    Write-Host $Message -ForegroundColor White
}

# Ensure script runs from project root
$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $ProjectRoot
Write-Host "==========================================================" -ForegroundColor Cyan
Write-Host "   ResilienceNode Firmware Build Automation Tool          " -ForegroundColor White
Write-Host "   Target: $Target | Project Root: $ProjectRoot           " -ForegroundColor DarkGray
Write-Host "==========================================================" -ForegroundColor Cyan

# 1. Verify ESP-IDF environment
Write-Step "Checking for ESP-IDF environment (idf.py)..."
$IdfCommand = Get-Command "idf.py" -ErrorAction SilentlyContinue

if (-not $IdfCommand) {
    if ($env:IDF_PATH -and (Test-Path "$env:IDF_PATH\export.ps1")) {
        Write-Warn "idf.py not found in current PATH. Sourcing `$env:IDF_PATH\export.ps1..."
        & "$env:IDF_PATH\export.ps1"
        $IdfCommand = Get-Command "idf.py" -ErrorAction SilentlyContinue
    }
}

if (-not $IdfCommand) {
    Write-Fail "ESP-IDF environment was not detected. Please run 'export.ps1' from your ESP-IDF installation directory before running this script."
    exit 1
}

$idfVersion = idf.py --version
Write-Success "Found ESP-IDF: $idfVersion"

# 2. Clean build if requested
if ($Clean) {
    Write-Step "Cleaning build artifacts..."
    idf.py fullclean
    Write-Success "Build directory wiped clean."
}

# 3. Configure Target if required
Write-Step "Validating target chip: $Target..."
if (-not (Test-Path "build\CMakeCache.txt")) {
    Write-Host "Setting target to $Target..." -ForegroundColor DarkGray
    idf.py set-target $Target
}

# 4. Execute Build
Write-Step "Compiling firmware binary..."
$buildExitCode = 0
try {
    idf.py build
} catch {
    $buildExitCode = 1
}

$sw.Stop()
$elapsed = [math]::Round($sw.Elapsed.TotalSeconds, 1)

if ($LASTEXITCODE -eq 0 -and $buildExitCode -eq 0) {
    Write-Host "`n----------------------------------------------------------" -ForegroundColor Green
    Write-Success "Build completed successfully in ${elapsed}s!"
    Write-Host "----------------------------------------------------------" -ForegroundColor Green
    
    $binPath = Join-Path $ProjectRoot "build\resiliencenode.bin"
    if (Test-Path $binPath) {
        $binSize = (Get-Item $binPath).Length / 1KB
        Write-Host "Binary artifact: build\resiliencenode.bin ($([math]::Round($binSize, 1)) KB)" -ForegroundColor Cyan
    }
    Write-Host "Ready to flash with: .\scripts\flash.ps1 -Port COMx" -ForegroundColor White
    exit 0
} else {
    Write-Host "`n----------------------------------------------------------" -ForegroundColor Red
    Write-Fail "Build failed after ${elapsed}s. Review the compiler errors above."
    Write-Host "----------------------------------------------------------" -ForegroundColor Red
    exit 1
}

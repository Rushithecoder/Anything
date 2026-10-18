<#
.SYNOPSIS
    Flashes ResilienceNode firmware to an attached ESP32 device.

.DESCRIPTION
    Detects connected COM ports, supports full chip erase, and flashes the
    compiled binary at high speed (460,800 or 921,600 baud).

.PARAMETER Port
    Serial COM port (e.g., 'COM3'). If omitted, the script attempts auto-detection.

.PARAMETER Baud
    Flashing baud rate (default: 460800, alternative: 921600 or 115200).

.PARAMETER EraseFlash
    If specified, performs a full flash erase before flashing.

.PARAMETER Monitor
    If specified, launches the serial monitor immediately after flashing.

.EXAMPLE
    .\scripts\flash.ps1 -Port COM3
    .\scripts\flash.ps1 -EraseFlash -Monitor
#>

[CmdletBinding()]
param(
    [string]$Port = "",
    [int]$Baud = 460800,
    [switch]$EraseFlash,
    [switch]$Monitor
)

$ErrorActionPreference = "Stop"

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
Write-Host "   ResilienceNode Firmware Flashing Utility               " -ForegroundColor White
Write-Host "==========================================================" -ForegroundColor Cyan

# 1. Check build artifacts
$appBin = Join-Path $ProjectRoot "build\resiliencenode.bin"
if (-not (Test-Path $appBin)) {
    Write-Warn "Binary 'build\resiliencenode.bin' was not found. Initiating build first..."
    & (Join-Path $PSScriptRoot "build.ps1")
    if (-not (Test-Path $appBin)) {
        Write-Fail "Build failed. Cannot proceed with flashing."
        exit 1
    }
}

# 2. Port Detection
if ([string]::IsNullOrWhiteSpace($Port)) {
    Write-Step "Detecting attached serial COM ports..."
    $availablePorts = [System.IO.Ports.SerialPort]::GetPortNames()

    if ($availablePorts.Count -eq 0) {
        Write-Fail "No active COM ports detected. Please plug in your ESP32 board via USB and ensure drivers (CP210x, CH340, or FTDI) are installed."
        exit 1
    } elseif ($availablePorts.Count -eq 1) {
        $Port = $availablePorts[0]
        Write-Success "Auto-detected single device on port: $Port"
    } else {
        Write-Warn "Multiple serial ports detected: $($availablePorts -join ', ')"
        $Port = $availablePorts[0]
        Write-Host "Defaulting to first detected port: $Port (override with -Port <COMx>)" -ForegroundColor DarkGray
    }
}

Write-Host "Target Port: $Port | Flashing Baud: $Baud" -ForegroundColor Cyan

# 3. Erase flash if requested
if ($EraseFlash) {
    Write-Step "Performing complete chip erase on $Port..."
    idf.py -p $Port erase-flash
    Write-Success "Flash completely erased."
}

# 4. Flash binary
Write-Step "Writing firmware to ESP32 on $Port at $Baud baud..."
$flashCmd = "idf.py -p $Port -b $Baud flash"
if ($Monitor) {
    $flashCmd += " monitor"
}

Invoke-Expression $flashCmd

if ($LASTEXITCODE -eq 0) {
    Write-Host "`n----------------------------------------------------------" -ForegroundColor Green
    Write-Success "Flashing completed successfully on $Port!"
    Write-Host "----------------------------------------------------------" -ForegroundColor Green
    if (-not $Monitor) {
        Write-Host "To monitor device serial logs, run: .\scripts\monitor.ps1 -Port $Port" -ForegroundColor Cyan
    }
    exit 0
} else {
    Write-Host "`n----------------------------------------------------------" -ForegroundColor Red
    Write-Fail "Flashing failed with exit code $LASTEXITCODE."
    Write-Host "Tip: Press and hold the BOOT button on the ESP32 while initiating the flash command." -ForegroundColor Yellow
    Write-Host "----------------------------------------------------------" -ForegroundColor Red
    exit 1
}

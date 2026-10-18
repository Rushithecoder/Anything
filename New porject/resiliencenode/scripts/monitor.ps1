<#
.SYNOPSIS
    Launches the ESP-IDF serial console monitor for ResilienceNode.

.DESCRIPTION
    Connects to the ESP32 UART interface at 115200 baud, displaying colorized
    log output, task telemetry, and real-time state machine diagnostics.

.PARAMETER Port
    Serial COM port (e.g., 'COM3'). If omitted, the script attempts auto-detection.

.PARAMETER Baud
    Serial communication baud rate (default: 115200).

.EXAMPLE
    .\scripts\monitor.ps1
    .\scripts\monitor.ps1 -Port COM3
#>

[CmdletBinding()]
param(
    [string]$Port = "",
    [int]$Baud = 115200
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
Write-Host "   ResilienceNode Serial Diagnostics Console Launcher     " -ForegroundColor White
Write-Host "==========================================================" -ForegroundColor Cyan

# 1. Port Detection
if ([string]::IsNullOrWhiteSpace($Port)) {
    Write-Step "Detecting attached serial COM ports..."
    $availablePorts = [System.IO.Ports.SerialPort]::GetPortNames()

    if ($availablePorts.Count -eq 0) {
        Write-Fail "No active COM ports detected. Please connect the ESP32 device via USB."
        exit 1
    } elseif ($availablePorts.Count -eq 1) {
        $Port = $availablePorts[0]
        Write-Success "Auto-detected single device on port: $Port"
    } else {
        Write-Warn "Multiple serial ports detected: $($availablePorts -join ', ')"
        $Port = $availablePorts[0]
        Write-Host "Connecting to first detected port: $Port (override with -Port <COMx>)" -ForegroundColor DarkGray
    }
}

Write-Host "`nConnecting to $Port at $Baud baud..." -ForegroundColor Green
Write-Host "----------------------------------------------------------" -ForegroundColor DarkGray
Write-Host "  Keyboard shortcuts for ESP-IDF Monitor:                 " -ForegroundColor Yellow
Write-Host "    • Exit Monitor:         Ctrl + ]                      " -ForegroundColor Yellow
Write-Host "    • Hard Reset Chip:      Ctrl + T followed by Ctrl + R " -ForegroundColor Yellow
Write-Host "    • Menu / Help:          Ctrl + T followed by Ctrl + H " -ForegroundColor Yellow
Write-Host "----------------------------------------------------------`n" -ForegroundColor DarkGray

# 2. Launch monitor
idf.py -p $Port monitor

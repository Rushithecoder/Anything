<div align="center">

# 🛡️ ResilienceNode

### Autonomous Edge Network Monitoring Black Box
**Always-On Local Telemetry, Incident Classification & Out-of-Band Diagnostics for Mission-Critical Wi-Fi**

[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.1%2B-blue.svg)](https://github.com/espressif/esp-idf)
[![Hardware](https://img.shields.io/badge/Target-ESP32--WROOM--32%20%7C%20ESP32--S3-red.svg)](https://www.espressif.com/en/products/socs/esp32)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![Status](https://img.shields.io/badge/Build-Passing-brightgreen.svg)]()
[![Web UI](https://img.shields.io/badge/Dashboard-Embedded%20Zero--Dep%20SPA-orange.svg)]()

</div>

---

## 📌 Executive Summary

Modern enterprise, industrial, smart home, and retail edge infrastructures rely unconditionally on continuous local Wi-Fi and WAN connectivity. When a router hangs, an ISP drops, or RF interference collapses a link, conventional cloud-based monitoring agents (Datadog, Pingdom, New Relic) fail simultaneously—leaving administrators with **zero visibility** ("the blind outage paradox").

**ResilienceNode** is an autonomous, ultra-low-power embedded hardware black box built on the Espressif ESP32 SoC. It operates continuously within your critical network zone, actively monitoring local gateway reachability, public DNS resolution, and target endpoint latencies. 

Operating in simultaneous **Station (STA) + Access Point (AP)** mode, ResilienceNode maintains an isolated, independent out-of-band Wi-Fi network (`ResilienceNode-XXXX`). During total upstream outages, engineers and on-site staff can connect directly to the node at `http://192.168.4.1` to inspect live telemetry, analyze root-cause incident classifications, and export persistent non-volatile event records.

---

## 🚀 Key Features

- **Concurrent Dual-Mode Wi-Fi Architecture (AP + STA):** 
  Maintains connection to the uplink WLAN while broadcasting an isolated management network (`ResilienceNode-XXXX`, WPA2-PSK) on `192.168.4.1`.
- **Active Multi-Target Health Engine:** 
  Concurrent probing across three topological layers: Local Gateway (ARP/ICMP), Core DNS (`8.8.8.8`, `1.1.1.1`), and Cloud Application Targets.
- **Deterministic Incident Classification State Machine:** 
  Classifies link behavior in real-time: `HEALTHY`, `DEGRADED` (jitter/loss), `OUTAGE` (gateway alive, WAN dead), `DISCONNECTED` (RF/AP lost), and `RECOVERING`.
- **Flash-Backed Black-Box Event Store:** 
  Power-fail-safe incident logging in ESP32 Non-Volatile Storage (NVS) using wear-leveling circular buffer semantics. Never loses historical failure logs during power cycles.
- **Embedded Zero-Dependency Web Dashboard:** 
  Single-page application (HTML5/CSS3/ES6) compressed directly into firmware ROM. Requires no internet connection, external CDNs, or runtime dependencies.
- **High-Fidelity Telemetry Metrics:** 
  Calculates instantaneous and rolling-window metrics (min/avg/max RTT, jitter variance, packet loss percentage, link flap counts, uptime, and RSSI).
- **Comprehensive RESTful JSON API:** 
  Allows seamless scraping, automation, and diagnostic export (`/api/status`, `/api/telemetry`, `/api/incidents`, `/api/config`, `/api/export`).
- **Autonomous Watchdog & Backoff Reconnect:** 
  Integrated ESP32 Task Watchdog Timer (TWDT) and exponential backoff link recovery to eliminate edge deadlock.
- **Visual Hardware & Serial Diagnostic Stream:** 
  Blink-code visual diagnostics via onboard LED (GPIO 2) and structured 115200 baud serial log streaming.

---

## 🏗️ System Architecture

```
                      +--------------------------------------------------+
                      |               LOCAL ENVIRONMENT                  |
                      |                                                  |
 [Upstream Router] <--+--- (STA Mode: 192.168.1.x) ---+                  |
   (WAN / Internet)   |                               |                  |
                      |                      +--------v--------+         |
                      |                      |  ESP32 SoC      |         |
                      |                      |  ResilienceNode |         |
                      |                      +--------^--------+         |
                      |                               |                  |
 [Operator Laptop] <--+--- (AP Mode: 192.168.4.1) ----+                  |
  or Field Tech Phone |  SSID: ResilienceNode-XXXX                       |
                      |  Pass: resilience2024                            |
                      +--------------------------------------------------+
```

### Firmware Subsystem Decomposition

```
+-------------------------------------------------------------------------+
|                              FreeRTOS Core                              |
+-------------------------------------------------------------------------+
|                                                                         |
|  +------------------+   +-------------------+   +--------------------+  |
|  |   wifi_manager   |   |  network_monitor  |   |  incident_engine   |  |
|  |  (STA + AP dual) |-->| (ICMP/TCP Probes) |-->|   (State Machine)  |  |
|  +------------------+   +-------------------+   +--------------------+  |
|           |                       |                        |            |
|           v                       v                        v            |
|  +------------------+   +-------------------+   +--------------------+  |
|  |    telemetry     |<--|    event_store    |<--|     web_server     |  |
|  |  (Rolling Stats) |   |    (NVS Flash)    |   | (REST API + SPA UI)|  |
|  +------------------+   +-------------------+   +--------------------+  |
|                                                                         |
+-------------------------------------------------------------------------+
```

---

## 🧰 Hardware Specifications

| Parameter | Specification | Notes |
| :--- | :--- | :--- |
| **Microcontroller** | Espressif ESP32-WROOM-32D / ESP32-S3 | Dual Xtensa® 32-bit LX6 @ 240 MHz |
| **SRAM** | 520 KB Internal SRAM | Heap budget: >180 KB free during load |
| **Flash Memory** | 4 MB SPI Flash (DIO / QIO) | NVS partition dedicated to black-box logs |
| **Wi-Fi Radio** | 802.11 b/g/n (2.4 GHz) | HT20/HT40, concurrent AP + STA |
| **Power Supply** | 5V DC via Micro-USB / USB-C | Typical draw: 120-180 mA (Peak: 240 mA) |
| **Status Indicator**| Onboard Blue LED (GPIO 2) / RGB WS2812 | State-based blink cadence |
| **Operating Temp** | -40°C to +85°C (Industrial rated) | Suitable for utility cabinets |

---

## ⚡ Quick Start Guide

### 1. Prerequisites
Ensure you have the Espressif IoT Development Framework (**ESP-IDF v5.1 or newer**) installed and configured on your system:
```powershell
# In PowerShell:
idf.py --version
# Expected: ESP-IDF v5.1.x or v5.2.x
```

### 2. Clone the Repository
```powershell
git clone https://github.com/your-org/resiliencenode.git
cd resiliencenode
```

### 3. Build Firmware
Run the dedicated build automation script:
```powershell
.\scripts\build.ps1
```
*Or manually via ESP-IDF:*
```powershell
idf.py set-target esp32
idf.py build
```

### 4. Flash and Monitor
Connect your ESP32 board via USB and run:
```powershell
# Flash to designated COM port (e.g. COM3)
.\scripts\flash.ps1 -Port COM3

# Launch serial monitor with ANSI color decoding
.\scripts\monitor.ps1 -Port COM3
```

---

## 📱 Operating & Access Instructions

1. **Power Up:** Plug ResilienceNode into any 5V USB port or dedicated adapter.
2. **Access Point Broadcast:** The device broadcasts an emergency recovery SSID:
   - **SSID:** `ResilienceNode-XXXX` *(where XXXX represents the last 4 characters of the MAC address)*
   - **Password:** `resilience2024`
   - **Authentication:** WPA2-PSK
3. **Open Dashboard:** Connect your laptop, tablet, or smartphone to the Wi-Fi network and navigate to:
   ```
   http://192.168.4.1
   ```
4. **Configure Uplink:** Navigate to the **Config** tab, enter your facility's Wi-Fi SSID and Password, and click **Save & Connect**.
5. **Real-Time Telemetry:** The dashboard immediately updates at 1 Hz displaying packet latency, jitter, loss %, state indicators, and the rolling incident log.

---

## 📡 REST API Reference

ResilienceNode exposes a clean, lightweight REST API for automation, Prometheus scrapers, and external telemetry systems:

### Core Endpoints

| Method | Endpoint | Description | Sample Output |
| :--- | :--- | :--- | :--- |
| `GET` | `/api/status` | Current system health, state machine status, uptime, and memory | `{"state":"HEALTHY","uptime_sec":1420,"free_heap":198420}` |
| `GET` | `/api/telemetry` | Rolling window probe statistics (RTT, Jitter, Loss %) | `{"min_rtt_ms":12,"avg_rtt_ms":24,"loss_pct":0.0}` |
| `GET` | `/api/incidents`| Historical list of all recorded outage & degradation events | `[{"id":1,"type":"OUTAGE","start":167253,"duration_s":45}]` |
| `POST`| `/api/config` | Update monitoring targets, probe frequencies, and thresholds | `{"probe_interval_ms":2000,"ping_target":"8.8.8.8"}` |
| `GET` | `/api/export` | Full diagnostics bundle (JSON or CSV format) | Comprehensive system dump for post-mortem analysis |

---

## 🧪 Demonstration Scenarios

ResilienceNode includes a full simulation guide in [`docs/DEMO_GUIDE.md`](file:///c:/Rushabh@IMPORTANT/New%20porject/resiliencenode/docs/DEMO_GUIDE.md):

1. **Scenario 1: Boot & Autonomous AP Discovery:** Cold start, SoftAP broadcast, and dashboard loading in < 2 seconds.
2. **Scenario 2: Real-Time Baseline Health:** Uplink connection active; live RTT graphing and zero-loss confirmation.
3. **Scenario 3: Upstream WAN Disruption (The Blind Outage):** Uplink router WAN disconnected. ResilienceNode detects loss, triggers `OUTAGE`, preserves local dashboard at `192.168.4.1`.
4. **Scenario 4: Incident Classification & NVS Persistence:** Event recorded into flash; power-cycle test confirms zero data loss.
5. **Scenario 5: Autonomous Link Recovery:** WAN link restored; system transitions through `RECOVERING` to `HEALTHY`, calculating exact outage downtime.

---

## 📂 Project Structure

```
resiliencenode/
├── .gitignore                      # Comprehensive build and artifact exclusions
├── CMakeLists.txt                  # Top-level ESP-IDF CMake definition
├── README.md                       # Master project overview and quickstart
├── docs/                           # Exhaustive technical documentation
│   ├── ARCHITECTURE.md             # Detailed system architecture & memory budget
│   ├── DEMO_GUIDE.md               # 5-step interactive presentation & demo script
│   ├── PRODUCT_REQUIREMENTS.md     # Full PRD with requirements RN-001 to RN-012
│   ├── PROJECT_STATUS.md           # Engineering readiness & quality gates
│   ├── REQUIREMENTS_TRACEABILITY.md# Verification & traceability matrix
│   ├── TECHNICAL_DECISIONS.md      # Architectural Decision Records (ADRs)
│   └── TEST_REPORT.md              # Hardware validation test suite
├── main/                           # Application source code
│   ├── CMakeLists.txt              # Component CMake definition
│   ├── idf_component.yml           # ESP-IDF component registry metadata
│   ├── main.c                      # FreeRTOS scheduler & module orchestration
│   ├── wifi_manager.c/.h           # AP+STA concurrent radio management
│   ├── network_monitor.c/.h        # ICMP/TCP ping and latency probe engine
│   ├── incident_engine.c/.h        # Finite state machine & incident detector
│   ├── event_store.c/.h            # NVS flash circular black-box storage
│   ├── web_server.c/.h             # Embedded HTTP daemon & REST endpoints
│   ├── telemetry.c/.h              # Rolling statistics calculation engine
│   └── ui/                         # Embedded web dashboard
│       └── index.html              # Minified zero-dependency single page UI
├── pitch/                          # Presentation & Demo Assets
│   ├── PRESENTATION_SCRIPT.md      # Timed hackathon pitch & comprehensive Q&A
│   └── presentation.html           # Dark-mode keyboard-navigable slide deck
└── scripts/                        # Automation & Developer Tooling
    ├── build.ps1                   # One-click firmware compilation script
    ├── flash.ps1                   # Safe flashing utility with COM auto-detect
    └── monitor.ps1                 # High-baud serial monitor launcher
```

---

## ⚠️ Known Limitations & Scope

- **2.4 GHz Band Only:** Base ESP32 hardware supports 802.11 b/g/n (2.4 GHz). Networks requiring 5 GHz or 6 GHz require external dual-band radios or an ESP32-C5 variant.
- **Local Storage Depth:** Default NVS allocation stores up to 100 high-resolution incident events. Older events are rotated according to circular FIFO eviction.
- **Probe Resolution:** Minimum recommended ping interval is 500 ms to preserve Wi-Fi radio power balance and avoid local RF saturation.

---

## 🔮 Roadmap & Future Enhancements

- [ ] **Cellular / LoRaWAN Fallback:** Secondary low-power telemetry transmission back to central NOC during total facility electrical/network blackouts.
- [ ] **Power-over-Ethernet (PoE) Carrier Board:** ESP32-Ethernet integration for rackmount server closet deployment.
- [ ] **Hardware Power Loss Detection:** Supercapacitor backup providing 10-second run time to log clean `POWER_FAILURE` telemetry events.
- [ ] **DNS-over-HTTPS (DoH) Probing:** Enhanced diagnostics to distinguish DNS censorship/tampering from upstream transport loss.

---

## 📄 License

This project is licensed under the **MIT License** — see the [LICENSE](LICENSE) file for details. Open source and built for resilient edge infrastructure everywhere.

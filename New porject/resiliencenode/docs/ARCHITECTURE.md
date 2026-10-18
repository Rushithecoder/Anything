# 🏛️ System Architecture Specification

## Project: ResilienceNode
**Autonomous Edge Network Monitoring Black Box**  
**Document Version:** 1.0.0  
**Target Architecture:** Espressif ESP32 Dual-Core Xtensa LX6 (240 MHz)  
**RTOS:** FreeRTOS (ESP-IDF v5.1+)  

---

## 1. High-Level System Architecture

ResilienceNode is structured as a modular, event-driven real-time embedded system. It leverages the dual-core architecture of the ESP32: Core 0 handles low-level RF, Wi-Fi MAC, and lwIP networking stacks, while Core 1 executes the application logic, network probes, incident classification engine, event storage, and embedded HTTP server.

```
+===================================================================================================+
|                                        PHYSICAL HARDWARE                                          |
|  [ESP32-WROOM-32D]  •  2.4 GHz 802.11b/g/n RF  •  4MB SPI Flash  •  520KB SRAM  •  LED (GPIO 2)   |
+===================================================================================================+
                                                  │
                                                  ▼
+===================================================================================================+
|                                    ESP-IDF & RTOS INFRASTRUCTURE                                  |
|  FreeRTOS v10.4 (Dual Core)  •  lwIP v2.1.3 Dual Netif  •  NVS Flash Driver  •  Hardware TWDT      |
+===================================================================================================+
        │                                         │                                         │
        ▼                                         ▼                                         ▼
+───────────────────+                     +───────────────────+                     +───────────────────+
|   WIFI_MANAGER    |                     |  NETWORK_MONITOR  |                     |  INCIDENT_ENGINE  |
|  (Core 0 / Core 1)|                     |      (Core 1)     |                     |      (Core 1)     |
| ───────────────── |                     | ───────────────── |                     | ───────────────── |
| • STA + SoftAP    |                     | • Async ICMP Ping |                     | • 5-State FSM     |
| • ResilienceNode- |                     | • Gateway Probe   |                     | • Hysteresis Filter|
|   XXXX Broadcast  |                     | • DNS 8.8.8.8/1.1 |                     | • Outage Detect   |
| • Auto-Reconnect  |                     | • Cloud Endpoint  |                     | • Degrade Detect  |
+───────────────────+                     +───────────────────+                     +───────────────────+
        │                                         │                                         │
        │ Event / IP Updates                      │ Probe Results                           │ State Transitions
        ▼                                         ▼                                         ▼
+───────────────────+                     +───────────────────+                     +───────────────────+
|     TELEMETRY     |<────────────────────┤    EVENT_STORE    |<────────────────────┤    WEB_SERVER     |
|      (Core 1)     |  Rolling Metrics    |      (Core 1)     |  Historical Storage |      (Core 1)     |
| ───────────────── |                     | ───────────────── |                     | ───────────────── |
| • 60-slot RingBuf |                     | • NVS Flash Ring  |                     | • Port 80 Daemon  |
| • RTT Min/Avg/Max |                     | • 100 Incidents   |                     | • Embedded SPA UI |
| • RFC 3550 Jitter |                     | • Power-Fail Safe |                     | • JSON REST API   |
| • Loss Percentage |                     | • Atomic Commits  |                     | • Export Engine   |
+───────────────────+                     +───────────────────+                     +───────────────────+
        ▲                                         ▲                                         ▲
        │                                         │                                         │
        +───────────────────+─────────────────────+─────────────────────────────────────────+
                            │
                            ▼
             +─────────────────────────────+
             |       MAIN SUPERVISOR       |
             | • System Initialization     |
             | • Task Health & Watchdog    |
             | • LED Blink Diagnostics     |
             | • Serial UART Telemetry     |
             +─────────────────────────────+
```

---

## 2. Firmware Subsystem Modules

The codebase is organized into clean, decoupled C modules residing under `main/`:

### 2.1 `wifi_manager`
- **Role:** Controls the physical and logical Wi-Fi interfaces in concurrent Station (STA) and Access Point (AP) mode.
- **Key Responsibilities:**
  - Initializes TCP/IP adapter (`esp_netif`) and default ESP event loop.
  - Generates dynamic SoftAP SSID using the hardware MAC: `ResilienceNode-XXXX` (e.g., `ResilienceNode-8A3F`).
  - Configures SoftAP network: static IP `192.168.4.1`, subnet `255.255.255.0`, DHCP pool `192.168.4.2` – `192.168.4.10`.
  - Connects Station interface to target network; manages credentials retrieved from NVS or runtime configuration.
  - Executes exponential backoff reconnection (1s, 2s, 4s, 8s, up to 30s) during Wi-Fi link drops.
  - Ensures total isolation: failure of the Station connection never interrupts the SoftAP or drops client TCP sessions.

### 2.2 `network_monitor`
- **Role:** Active multi-tier probing engine measuring instantaneous reachability and latency.
- **Key Responsibilities:**
  - Implements lightweight non-blocking ICMP echo requests using raw lwIP sockets.
  - Probes three distinct logical tiers concurrently:
    1. **Tier 1 (LAN):** Default Gateway (extracted dynamically from STA DHCP lease).
    2. **Tier 2 (WAN / Core DNS):** Public anycast resolvers (`8.8.8.8` and `1.1.1.1`).
    3. **Tier 3 (Cloud Target):** Configurable HTTP/TCP endpoint (e.g., `1.1.1.1:80` or corporate cloud IP).
  - Enforces configurable timeout windows (default 1000 ms) and probe cadence (default 2000 ms).
  - Packages raw probe outcomes (`target_ip`, `rtt_ms`, `status_code`, `timestamp`) and forwards them to `telemetry` and `incident_engine`.

### 2.3 `incident_engine`
- **Role:** Deterministic Finite State Machine (FSM) that classifies edge connectivity and triggers incident lifecycle events.
- **Key Responsibilities:**
  - Maintains the primary system state: `HEALTHY`, `DEGRADED`, `OUTAGE`, `DISCONNECTED`, `RECOVERING`.
  - Implements hysteresis to prevent flapping: requires 3 consecutive failures to trigger `OUTAGE`/`DISCONNECTED`, and 5 consecutive healthy probes in `RECOVERING` before transitioning to `HEALTHY`.
  - Evaluates root causes:
    - *Gateway unreachable + STA down* $\rightarrow$ `DISCONNECTED` (Local RF / AP failure).
    - *Gateway reachable + All WAN probes timed out* $\rightarrow$ `OUTAGE` (ISP / Uplink failure).
    - *High packet loss (>5%) or high RTT (>150ms)* $\rightarrow$ `DEGRADED` (Congestion / Interference).
  - Emits structured incident open/close events to `event_store`.

### 2.4 `event_store`
- **Role:** Flash persistence engine acting as the black-box flight recorder.
- **Key Responsibilities:**
  - Leverages ESP-IDF Non-Volatile Storage (NVS) with the `rn_events` namespace.
  - Manages a circular FIFO buffer supporting 100 structured incident records without wear-out risks.
  - Atomically writes record headers and data structures to ensure corruption immunity during sudden power removal.
  - Maintains persistent global operational counters: `total_incidents`, `total_downtime_sec`, `boot_count`, `link_flaps`.
  - Provides thread-safe read/query API supporting pagination (`offset`, `limit`) and CSV/JSON export serialization.

### 2.5 `telemetry`
- **Role:** Real-time statistics aggregation and time-series rolling memory buffer.
- **Key Responsibilities:**
  - Maintains a 60-slot in-memory circular buffer storing the last 60 probe cycles.
  - Computes statistical indicators on demand:
    - **Min RTT:** Lowest recorded ping latency in window.
    - **Max RTT:** Peak recorded ping latency in window.
    - **Avg RTT:** Exponential Moving Average (EMA) or arithmetic mean.
    - **RFC 3550 Jitter:** Statistical variance between consecutive packet arrival times:
      $$J(i) = J(i-1) + \frac{|D(i-1, i)| - J(i-1)}{16}$$
    - **Packet Loss Percentage:** Ratio of failed probes to total attempts in window.
  - Guards all telemetry structures with FreeRTOS mutex primitives to guarantee thread safety.

### 2.6 `web_server`
- **Role:** High-performance, low-footprint embedded HTTP server daemon (`esp_http_server`).
- **Key Responsibilities:**
  - Listens on TCP port 80 across both `192.168.4.1` (SoftAP) and the Station DHCP IP.
  - Serves the compiled, zero-dependency Single Page Application dashboard (`index.html`) embedded directly into flash ROM.
  - Implements RESTful JSON endpoints: `/api/status`, `/api/telemetry`, `/api/incidents`, `/api/config`, `/api/export`.
  - Supports chunked HTTP streaming for memory-safe export of large incident logs.

### 2.7 `main` (Supervisor & Diagnostics)
- **Role:** Application bootloader, task supervisor, and hardware I/O driver.
- **Key Responsibilities:**
  - Initializes NVS, Netif, Event Loops, and internal FreeRTOS queues.
  - Spawns and configures FreeRTOS worker tasks with strict core affinity and stack budgets.
  - Registers tasks with the FreeRTOS Task Watchdog Timer (TWDT, 10-second timeout).
  - Drives the onboard status LED (GPIO 2) with distinct visual blink cadences corresponding to FSM states.
  - Emits ANSI-formatted diagnostic logs and periodic headless telemetry over UART at 115200 baud.

---

## 3. FreeRTOS Task Execution Model & Priority Hierarchy

To guarantee deterministic timing and eliminate priority inversion, tasks are assigned dedicated priorities and core affinities:

| Task Name | Core | Priority | Stack Size | Period / Trigger | Description |
| :--- | :---: | :---: | :---: | :---: | :--- |
| `wifi_task` | Core 0 | 18 | 4096 B | Event Driven | ESP-IDF Wi-Fi driver and event dispatcher |
| `net_monitor_task`| Core 1 | 12 | 4096 B | 2000 ms Timer | Executes ICMP echo requests and gathers raw RTT |
| `incident_task` | Core 1 | 10 | 3072 B | Queue Driven | Evaluates probe results through FSM; logs events |
| `httpd_server` | Core 1 | 5 | 8192 B | TCP Socket | Serves HTTP requests, REST API, and static assets |
| `led_diag_task` | Core 1 | 2 | 2048 B | 100 ms Delay | Drives visual LED pulse cadences (GPIO 2) |
| `sys_watchdog` | Core 1 | 1 | 2048 B | 5000 ms Timer | Pet TWDT, monitors heap watermark, serial stream |

---

## 4. End-to-End Data Flow

### 4.1 Probe Cycle & Classification Flow

```
[Timer: 2000ms]
       │
       ▼
[net_monitor_task] ───(Raw ICMP)───> [Default Gateway (192.168.1.1)]
       │           ───(Raw ICMP)───> [Public DNS 1 (8.8.8.8)]
       │           ───(Raw ICMP)───> [Public DNS 2 (1.1.1.1)]
       │
       ▼ (Probe Results: RTT, Status)
[telemetry_engine] ───(Update Rolling 60s Buffer, Recalculate Jitter/Loss)
       │
       ▼ (Push to Queue)
[incident_engine] ───(Evaluate Conditions)
       │
       ├── State Changed? ───YES───> [event_store] ───(Write NVS Flash)
       │                                   │
       └── NO                              ▼
            │                         [led_diag_task] ───(Update Blink Cadence)
            ▼
[FreeRTOS Task Sleep]
```

### 4.2 Web Client Request Flow (Out-of-Band at 192.168.4.1)

```
[Field Tech Phone] ──(HTTP GET /api/status)──> [web_server (Port 80)]
                                                       │
                                 +─────────────────────┴─────────────────────+
                                 │                                           │
                                 ▼                                           ▼
                       [telemetry_get_stats()]                    [incident_engine_state()]
                                 │                                           │
                                 +─────────────────────┬─────────────────────+
                                                       │
                                                       ▼
                                            [JSON Serialization]
                                            (Heap-safe cJSON buffer)
                                                       │
                                                       ▼
[Field Tech Phone] <──(HTTP 200 OK + JSON)─────────────+
```

---

## 5. Incident Engine State Machine (FSM)

The incident engine transitions between 5 deterministic states based on probe criteria:

```
                  +----------------------------------------------+
                  |                                              |
                  |                   BOOT                       |
                  |                                              |
                  +----------------------+-----------------------+
                                         │
                                         ▼ (Wi-Fi Associated & IP Acquired)
                  +----------------------------------------------+
                  |                                              |
     +----------->|                   HEALTHY                    |<------------+
     |            |  • Gateway Reachable                         |             |
     |            |  • DNS Reachable (RTT < 150ms, Loss < 5%)    |             |
     |            +------+----------------+---------------+------+             |
     |                   │                │               │                    |
     | (5 Healthy Probes)│                │               │                    |
     |                   │ (RTT > 150ms   │ (WAN Timeout  │ (Wi-Fi Disconnect  |
     |                   │  OR Loss > 5%) │  >= 3 Cycles) │  OR GW Unreachable)|
     |                   ▼                ▼               ▼                    |
     |            +------------+   +------------+   +--------------+           |
     |            |  DEGRADED  |   |   OUTAGE   |   | DISCONNECTED |           |
     |            +-----+------+   +-----+------+   +-------+------+           |
     |                  │                │                  │                  |
     |                  │ (Recovers)     │ (Probe Success)  │ (Link Restored)  |
     |                  +--------+       +--------+         │                  |
     |                           │                │         │                  |
     |                           ▼                ▼         ▼                  |
     |                    +─────────────────────────────────────+              |
     +--------------------+             RECOVERING              +--------------+
                          |   • Requires 5 clean probe cycles   |
                          |   • Transition to HEALTHY on count=5|
                          +-------------------------------------+
```

### State Transition Matrix

| Current State | Event / Condition | Next State | Action Taken |
| :--- | :--- | :--- | :--- |
| **BOOT** | Wi-Fi associated, valid IP acquired | `RECOVERING` | Initialize telemetry, start probe timer |
| **HEALTHY** | RTT > 150ms OR Loss 5%–50% | `DEGRADED` | Log `INCIDENT_DEGRADED`, update LED |
| **HEALTHY** | Gateway alive, WAN DNS failed ≥ 3 cycles | `OUTAGE` | Open `INCIDENT_OUTAGE`, record start time |
| **HEALTHY** | STA disconnected OR Gateway unreachable | `DISCONNECTED` | Open `INCIDENT_DISCONNECTED`, backoff on |
| **DEGRADED** | RTT < 100ms AND Loss < 5% for 3 cycles | `HEALTHY` | Close degradation incident, record duration |
| **DEGRADED** | WAN DNS failed ≥ 3 cycles | `OUTAGE` | Escalate incident to `OUTAGE` |
| **OUTAGE** | First successful WAN probe received | `RECOVERING` | Set recovery counter = 1 |
| **DISCONNECTED**| Wi-Fi reassociated, IP acquired | `RECOVERING` | Set recovery counter = 1 |
| **RECOVERING** | 5 consecutive successful probe cycles | `HEALTHY` | Close incident, calculate downtime, commit NVS |
| **RECOVERING** | Any probe fails before count reaches 5 | `OUTAGE` / `DISCONNECTED` | Reset counter, remain in failure state |

---

## 6. Embedded Web Server & REST API Specification

### 6.1 `GET /api/status`
Returns general node health, network addressing, and current state.
```json
{
  "device": "ResilienceNode",
  "version": "1.0.0",
  "state": "HEALTHY",
  "uptime_sec": 3840,
  "free_heap_bytes": 194560,
  "min_free_heap_bytes": 182100,
  "wifi": {
    "sta_connected": true,
    "sta_ip": "192.168.1.145",
    "sta_rssi_dbm": -58,
    "ap_ssid": "ResilienceNode-8A3F",
    "ap_clients": 1
  },
  "current_incident": null
}
```

### 6.2 `GET /api/telemetry`
Returns instantaneous rolling-window metrics and target probe breakdowns.
```json
{
  "timestamp": 3840,
  "window_sec": 60,
  "metrics": {
    "last_rtt_ms": 18,
    "min_rtt_ms": 11,
    "avg_rtt_ms": 22,
    "max_rtt_ms": 48,
    "jitter_ms": 3,
    "packet_loss_pct": 0.0,
    "total_probes": 60,
    "failed_probes": 0
  },
  "targets": [
    {"target": "192.168.1.1", "name": "Gateway", "reachable": true, "rtt_ms": 2},
    {"target": "8.8.8.8", "name": "DNS-Primary", "reachable": true, "rtt_ms": 18},
    {"target": "1.1.1.1", "name": "DNS-Secondary", "reachable": true, "rtt_ms": 16}
  ],
  "history": [21, 19, 18, 24, 22, 19, 18, 17, 23, 18]
}
```

### 6.3 `GET /api/incidents`
Returns historical incident records with pagination parameters (`?limit=20&offset=0`).
```json
{
  "total_records": 3,
  "offset": 0,
  "limit": 20,
  "incidents": [
    {
      "id": 3,
      "type": "OUTAGE",
      "trigger": "WAN_DNS_TIMEOUT",
      "start_uptime_s": 2400,
      "end_uptime_s": 2492,
      "duration_s": 92,
      "peak_rtt_ms": 0,
      "packet_loss_pct": 100.0,
      "resolved": true
    },
    {
      "id": 2,
      "type": "DEGRADED",
      "trigger": "HIGH_JITTER_AND_LOSS",
      "start_uptime_s": 1200,
      "end_uptime_s": 1340,
      "duration_s": 140,
      "peak_rtt_ms": 280,
      "packet_loss_pct": 28.5,
      "resolved": true
    }
  ]
}
```

### 6.4 `POST /api/config`
Updates dynamic monitoring parameters and target network credentials.
```json
// Request Body:
{
  "sta_ssid": "Enterprise_Uplink",
  "sta_password": "supersecretpassword",
  "probe_interval_ms": 2000,
  "target_dns": "8.8.8.8"
}

// Response (HTTP 200 OK):
{
  "status": "SUCCESS",
  "message": "Configuration saved to NVS. Reconnection initiated."
}
```

### 6.5 `GET /api/export`
Exports the entire black-box log as a downloadable CSV or JSON stream:
```http
HTTP/1.1 200 OK
Content-Type: text/csv
Content-Disposition: attachment; filename="resiliencenode_log.csv"
Transfer-Encoding: chunked

id,type,trigger,start_s,end_s,duration_s,loss_pct,peak_rtt_ms
1,DEGRADED,HIGH_RTT,420,510,90,12.5,210
2,OUTAGE,WAN_UNREACHABLE,1120,1215,95,100.0,0
3,DISCONNECTED,STA_DISCONNECTED,2010,2045,35,100.0,0
```

---

## 7. Memory Budget & Storage Strategy

### 7.1 Internal SRAM Budget (520 KB Total)

```
+───────────────────────────────────────────────────────────+
|               520 KB Total Internal SRAM                  |
+───────────────────────────────────────────────────────────+
| [Reserved by ROM / System Bus]               | ~120 KB    |
| [FreeRTOS Stacks (6 tasks)]                  | ~24 KB     |
| [lwIP Network Buffers / Sockets]             | ~64 KB     |
| [Wi-Fi Driver RX/TX Queues & DMA]            | ~72 KB     |
| [Application Rolling Telemetry Buffer]       | ~8 KB      |
| [HTTP Server Worker & Connection Buffers]    | ~16 KB     |
| [Available Dynamic Heap (Headroom)]          | >210 KB    |
+───────────────────────────────────────────────────────────+
```

### 7.2 Flash Partition Layout (4 MB Flash)

```
0x00000000 ┌─────────────────────────────────────────┐
           │ Bootloader (32 KB)                      │
0x00008000 ├─────────────────────────────────────────┤
           │ Partition Table (4 KB)                  │
0x00009000 ├─────────────────────────────────────────┤
           │ NVS - Non-Volatile Storage (24 KB)      │  <-- Incidents & Counters
0x0000F000 ├─────────────────────────────────────────┤
           │ PHY Init Data (4 KB)                    │
0x00010000 ├─────────────────────────────────────────┤
           │ Factory App Image (2048 KB)             │  <-- Firmware Binary & Embedded UI
0x00210000 ├─────────────────────────────────────────┤
           │ Free / Storage Partition (1984 KB)      │  <-- Future OTA / LittleFS
0x00400000 └─────────────────────────────────────────┘
```

---

## 8. Technology Stack Reference

| Layer | Component | Version / Specification | Rationale |
| :--- | :--- | :--- | :--- |
| **Hardware** | ESP32-WROOM-32 | Dual Core LX6 @ 240MHz | High availability, integrated dual Wi-Fi, low power |
| **SDK / Framework**| ESP-IDF | v5.1.2 LTS | Native C framework with robust hardware timers & lwIP |
| **Operating System**| FreeRTOS | v10.4.3 (SMP) | Real-time task scheduling and deterministic priorities |
| **Network Stack** | lwIP | v2.1.3 | Low-footprint TCP/IP stack with raw ICMP socket support |
| **HTTP Engine** | `esp_http_server` | ESP-IDF Native Component | Non-blocking, multi-session HTTP daemon with URI dispatch |
| **Data Persistence**| ESP NVS | Native Flash Key-Value | Built-in wear-leveling and atomic sector transaction safety |
| **Serialization** | cJSON | v1.7.15 | Embedded JSON parser/generator with minimal heap usage |
| **Dashboard UI** | HTML5 / CSS3 / ES6 | Vanilla JS (Zero CDN) | Compressed and embedded in ROM; works completely offline |

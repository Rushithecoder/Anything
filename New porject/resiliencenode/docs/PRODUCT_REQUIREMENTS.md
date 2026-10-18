# 📋 Product Requirements Document (PRD)

## Project: ResilienceNode
**Autonomous Edge Network Monitoring Black Box**  
**Document Version:** 1.0.0  
**Target Platform:** Espressif ESP32 (ESP-IDF v5.1+)  
**Status:** Approved for Implementation  

---

## 1. Executive Vision & Scope

ResilienceNode is an autonomous, ultra-reliable network monitoring appliance designed to eliminate the "blind outage" paradox. When edge networks fail, upstream cloud monitoring loses connectivity and provides no diagnostic insight. ResilienceNode bridges this gap by continuously executing active network health probes from within the local network while maintaining an isolated, independent out-of-band management Access Point (`ResilienceNode-XXXX` on `192.168.4.1`). 

The device functions as an embedded flight data recorder ("black box") for edge networks: detecting link degradation, classifying outage root causes, logging events into persistent non-volatile flash memory, and serving a live real-time dashboard and REST API without external cloud or internet dependencies.

---

## 2. System Actor & User Personas

- **Network Reliability Engineer (Site Admin):** Requires verifiable, timestamped logs of link drops, ISP latency spikes, and DNS failures to enforce SLAs and diagnose chronic instability.
- **Field Support Technician:** Connects directly to the node’s emergency AP via smartphone or laptop during a total network blackout to instantly inspect local gateway status, router reachability, and historical failure triggers.
- **Automation / NOC Scraper:** Queries the local REST API (`/api/status`, `/api/telemetry`, `/api/incidents`) to ingest high-frequency edge metrics into local SIEM/observability pipelines.

---

## 3. Comprehensive Requirements Catalog

```
+---------+-------------------------------------------------+----------+----------+
| Req ID  | Requirement Title                               | Priority | Category |
+---------+-------------------------------------------------+----------+----------+
| RN-001  | Autonomous Dual-Mode Wi-Fi Operation (AP + STA) | P0       | Network  |
| RN-002  | Active Multi-Target Network Health Probing      | P0       | Telemetry|
| RN-003  | Real-Time Latency, Jitter & Loss Telemetry      | P0       | Telemetry|
| RN-004  | Deterministic Incident Classification Engine    | P0       | Engine   |
| RN-005  | Automated Outage & Degradation Event Logging    | P0       | Engine   |
| RN-006  | Non-Volatile Persistent Event Store (NVS)       | P0       | Storage  |
| RN-007  | Embedded Zero-Dependency HTTP Web Server        | P1       | Server   |
| RN-008  | Real-Time Self-Hosted Single Page Dashboard     | P1       | UI/UX    |
| RN-009  | Standardized RESTful Telemetry & Control API    | P1       | API      |
| RN-010  | Out-of-Band Incident Diagnostics & Export       | P1       | Operab.  |
| RN-011  | Resilient Auto-Reconnection & Hardware Watchdog | P0       | Reliab.  |
| RN-012  | On-Device Hardware Signaling & Serial Telemetry | P2       | Diagnos. |
+---------+-------------------------------------------------+----------+----------+
```

---

### Requirement RN-001: Autonomous Dual-Mode Wi-Fi Operation (AP + STA)
- **Category:** Network Connectivity
- **Priority:** P0 (Critical)
- **Description:**  
  The system must initialize and maintain concurrent Station (STA) and Soft Access Point (AP) modes using the ESP32 dual Wi-Fi stack. The Station interface connects to the monitored local Wi-Fi infrastructure (uplink network). The SoftAP interface broadcasts an independent, isolated emergency management Wi-Fi network that remains active regardless of Station link status, DHCP leases, or router availability.
- **Acceptance Criteria:**
  1. SoftAP broadcasts with SSID format `ResilienceNode-XXXX`, where `XXXX` represents the last 4 characters of the ESP32 Wi-Fi Base MAC address in uppercase hexadecimal.
  2. SoftAP uses WPA2-PSK authentication with default passphrase `resilience2024` on channel 1 (or matching STA channel) with default gateway IP `192.168.4.1` and subnet `255.255.255.0`.
  3. Built-in DHCP server assigns IP addresses in the `192.168.4.2` – `192.168.4.10` pool to connecting management clients within 3 seconds of association.
  4. Station interface successfully associates with target 2.4 GHz 802.11 b/g/n networks supporting WPA/WPA2-Personal, obtaining IP configuration via DHCP.
  5. Complete failure or disconnection of the Station interface (e.g., target router powered off) shall NOT drop, reset, or degrade the SoftAP broadcast or existing client TCP sessions.

---

### Requirement RN-002: Active Multi-Target Network Health Probing
- **Category:** Telemetry & Monitoring
- **Priority:** P0 (Critical)
- **Description:**  
  The system must execute continuous, non-blocking active network health probes across multiple logical network tiers to determine the exact boundary of connectivity failures. Probes must target: (a) Local Default Gateway (LAN tier), (b) Primary Public DNS Resolver (8.8.8.8, WAN tier), (c) Secondary Public DNS Resolver (1.1.1.1, Redundant WAN tier), and (d) Configurable Cloud Application Endpoint (Services tier).
- **Acceptance Criteria:**
  1. Default Gateway probe executes via ICMP ping every `probe_interval` (default 2000 ms, configurable 500 ms – 10000 ms).
  2. Public DNS probes (`8.8.8.8` and `1.1.1.1`) execute concurrently every `probe_interval` with individual timeout threshold of 1000 ms.
  3. Probe engine uses non-blocking asynchronous sockets / lwIP raw ICMP callbacks to prevent FreeRTOS task starvation.
  4. Each probe cycle records: Target IP, boolean reachability status, round-trip time in milliseconds (resolution ≤ 1 ms), and error code if failed (e.g., Timeout, Unreachable, Network Down).
  5. Probes are automatically paused when the Station interface reports `WIFI_EVENT_STA_DISCONNECTED` and immediately resume upon `IP_EVENT_STA_GOT_IP`.

---

### Requirement RN-003: Real-Time Latency, Jitter & Loss Telemetry
- **Category:** Telemetry & Statistics
- **Priority:** P0 (Critical)
- **Description:**  
  The system must maintain an in-memory rolling time-series window of network performance metrics, computing statistical indicators of network health including Minimum RTT, Maximum RTT, Exponential Moving Average RTT, Packet Loss Percentage, and Inter-packet Jitter.
- **Acceptance Criteria:**
  1. Rolling buffer retains the most recent 60 probe cycles (1-minute window at default 1 Hz probing).
  2. Packet loss percentage is calculated over the rolling window as: `Loss% = (Failed_Probes / Total_Probes) * 100`.
  3. Jitter calculation follows RFC 3550 statistical variance: `Jitter = Jitter + (|Diff_RTT| - Jitter) / 16`.
  4. System tracks Wi-Fi physical link quality: RSSI (dBm), channel, and Wi-Fi disconnect/reconnect flap counter.
  5. Telemetry engine updates statistics atomically, protected by FreeRTOS mutexes to prevent race conditions during web server reads.

---

### Requirement RN-004: Deterministic Incident Classification Engine
- **Category:** Intelligence & State Machine
- **Priority:** P0 (Critical)
- **Description:**  
  The system must evaluate incoming probe results through a deterministic Finite State Machine (FSM) to classify the overall network status into five discrete operational states: `HEALTHY`, `DEGRADED`, `OUTAGE`, `DISCONNECTED`, and `RECOVERING`.
- **Acceptance Criteria:**
  1. **HEALTHY:** Station connected, Default Gateway reachable, Public DNS reachable, Loss % < 5%, Average RTT < 100 ms.
  2. **DEGRADED:** Station connected and internet reachable, but Loss % between 5% and 50% OR Average RTT > 150 ms OR Jitter > 40 ms.
  3. **OUTAGE:** Station connected to AP and Default Gateway is reachable, but ALL public internet targets fail consecutively for ≥ 3 cycles (ISP / WAN failure).
  4. **DISCONNECTED:** Station Wi-Fi physical link lost (`WIFI_EVENT_STA_DISCONNECTED`) or Default Gateway unreachable for ≥ 3 cycles (Local LAN / Router failure).
  5. **RECOVERING:** Transition state triggered upon first successful probe following an `OUTAGE` or `DISCONNECTED` state, requiring 5 consecutive successful probe cycles before returning to `HEALTHY`.
  6. State transitions must emit an internal notification event within 100 ms of condition fulfillment.

---

### Requirement RN-005: Automated Outage & Degradation Event Logging
- **Category:** Event Logging & Auditing
- **Priority:** P0 (Critical)
- **Description:**  
  The system must automatically generate structured incident records whenever the state machine transitions out of `HEALTHY` or between failure states. Each incident record must capture comprehensive root-cause metadata, entry/exit timestamps, peak latency, and packet loss figures.
- **Acceptance Criteria:**
  1. An incident record is initiated immediately upon transition from `HEALTHY` to `DEGRADED`, `OUTAGE`, or `DISCONNECTED`.
  2. The record captures: Incident ID (monotonic 32-bit counter), Incident State, Start Timestamp (uptime seconds / RTC), Root Cause Trigger (e.g., "DNS_TIMEOUT", "GATEWAY_UNREACHABLE", "HIGH_JITTER"), Peak RTT, and Packet Loss %.
  3. Upon recovery back to `HEALTHY`, the incident record is closed, and total outage duration (seconds) is calculated and recorded.
  4. Active incidents are updated in real-time so that ongoing outages display current elapsed duration even prior to resolution.

---

### Requirement RN-006: Non-Volatile Persistent Event Store (NVS)
- **Category:** Data Storage & Black Box Persistence
- **Priority:** P0 (Critical)
- **Description:**  
  All closed incident records and cumulative operational statistics must be written to flash memory using the ESP32 Non-Volatile Storage (NVS) partition. The store must survive hard resets, power cuts, and watchdog reboots without data corruption.
- **Acceptance Criteria:**
  1. Incidents are stored in a dedicated NVS namespace (`rn_events`) structured as a circular FIFO buffer supporting at least 100 historical incidents.
  2. Write operations utilize wear-leveling techniques inherent to ESP-IDF NVS; single-sector churn is avoided by sequential index hashing.
  3. Data format ensures atomic writes: power failure during an incident write shall never corrupt previously committed incident records.
  4. Cumulative lifetime counters are maintained in NVS: Total Incidents, Total Outage Seconds, Total Flaps, and Boot Count.
  5. An API command (`POST /api/incidents/clear`) allows authorized clearing of the event store while safely resetting pointers.

---

### Requirement RN-007: Embedded Zero-Dependency HTTP Web Server
- **Category:** Server Infrastructure
- **Priority:** P1 (High)
- **Description:**  
  The system must embed an ultra-lightweight HTTP 1.1 web server running on port 80 based on ESP-IDF `esp_http_server`. The server must bind to both the Station IP and SoftAP IP (`192.168.4.1`), serving static dashboard assets and dynamic REST API endpoints concurrently.
- **Acceptance Criteria:**
  1. Server listens on port 80 with support for at least 4 concurrent TCP client connections.
  2. HTTP Keep-Alive and chunked transfer encoding are supported.
  3. Static assets (`index.html`, styles, and scripts) are embedded in flash ROM via CMake `EMBED_FILES` and served with GZIP compression headers.
  4. Asset delivery latency for the initial page load is < 150 ms over SoftAP.
  5. Server handles malformed requests and unmapped URIs with standard HTTP 404/400 responses without triggering heap leaks or task panics.

---

### Requirement RN-008: Real-Time Self-Hosted Single Page Dashboard
- **Category:** User Interface & Visuals
- **Priority:** P1 (High)
- **Description:**  
  The device must host a responsive, single-page application (SPA) dashboard accessible via standard modern web browsers. The interface must feature a dark-mode industrial diagnostic aesthetic, zero external CDN or cloud script dependencies, and sub-second polling for real-time telemetry updates.
- **Acceptance Criteria:**
  1. Dashboard renders correctly on desktop, tablet, and mobile displays without horizontal scroll overflow.
  2. UI displays: (a) Overall System State Badge (Color-coded: Green = Healthy, Yellow = Degraded, Red = Outage/Disconnected), (b) Real-time RTT Gauges / SVG Charts, (c) Packet Loss & Jitter Cards, (d) Target Probing Status Matrix, (e) Historical Incident Table, (f) System Vitals (Uptime, Free Heap, Wi-Fi RSSI).
  3. Telemetry values auto-refresh every 1000 ms via asynchronous `fetch()` requests to `/api/telemetry` and `/api/status`.
  4. Total asset payload (HTML + CSS + JS) uncompressed is ≤ 40 KB, enabling instantaneous offline rendering from ESP32 flash.
  5. The UI includes an interactive configuration modal to update target Wi-Fi credentials and probe parameters directly from the browser.

---

### Requirement RN-009: Standardized RESTful Telemetry & Control API
- **Category:** API & Interoperability
- **Priority:** P1 (High)
- **Description:**  
  The system must expose a clean, documented RESTful API delivering telemetry, system status, incident logs, and administrative controls in JSON format.
- **Acceptance Criteria:**
  1. `GET /api/status`: Returns JSON with state, uptime, free heap, min free heap, Wi-Fi RSSI, STA IP, AP client count, and active incident summary.
  2. `GET /api/telemetry`: Returns JSON with current RTT, rolling min/avg/max RTT, jitter, packet loss %, probe target list, and historical RTT array (last 30 samples).
  3. `GET /api/incidents`: Returns JSON array of all stored incidents ordered newest to oldest, with pagination support (`limit` and `offset` query parameters).
  4. `POST /api/config`: Accepts JSON payload to update STA SSID, STA Password, Ping Targets, and Probe Interval; validates input and saves to NVS.
  5. `GET /api/export`: Generates downloadable diagnostics report in JSON or standard CSV format for offline post-mortem reporting.

---

### Requirement RN-010: Out-of-Band Incident Diagnostics & Export
- **Category:** Operations & Survivability
- **Priority:** P1 (High)
- **Description:**  
  During an active WAN outage, router failure, or physical Ethernet disconnection, the device must maintain full out-of-band operational capability over its SoftAP (`192.168.4.1`). Field personnel must be able to download the complete diagnostic incident history directly to a mobile device or laptop without internet connectivity.
- **Acceptance Criteria:**
  1. SoftAP connectivity and the web dashboard must remain 100% operational when the Station link is entirely down or searching for AP.
  2. The dashboard provides an "Export CSV" and "Export JSON" button that streams the entire incident store directly as a browser download file (`resiliencenode_report_<timestamp>.csv`).
  3. The export includes all historical outages, transition timestamps, durations, and peak degradation metrics.
  4. Export generation utilizes stream chunking to prevent ESP32 memory exhaustion when exporting large incident logs.

---

### Requirement RN-011: Resilient Auto-Reconnection & Hardware Watchdog
- **Category:** Reliability & Fault Tolerance
- **Priority:** P0 (Critical)
- **Description:**  
  The device must operate unattended for months without freezing, memory leaking, or locking up. The Wi-Fi manager must implement intelligent exponential backoff reconnection to handle AP reboots and temporary interference, and the system must be guarded by the ESP32 Hardware Task Watchdog Timer (TWDT).
- **Acceptance Criteria:**
  1. Upon Wi-Fi disconnection, Station interface enters exponential backoff retry: 1s, 2s, 4s, 8s, up to a maximum cap of 30s between attempts.
  2. FreeRTOS Task Watchdog Timer (TWDT) is initialized with a 10-second timeout monitoring the main orchestration task and network monitor task.
  3. System monitors free heap; if available heap falls below 20 KB (critical threshold), the system performs emergency garbage collection and logs a warning event before any potential OOM crash.
  4. Memory leak testing validates that continuous 24-hour operation with 1 Hz polling produces zero net heap loss (< 1 KB fluctuation).

---

### Requirement RN-012: On-Device Hardware Signaling & Serial Telemetry Stream
- **Category:** Hardware Diagnostics
- **Priority:** P2 (Medium)
- **Description:**  
  The system must provide physical at-a-glance status indication via the onboard LED (GPIO 2) and output structured human-readable and machine-parseable log telemetry over the hardware UART serial interface at 115200 baud.
- **Acceptance Criteria:**
  1. Onboard LED reflects state machine status via distinct visual cadences:
     - `HEALTHY`: Solid ON with a brief 100 ms pulse every 3 seconds (heartbeat).
     - `DEGRADED`: Slow flashing (1 Hz, 50% duty cycle).
     - `OUTAGE`: Rapid double flash every 1 second.
     - `DISCONNECTED`: Rapid flashing (4 Hz).
     - `RECOVERING`: Breathing / pulsing cadence.
  2. Hardware UART outputs ANSI color-coded log lines at 115200 baud (8-N-1) using standard ESP log levels (`ESP_LOGI`, `ESP_LOGW`, `ESP_LOGE`).
  3. Serial stream outputs an ASCII telemetry summary line every 5 seconds for headless monitoring and terminal debugging.

---

## 4. Non-Functional Requirements Summary

| Dimension | Specification | Verification Method |
| :--- | :--- | :--- |
| **Boot Time** | Cold boot to SoftAP ready ≤ 1500 ms; First probe ≤ 3000 ms | Oscilloscope / Serial timestamp |
| **Memory Headroom** | Minimum free internal heap ≥ 120 KB under full web load | `/api/status` heap monitoring |
| **Flash Endurance** | Minimum 100,000 incident writes without sector exhaustion | NVS wear-leveling calculation |
| **Power Consumption** | Average ≤ 160 mA @ 5V DC (0.8 W); Peak ≤ 240 mA | USB Power Meter / In-line shunt |
| **MTBF** | Mean Time Between Failures > 50,000 hours uninterrupted | Continuous burn-in stress test |

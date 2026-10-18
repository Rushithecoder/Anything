# 🧪 Hardware Verification & Test Report

## Project: ResilienceNode
**Autonomous Edge Network Monitoring Black Box**  
**Document Version:** 1.0.0  
**Test Suite Release:** v1.0-RC1  
**Execution Status:** Initial Baseline Template (Hardware Testing Pending)  

---

## 1. Executive Test Execution Summary

```
+-----------------------------------------------------------------------------------+
|                            TEST RUN STATUS SUMMARY                                |
+-----------------------+---------------------+-------------------+-----------------+
| Total Test Cases: 12  | Passed: 0           | Failed: 0         | Blocked: 0      |
+-----------------------+---------------------+-------------------+-----------------+
| Execution Rate: 0.0%  | Pass Rate: N/A      | Pending (NOT_RUN): 12             |
+-----------------------+---------------------+-------------------+-----------------+
```

> [!NOTE]  
> All test cases below are structured with exact execution steps and expected criteria. As physical hardware validation is scheduled for the bench evaluation phase, all tests are initialized to **`NOT_RUN`**.

---

## 2. Test Environment & Apparatus

| Equipment / Parameter | Specification | Notes |
| :--- | :--- | :--- |
| **Device Under Test (DUT)** | ESP32-WROOM-32D Development Board | Dual-Core LX6 @ 240MHz, 4MB Flash |
| **Firmware Version** | ResilienceNode v1.0.0-dev | Compiled via ESP-IDF v5.1.2 |
| **Operating System** | Windows 11 Enterprise (Host Testbed) | PowerShell 7.4 / ESP-IDF Tools |
| **Serial Interface** | Silicon Labs CP2102 / CH340 USB-to-UART | Baud: 115200, 8-N-1 |
| **Test Router (Uplink)** | Dual-band 802.11ac Wi-Fi Router | 2.4 GHz SSID: `DemoNetwork` |
| **Test Client** | Standard Wi-Fi Laptop + Google Chrome | Used for 192.168.4.1 out-of-band evaluation |
| **Traffic Analyzer** | Wireshark v4.2 + Raw Wi-Fi Monitor NIC | Packet inspection and timing verification |

---

## 3. Detailed Test Case Specifications

---

### TC-001: Autonomous Dual-Mode Wi-Fi Operation & SoftAP Broadcast
- **Requirement Reference:** RN-001
- **Category:** Connectivity
- **Initial Status:** `NOT_RUN`
- **Pre-conditions:** ESP32 flashed with firmware; no prior Wi-Fi credentials stored in NVS.
- **Test Procedure:**
  1. Apply 5V power to the ESP32 USB port.
  2. Perform Wi-Fi RF beacon scan using host laptop.
  3. Validate SSID name matches pattern `ResilienceNode-XXXX` with last 4 characters of device MAC.
  4. Connect client device using WPA2 passphrase `resilience2024`.
  5. Inspect client network adapter IP assignment via `ipconfig` / `ifconfig`.
  6. In dashboard, configure Station SSID to `DemoNetwork` and verify concurrent association.
- **Expected Results:**
  - SoftAP beacons appear within 2000 ms of cold boot.
  - Client receives IP in the range `192.168.4.2` – `192.168.4.10` with Gateway `192.168.4.1`.
  - Station interface simultaneously associates with `DemoNetwork` and obtains upstream IP.
  - Pinging `192.168.4.1` succeeds with RTT < 5 ms.
- **Actual Results:** *Pending hardware bench execution.*
- **Verdict:** `NOT_RUN`

---

### TC-002: Multi-Target ICMP Ping Engine & Timeout Handling
- **Requirement Reference:** RN-002
- **Category:** Telemetry & Network
- **Initial Status:** `NOT_RUN`
- **Pre-conditions:** Station interface connected to uplink router with active internet access.
- **Test Procedure:**
  1. Launch serial monitor at 115200 baud.
  2. Observe ICMP probe cycles dispatched every 2000 ms.
  3. Open Wireshark on LAN to monitor ICMP echo requests and replies.
  4. Block ICMP echo replies for `8.8.8.8` via router firewall rule while allowing `192.168.1.1`.
  5. Verify timeout handling and response time recording.
- **Expected Results:**
  - Probes to Gateway, `8.8.8.8`, and `1.1.1.1` fire concurrently.
  - Normal RTT values are reported accurately within 1 ms of Wireshark measurements.
  - Blocked destination triggers timeout flag after 1000 ms without hanging the FreeRTOS task.
- **Actual Results:** *Pending hardware bench execution.*
- **Verdict:** `NOT_RUN`

---

### TC-003: Real-Time Latency, RFC 3550 Jitter & Rolling Window Loss
- **Requirement Reference:** RN-003
- **Category:** Telemetry & Statistics
- **Initial Status:** `NOT_RUN`
- **Pre-conditions:** Node operating in `HEALTHY` state; 60 probe cycles completed.
- **Test Procedure:**
  1. Query `GET /api/telemetry` over HTTP.
  2. Inspect `metrics` object: `min_rtt_ms`, `avg_rtt_ms`, `max_rtt_ms`, `jitter_ms`, `packet_loss_pct`.
  3. Induce artificial 30% packet loss using test router WAN packet drop emulator.
  4. Observe rolling window update over 60 seconds.
- **Expected Results:**
  - Computed average RTT matches mathematical mean of in-memory rolling buffer.
  - Jitter complies with RFC 3550 exponential filter formula.
  - Packet loss percentage dynamically rises and reflects dropped packet ratio (~30%).
- **Actual Results:** *Pending hardware bench execution.*
- **Verdict:** `NOT_RUN`

---

### TC-004: FSM Incident Classification & Hysteresis State Transitions
- **Requirement Reference:** RN-004
- **Category:** Intelligence & State Machine
- **Initial Status:** `NOT_RUN`
- **Pre-conditions:** Node in `HEALTHY` state; serial logger active.
- **Test Procedure:**
  1. Disconnect upstream WAN cable from test router (Gateway remains reachable; WAN fails).
  2. Observe consecutive failure counter over probe cycles 1, 2, and 3.
  3. Validate state transition to `OUTAGE` on cycle 3.
  4. Reconnect WAN cable; observe first successful probe.
  5. Validate state transition to `RECOVERING` and monitor counter until 5 clean probes.
- **Expected Results:**
  - Transient single packet drop does not trigger outage (hysteresis filter).
  - Outage triggers precisely after 3 consecutive WAN probe failures.
  - Recovery requires exactly 5 consecutive clean probe cycles before returning to `HEALTHY`.
- **Actual Results:** *Pending hardware bench execution.*
- **Verdict:** `NOT_RUN`

---

### TC-005: Automated Outage Event Lifecycle & Duration Tracking
- **Requirement Reference:** RN-005
- **Category:** Event Logging
- **Initial Status:** `NOT_RUN`
- **Pre-conditions:** Node in `HEALTHY` state.
- **Test Procedure:**
  1. Sever internet connection for exactly 60 seconds.
  2. Query `GET /api/status` during the outage to inspect active incident.
  3. Restore internet connection and wait for `HEALTHY` recovery.
  4. Query `GET /api/incidents` to review the closed incident record.
- **Expected Results:**
  - Active incident object appears in `/api/status` within 6 seconds of failure.
  - Incident `duration_s` increments accurately in real-time.
  - Upon recovery, closed incident record reflects `duration_s` ~60s (±3s) with root cause `WAN_DNS_TIMEOUT`.
- **Actual Results:** *Pending hardware bench execution.*
- **Verdict:** `NOT_RUN`

---

### TC-006: Persistent Non-Volatile Flash Event Store & Power-Cut Resilience
- **Requirement Reference:** RN-006
- **Category:** Flash Storage & Reliability
- **Initial Status:** `NOT_RUN`
- **Pre-conditions:** At least 2 incidents recorded into flash memory.
- **Test Procedure:**
  1. Verify incidents exist via `GET /api/incidents`.
  2. Abruptly disconnect 5V USB power supply from ESP32 during runtime.
  3. Leave powered off for 10 seconds.
  4. Reconnect 5V power supply and wait for boot.
  5. Connect to `http://192.168.4.1/api/incidents`.
- **Expected Results:**
  - NVS partition mounts cleanly with zero checksum errors.
  - All previously stored incident records remain intact with valid data fields.
  - Boot counter in `/api/status` increments by 1.
- **Actual Results:** *Pending hardware bench execution.*
- **Verdict:** `NOT_RUN`

---

### TC-007: Embedded Lightweight HTTP Server Concurrency & Keep-Alive
- **Requirement Reference:** RN-007
- **Category:** Web Infrastructure
- **Initial Status:** `NOT_RUN`
- **Pre-conditions:** Web server running on port 80.
- **Test Procedure:**
  1. Execute concurrent HTTP benchmark using `curl` / `ab` (ApacheBench):
     `ab -n 100 -c 4 http://192.168.4.1/api/status`
  2. Verify HTTP response codes, latency, and socket reuse.
  3. Monitor free heap memory during and after the test run.
- **Expected Results:**
  - 100% of requests return HTTP 200 OK.
  - Zero dropped connections or socket deadlocks.
  - Free heap returns to pre-test baseline (>180 KB free); zero heap leaks detected.
- **Actual Results:** *Pending hardware bench execution.*
- **Verdict:** `NOT_RUN`

---

### TC-008: Zero-Dependency Single Page Web Dashboard Offline Rendering
- **Requirement Reference:** RN-008
- **Category:** User Interface
- **Initial Status:** `NOT_RUN`
- **Pre-conditions:** Client laptop connected exclusively to `ResilienceNode-XXXX` with all other network adapters disabled (no cellular, no secondary Wi-Fi).
- **Test Procedure:**
  1. Open browser in incognito mode (empty cache).
  2. Navigate to `http://192.168.4.1`.
  3. Inspect Browser Developer Tools (Network & Console tabs).
  4. Verify that all assets (HTML, CSS, JS, SVG icons) load with zero external HTTP requests.
- **Expected Results:**
  - Entire dashboard renders in < 150 ms.
  - Zero 404 errors or failed external CDN requests.
  - Live RTT SVG graph animates smoothly at 1 Hz update rate.
- **Actual Results:** *Pending hardware bench execution.*
- **Verdict:** `NOT_RUN`

---

### TC-009: RESTful Telemetry & Control API JSON Schema Validation
- **Requirement Reference:** RN-009
- **Category:** API & Protocols
- **Initial Status:** `NOT_RUN`
- **Pre-conditions:** REST endpoints active.
- **Test Procedure:**
  1. Execute `GET /api/status`, `GET /api/telemetry`, and `GET /api/incidents`.
  2. Validate each response against standardized JSON Schema using an automated validator.
  3. Send `POST /api/config` with payload `{"probe_interval_ms": 3000}`.
  4. Verify probe interval shifts to 3000 ms.
- **Expected Results:**
  - Responses parse cleanly as valid RFC 8259 JSON.
  - All expected keys (`device`, `state`, `uptime_sec`, `metrics`, `incidents`) are present with correct data types.
  - Configuration POST successfully updates runtime probe timing.
- **Actual Results:** *Pending hardware bench execution.*
- **Verdict:** `NOT_RUN`

---

### TC-010: Out-of-Band Incident Diagnostics & Chunked CSV Export
- **Requirement Reference:** RN-010
- **Category:** Operations & Survivability
- **Initial Status:** `NOT_RUN`
- **Pre-conditions:** Target uplink router powered off completely (complete local network blackout); client connected to `ResilienceNode-XXXX`.
- **Test Procedure:**
  1. Confirm laptop has connectivity to `192.168.4.1`.
  2. Click the "Export CSV" button in dashboard or execute `curl -o dump.csv http://192.168.4.1/api/export`.
  3. Inspect downloaded CSV file structure and row integrity.
- **Expected Results:**
  - HTTP 200 response with `Content-Type: text/csv` and chunked transfer encoding.
  - CSV file contains complete header and all historical incident rows without truncated lines.
  - Memory usage during export remains stable due to streaming buffers.
- **Actual Results:** *Pending hardware bench execution.*
- **Verdict:** `NOT_RUN`

---

### TC-011: Resilient Reconnection Backoff & Task Watchdog Timer
- **Requirement Reference:** RN-011
- **Category:** Reliability & System
- **Initial Status:** `NOT_RUN`
- **Pre-conditions:** Station connected to test router.
- **Test Procedure:**
  1. Power off test router for 10 minutes.
  2. Monitor serial output to observe Wi-Fi reconnect retry delays.
  3. Verify FreeRTOS Task Watchdog Timer (TWDT) status.
  4. Power test router back on.
- **Expected Results:**
  - Station reconnect backoff delays escalate: 1s, 2s, 4s, 8s, capping at 30s.
  - System does not trigger an unexpected TWDT reset during disconnection.
  - Node automatically reconnects to router within 30 seconds of router reboot.
- **Actual Results:** *Pending hardware bench execution.*
- **Verdict:** `NOT_RUN`

---

### TC-012: Status LED Blink Cadence & Serial Telemetry Output
- **Requirement Reference:** RN-012
- **Category:** Hardware Diagnostics
- **Initial Status:** `NOT_RUN`
- **Pre-conditions:** ESP32 connected to PC; serial terminal attached.
- **Test Procedure:**
  1. Observe onboard LED (GPIO 2) during each state: `HEALTHY`, `DEGRADED`, `OUTAGE`, `DISCONNECTED`.
  2. Measure blink frequency and duty cycle with a photodiode or optical timing.
  3. Inspect UART serial log stream at 115200 baud for ANSI colored tags and 5s summary lines.
- **Expected Results:**
  - `HEALTHY`: Heartbeat pulse every 3s.
  - `DEGRADED`: 1 Hz steady blink.
  - `OUTAGE`: Rapid double-flash sequence.
  - `DISCONNECTED`: 4 Hz rapid flash.
  - Serial output emits clean ANSI log records with timestamp, state, and RTT summary.
- **Actual Results:** *Pending hardware bench execution.*
- **Verdict:** `NOT_RUN`

---

## 4. Test Sign-Off & Hardware Verification Protocol

| Role | Name | Signature / Approval | Date |
| :--- | :--- | :--- | :--- |
| **Lead Verification Engineer** | _______________________ | _______________________ | ____________ |
| **Firmware Architect** | _______________________ | _______________________ | ____________ |
| **QA / Release Manager** | _______________________ | _______________________ | ____________ |

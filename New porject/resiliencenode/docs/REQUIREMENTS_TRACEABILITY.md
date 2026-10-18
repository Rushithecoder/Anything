# 🔗 Requirements Traceability Matrix (RTM)

## Project: ResilienceNode
**Autonomous Edge Network Monitoring Black Box**  
**Document Version:** 1.0.0  
**Status:** Verification Baseline  

---

## 1. Traceability Overview

This document provides end-to-end bidirectional traceability linking every product requirement defined in [`docs/PRODUCT_REQUIREMENTS.md`](file:///c:/Rushabh@IMPORTANT/New%20porject/resiliencenode/docs/PRODUCT_REQUIREMENTS.md) to its implementation source code, verification test case in [`docs/TEST_REPORT.md`](file:///c:/Rushabh@IMPORTANT/New%20porject/resiliencenode/docs/TEST_REPORT.md), and physical acceptance evidence.

```
+---------------------------------------------------------------------------------------------------+
|                                 TRACEABILITY VALUE CHAIN                                          |
|                                                                                                   |
|  [PRD Requirement]  ───────>  [Source Implementation]  ───────>  [Test Case]  ───────>  [Evidence]|
|   (RN-001..RN-012)             (C Modules / Headers)              (TC-001..12)         (Logs/UI)  |
+---------------------------------------------------------------------------------------------------+
```

---

## 2. Requirements Traceability Matrix

| Req ID | Requirement Title | Implementation Files | Specific Functions / Components | Test Case ID | Verification Method | Acceptance Evidence |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **RN-001** | Dual-Mode Wi-Fi (AP + STA) | `main/wifi_manager.c`<br>`main/wifi_manager.h` | `wifi_manager_init()`<br>`wifi_event_handler()`<br>`wifi_init_softap()` | **TC-001** | Functional / RF Scan | AP SSID `ResilienceNode-XXXX` broadcasting; STA connected concurrently; DHCP leases assigned. |
| **RN-002** | Multi-Target Active Probing | `main/network_monitor.c`<br>`main/network_monitor.h` | `net_monitor_task()`<br>`icmp_ping_target()`<br>`probe_execute_cycle()` | **TC-002** | Protocol / Wireshark | Concurrent ICMP packets dispatched to Gateway, 8.8.8.8, 1.1.1.1 with timeout enforcement. |
| **RN-003** | Real-Time Telemetry & Jitter | `main/telemetry.c`<br>`main/telemetry.h` | `telemetry_record_sample()`<br>`telemetry_compute_jitter()`<br>`telemetry_get_window()` | **TC-003** | Mathematical / Unit | 60-slot circular buffer maintains Min/Avg/Max RTT, RFC 3550 Jitter, and Packet Loss %. |
| **RN-004** | Incident Classification Engine | `main/incident_engine.c`<br>`main/incident_engine.h` | `incident_engine_evaluate()`<br>`incident_fsm_transition()` | **TC-004** | State Injection / Fault | FSM accurately transitions between HEALTHY, DEGRADED, OUTAGE, DISCONNECTED, RECOVERING. |
| **RN-005** | Outage & Event Auto-Logging | `main/incident_engine.c`<br>`main/incident_engine.h` | `incident_open_record()`<br>`incident_close_record()` | **TC-005** | Functional / Log Audit | Incident records created with timestamp, trigger cause, peak latency, and computed downtime. |
| **RN-006** | Persistent Flash Store (NVS)| `main/event_store.c`<br>`main/event_store.h` | `event_store_save_incident()`<br>`event_store_get_all()`<br>`event_store_init()` | **TC-006** | Power-Cut / Reboot Test | Incidents committed to NVS flash; records survive hard power cycle without corruption. |
| **RN-007** | Embedded HTTP Web Server | `main/web_server.c`<br>`main/web_server.h` | `web_server_start()`<br>`http_uri_handler()`<br>`send_chunked_response()` | **TC-007** | Web / HTTP Concurrency | Port 80 HTTP server handles concurrent requests on both 192.168.4.1 and Station IP. |
| **RN-008** | Self-Hosted Single Page UI | `main/ui/index.html`<br>`main/web_server.c` | Embedded HTML/CSS/JS ROM image<br>`static_asset_handler()` | **TC-008** | Browser / UI Audit | Zero-dependency dark-mode dashboard renders in <100ms; auto-refreshes live SVG charts at 1Hz. |
| **RN-009** | RESTful Telemetry & API | `main/web_server.c`<br>`main/telemetry.c` | `api_status_handler()`<br>`api_telemetry_handler()`<br>`api_incidents_handler()` | **TC-009** | REST / JSON Validation | Valid JSON responses returned for `/api/status`, `/api/telemetry`, `/api/incidents`, `/api/config`. |
| **RN-010** | Out-of-Band Diagnostics | `main/web_server.c`<br>`main/event_store.c` | `api_export_handler()`<br>`export_csv_stream()` | **TC-010** | Disconnection / Export | Full dashboard and CSV diagnostic export accessible over SoftAP during upstream blackout. |
| **RN-011** | Auto-Reconnect & Watchdog | `main/wifi_manager.c`<br>`main/main.c` | `wifi_reconnect_backoff()`<br>`task_watchdog_init()`<br>`heap_monitor_task()` | **TC-011** | Stress / Watchdog Tripping| Exponential backoff reconnects upon AP revival; TWDT pets verify zero task deadlock; heap stable. |
| **RN-012** | LED Signaling & Serial Stream | `main/main.c`<br>`main/incident_engine.c` | `led_diagnostics_task()`<br>`serial_telemetry_stream()` | **TC-012** | Hardware / Logic Analyzer | Distinct LED blink cadences mapped to FSM states; ANSI serial logs streamed at 115200 baud. |

---

## 3. Subsystem Implementation Mapping

```
+---------------------------------------------------------------------------------------+
| MODULE DIRECTORY MAP                                                                  |
+---------------------------------------------------------------------------------------+
| File Path                     | Primary Responsibility           | Implemented Reqs   |
+-------------------------------+----------------------------------+--------------------+
| main/main.c                   | Boot, Supervision, LED, Watchdog | RN-011, RN-012     |
| main/wifi_manager.c           | AP+STA Dual Wi-Fi & Reconnection | RN-001, RN-011     |
| main/network_monitor.c        | ICMP Ping Probing Engine         | RN-002             |
| main/telemetry.c              | Rolling Stats, Jitter, Loss      | RN-003, RN-009     |
| main/incident_engine.c        | FSM State Classification & Logs  | RN-004, RN-005     |
| main/event_store.c            | Flash NVS Ring Buffer Persistence| RN-006, RN-010     |
| main/web_server.c             | Embedded HTTP Daemon & REST API  | RN-007, RN-009, 010|
| main/ui/index.html            | Embedded Dark Single-Page UI     | RN-008             |
+---------------------------------------------------------------------------------------+
```

---

## 4. Test Category Coverage Breakdown

```
+-------------------------+----------------------+--------------------------------------+
| Test Category           | Associated Test IDs  | Scope & Methodology                  |
+-------------------------+----------------------+--------------------------------------+
| Connectivity Tests      | TC-001, TC-011       | Wi-Fi association, AP isolation,     |
|                         |                      | exponential backoff recovery.        |
+-------------------------+----------------------+--------------------------------------+
| Telemetry & Metrics     | TC-002, TC-003       | Ping cadence, latency variance,      |
|                         |                      | RFC 3550 jitter accuracy.            |
+-------------------------+----------------------+--------------------------------------+
| Intelligence & Engine   | TC-004, TC-005       | Outage classification, hysteresis,   |
|                         |                      | active incident lifecycle.           |
+-------------------------+----------------------+--------------------------------------+
| Hardware & Storage      | TC-006, TC-012       | Flash wear-leveling, power cuts,     |
|                         |                      | LED blink cadence, UART streaming.   |
+-------------------------+----------------------+--------------------------------------+
| Web, UI & Export        | TC-007, TC-008,      | HTTP port 80 daemon, offline SPA UI, |
|                         | TC-009, TC-010       | REST endpoints, chunked CSV export.  |
+-------------------------+----------------------+--------------------------------------+
```

---

## 5. Verification Sign-Off Criteria

A requirement is deemed **Verified** when:
1. The corresponding test case execution in [`docs/TEST_REPORT.md`](file:///c:/Rushabh@IMPORTANT/New%20porject/resiliencenode/docs/TEST_REPORT.md) produces a `PASS` status.
2. Oscilloscope / logic analyzer or automated test harnesses validate timing tolerances (e.g. jitter ≤ 1ms error, boot time ≤ 2000ms).
3. The acceptance evidence has been recorded with build artifacts and serial test captures.

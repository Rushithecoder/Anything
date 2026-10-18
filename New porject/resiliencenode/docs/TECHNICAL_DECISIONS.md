# ⚖️ Architectural Technical Decisions (ADR)

## Project: ResilienceNode
**Autonomous Edge Network Monitoring Black Box**  
**Document Version:** 1.0.0  
**Format:** Architectural Decision Record (ADR)  

---

## Index of Architectural Decisions

- [ADR-001: Concurrent AP + STA Wi-Fi Mode vs. BLE / AP-Only Fallback](#adr-001-concurrent-ap--sta-wi-fi-mode-vs-ble--ap-only-fallback)
- [ADR-002: Real-Time HTTP Polling vs. WebSockets / Server-Sent Events (SSE)](#adr-002-real-time-http-polling-vs-websockets--server-sent-events-sse)
- [ADR-003: ESP-IDF Non-Volatile Storage (NVS) vs. LittleFS / SPIFFS Filesystem](#adr-003-esp-idf-non-volatile-storage-nvs-vs-littlefs--spiffs-filesystem)
- [ADR-004: Raw lwIP ICMP Sockets vs. High-Level Application HTTP Probing](#adr-004-raw-lwip-icmp-sockets-vs-high-level-application-http-probing)
- [ADR-005: 5-State FSM with Hysteresis vs. Instantaneous Boolean Alerting](#adr-005-5-state-fsm-with-hysteresis-vs-instantaneous-boolean-alerting)
- [ADR-006: Embedded Single-Page Application (ROM) vs. Cloud/CDN-Hosted Dashboard](#adr-006-embedded-single-page-application-rom-vs-cloudcdn-hosted-dashboard)
- [ADR-007: FreeRTOS Symmetric Multiprocessing & Core Affinity Allocation](#adr-007-freertos-symmetric-multiprocessing--core-affinity-allocation)
- [ADR-008: Exponential Backoff Reconnection & Task Watchdog Integration](#adr-008-exponential-backoff-reconnection--task-watchdog-integration)

---

### ADR-001: Concurrent AP + STA Wi-Fi Mode vs. BLE / AP-Only Fallback

- **Status:** Accepted
- **Context:**  
  ResilienceNode must continuously monitor a target Wi-Fi infrastructure (as a client/STA) while providing an out-of-band management interface during network outages. Traditional IoT devices either switch between AP and STA sequentially (captive portal pattern) or utilize Bluetooth Low Energy (BLE) for local provisioning and diagnostics.
- **Decision:**  
  Operate the ESP32 Wi-Fi subsystem in simultaneous concurrent **Access Point + Station (`WIFI_MODE_APSTA`)** mode at all times. The SoftAP (`ResilienceNode-XXXX`, IP `192.168.4.1`) remains permanently active and completely isolated from the Station interface state.
- **Alternatives Considered:**
  1. *AP-Only Fallback (Captive Portal):* Only broadcast SoftAP when Station disconnects.  
     *Rejected:* If the router is up but internet is down (an ISP outage), the Station remains connected, preventing technicians from accessing local diagnostics.
  2. *Bluetooth Low Energy (BLE) Companion App:*  
     *Rejected:* Requires field technicians to install a specialized proprietary mobile app. SoftAP enables immediate access from any device with a standard web browser without software installation.
- **Consequences:**  
  - *Positive:* 100% out-of-band diagnostic availability; zero mobile app dependencies; immediate browser access.
  - *Negative:* Increases radio power consumption slightly (~25 mA); both interfaces share the same physical 2.4 GHz RF channel.

---

### ADR-002: Real-Time HTTP Polling vs. WebSockets / Server-Sent Events (SSE)

- **Status:** Accepted
- **Context:**  
  The embedded dashboard UI requires sub-second live updates for packet latency charts, probe indicators, and system state badges. We must balance transport efficiency with memory consumption on the resource-constrained ESP32.
- **Decision:**  
  Utilize lightweight **1 Hz asynchronous HTTP REST polling** (`GET /api/telemetry` and `GET /api/status`) with HTTP Keep-Alive.
- **Alternatives Considered:**
  1. *WebSockets (`ws://`):*  
     *Rejected:* A persistent full-duplex WebSocket connection consumes a dedicated TCP socket, a large frame buffer (4–8 KB SRAM per client), and significant background task stack space in ESP-IDF. Multiple concurrent browser tabs could quickly exhaust internal heap.
  2. *Server-Sent Events (SSE):*  
     *Rejected:* Keeps long-lived HTTP connections open, tying up server worker threads in `esp_http_server`.
- **Consequences:**  
  - *Positive:* Stateless HTTP requests allow multiple clients to share worker threads; zero memory leaks from unclosed socket sessions; trivial caching and REST proxying.
  - *Negative:* Header overhead per poll (approx. 200 bytes per request), easily absorbed by local 802.11n bandwidth.

---

### ADR-003: ESP-IDF Non-Volatile Storage (NVS) vs. LittleFS / SPIFFS Filesystem

- **Status:** Accepted
- **Context:**  
  The black-box event store must record up to 100 incident records, operational counters (reboots, downtime), and configuration settings. Crucially, writes must survive sudden power loss without filesystem corruption or flash wear-out.
- **Decision:**  
  Utilize native **ESP-IDF NVS (Non-Volatile Storage)** with structured binary blobs and circular index tracking within the `rn_events` namespace.
- **Alternatives Considered:**
  1. *SPIFFS / LittleFS Filesystem:*  
     *Rejected:* Filesystems introduce file table descriptor overhead, larger RAM cache buffers (~16 KB), and risk file corruption or dangling file handles during mid-write power loss.
  2. *Raw Flash Partition (Custom Ring Buffer):*  
     *Rejected:* Requires writing custom wear-leveling and sector erase routines from scratch, introducing risk of edge-case bugs.
- **Consequences:**  
  - *Positive:* Built-in hardware wear-leveling across 24 KB flash partition; atomic key-value writes prevent half-written records; minimal RAM footprint (<1 KB).
  - *Negative:* Value size limited to 4000 bytes per key, which is more than sufficient for compact binary incident records (64 bytes each).

---

### ADR-004: Raw lwIP ICMP Sockets vs. High-Level Application HTTP Probing

- **Status:** Accepted
- **Context:**  
  Active monitoring requires precise latency measurements and accurate fault localization across the network topology (gateway, DNS, internet).
- **Decision:**  
  Implement the probe engine using **raw lwIP ICMP sockets** (`IPPROTO_ICMP`) combined with non-blocking select timeouts.
- **Alternatives Considered:**
  1. *High-Level HTTP GET (`esp_http_client`):*  
     *Rejected:* HTTP handshakes introduce TCP 3-way handshake delays, TLS encryption overhead (which consumes >35 KB RAM and massive CPU cycles), and HTTP server latency, obscuring true network transport latency.
  2. *DNS Lookup Probing (`getaddrinfo`):*  
     *Rejected:* OS-level DNS caches mask transient packet loss and fail to provide sub-millisecond round-trip timing.
- **Consequences:**  
  - *Positive:* Direct layer-3 transport measurement; sub-millisecond RTT precision; zero TLS/HTTP CPU overhead; direct ICMP error code visibility (Host Unreachable, Network Unreachable).
  - *Negative:* Raw sockets require careful checksum calculations and IP header handling in C.

---

### ADR-005: 5-State FSM with Hysteresis vs. Instantaneous Boolean Alerting

- **Status:** Accepted
- **Context:**  
  Wireless edge environments frequently experience transient RF blips, single packet drops, or microwave interference. Alerting on a single dropped ping produces intolerable alert fatigue and misleading incident logs.
- **Decision:**  
  Implement a deterministic **5-State Finite State Machine (`HEALTHY`, `DEGRADED`, `OUTAGE`, `DISCONNECTED`, `RECOVERING`)** governed by strict consecutive failure/success hysteresis thresholds.
- **Threshold Criteria:**
  - Transition to `OUTAGE`: Requires **3 consecutive** failed probe cycles (6 seconds).
  - Transition to `HEALTHY` from failure: Requires **5 consecutive** successful probe cycles (10 seconds) in the `RECOVERING` state.
  - Transition to `DEGRADED`: Triggered when rolling window packet loss is between 5% and 50% or RTT exceeds 150 ms.
- **Consequences:**  
  - *Positive:* Completely eliminates false positives caused by momentary RF interference; accurately isolates true link drops from chronic line degradation.
  - *Negative:* Introduces an intentional 6-second detection delay before declaring total outage, which is standard in enterprise observability.

---

### ADR-006: Embedded Single-Page Application (ROM) vs. Cloud/CDN-Hosted Dashboard

- **Status:** Accepted
- **Context:**  
  The dashboard must render reliably on field technicians' devices during a complete internet outage where external CDNs (like Bootstrap, Google Fonts, or Chart.js) are unreachable.
- **Decision:**  
  Embed a fully **self-contained Single Page Application (SPA)** written in pure HTML5, vanilla modern CSS3, and ES6 JavaScript directly into ESP32 flash ROM via CMake `EMBED_FILES`.
- **Alternatives Considered:**
  1. *External CDN Assets:*  
     *Rejected:* If the uplink internet connection is severed, modern browsers attempting to load CDN libraries will hang or fail, rendering a blank or broken dashboard during the very outage it is meant to diagnose!
  2. *Cloud SaaS Dashboard (MQTT/HTTP Forwarding):*  
     *Rejected:* Defeats the primary purpose of an autonomous edge black box.
- **Consequences:**  
  - *Positive:* 100% offline survivability; loads in <150 ms; zero external attack surface; guaranteed backwards compatibility.
  - *Negative:* Total uncompressed asset size must be kept under 40 KB to conserve flash ROM space.

---

### ADR-007: FreeRTOS Symmetric Multiprocessing & Core Affinity Allocation

- **Status:** Accepted
- **Context:**  
  The ESP32 features two 240 MHz Xtensa cores (Core 0 and Core 1). Network stack operations (lwIP, Wi-Fi ISRs) can be CPU-intensive and could starve real-time probe measurements or HTTP dashboard servicing if not carefully scheduled.
- **Decision:**  
  Pin low-level Wi-Fi and TCP/IP stack tasks to **Core 0** (system default), and pin all application tasks (`net_monitor_task`, `incident_task`, `httpd_server`, `led_task`) to **Core 1**.
- **Consequences:**  
  - *Positive:* Prevents Wi-Fi RF interrupts from disrupting ICMP microsecond timing; eliminates thread contention on the lwIP core; maximizes HTTP throughput.
  - *Negative:* Inter-task communication between cores requires proper FreeRTOS queue and mutex synchronization.

---

### ADR-008: Exponential Backoff Reconnection & Task Watchdog Integration

- **Status:** Accepted
- **Context:**  
  If the target uplink Wi-Fi router reboots, suffers power interruption, or is replaced, the node must not thrash the RF subsystem with infinite rapid reconnection attempts or freeze due to task lockups.
- **Decision:**  
  Implement an **exponential backoff reconnection algorithm** (1s $\rightarrow$ 2s $\rightarrow$ 4s $\rightarrow$ 8s $\rightarrow$ capped at 30s) paired with the ESP-IDF **Task Watchdog Timer (TWDT)** configured to a 10-second timeout.
- **Consequences:**  
  - *Positive:* Prevents thermal build-up and RF channel saturation during prolonged router outages; automatically self-heals in case of deadlocks or heap starvation.
  - *Negative:* Maximum reconnection latency after router reboot is 30 seconds once maximum backoff interval is reached.

# 🎬 Live Demonstration & Evaluation Guide

## Project: ResilienceNode
**Autonomous Edge Network Monitoring Black Box**  
**Document Version:** 1.0.0  
**Target Audience:** Hackathon Judges, Reliability Engineers, Evaluation Panels  

---

## 🎯 Executive Overview of the Demo

This guide provides an end-to-end operational walkthrough demonstrating how ResilienceNode operates during live network failures. In this demonstration, you will prove that ResilienceNode solves the **"Blind Outage Paradox"**: when an enterprise router loses upstream internet, traditional cloud monitoring tools (Datadog, Pingdom, Uptime Robot) lose contact and cannot tell engineers what failed. ResilienceNode maintains an independent out-of-band Wi-Fi connection, pinpoints the failure root cause in real time, logs the incident into non-volatile flash, and remains 100% operable.

---

## 🛠️ Demonstration Setup & Prerequisites

```
+---------------------------------------------------------------------------------+
|                               DEMO BENCH SETUP                                  |
|                                                                                 |
|  [Demo Router / Mobile Hotspot]             [ESP32 ResilienceNode]              |
|   SSID: "DemoNetwork"                        Plugged into 5V USB               |
|   Connected to Internet                      Running ResilienceNode Firmware   |
|            ▲                                         ▲                          |
|            │ (STA Mode)                              │ (AP Mode)                |
|            └─────────────────────────────────────────┼──────────┐               |
|                                                                 │               |
|                                                       [Evaluator Laptop/Phone]  |
|                                                        Connected to:            |
|                                                        "ResilienceNode-XXXX"    |
|                                                        Browser: 192.168.4.1     |
+---------------------------------------------------------------------------------+
```

### Required Hardware & Environment
1. **ResilienceNode Device:** ESP32 development board (flashed with latest firmware) connected to a USB power port.
2. **Evaluator Device:** Laptop, tablet, or smartphone with Wi-Fi and a modern web browser (Chrome, Safari, Firefox, Edge).
3. **Target Monitored Network:** A Wi-Fi Access Point (e.g., standard Wi-Fi router or smartphone configured as "DemoNetwork" with active internet).
4. **Failure Trigger Tool:** Access to toggle router WAN cable, router internet toggle, or phone cellular data toggle.

---

## 📋 The 5 Live Demonstration Scenarios

```
+-----------+-----------------------------------+-----------------------------------+
| Scenario  | Test Condition                    | Key Takeaway / Evaluator Proof    |
+-----------+-----------------------------------+-----------------------------------+
| 1. Boot   | Cold power application            | Instant AP broadcast in <2s; zero |
|           |                                   | cloud or internet setup needed.   |
+-----------+-----------------------------------+-----------------------------------+
| 2. Connect| STA associates with uplink WLAN   | Real-time 1Hz telemetry, multi-   |
|           |                                   | target latency matrix, HEALTHY.   |
+-----------+-----------------------------------+-----------------------------------+
| 3. Outage | WAN cable severed / data disabled | Blind Outage solved: Local AP     |
|           |                                   | stays alive; OUTAGE detected.     |
+-----------+-----------------------------------+-----------------------------------+
| 4. Persist| Black-box NVS flash commit & power| Hard reboot performed; incident   |
|           | cycle verification                | log survives across power loss.   |
+-----------+-----------------------------------+-----------------------------------+
| 5. Recover| WAN link restored & report export | Auto-recovery through RECOVERING; |
|           |                                   | one-click CSV diagnostic export.  |
+-----------+-----------------------------------+-----------------------------------+
```

---

### Scenario 1: Autonomous Boot & Out-of-Band Discovery

**Objective:** Demonstrate that the node boots instantaneously, requires zero cloud configuration, and provides immediate local management.

#### Procedure:
1. Plug the ResilienceNode into USB power.
2. Observe the onboard LED (GPIO 2) — it flashes rapidly during peripheral initialization and then enters a steady heartbeat pulse.
3. Open the Wi-Fi settings on your evaluator laptop or smartphone.
4. Scan for Wi-Fi networks: Look for `ResilienceNode-XXXX` *(where XXXX is the unique 4-character MAC suffix)*.
5. Connect using the network credentials:
   - **SSID:** `ResilienceNode-XXXX`
   - **Password:** `resilience2024`
6. Open your web browser and navigate to:
   ```
   http://192.168.4.1
   ```

#### What Evaluators See:
- The ResilienceNode dark-mode diagnostic dashboard loads immediately (< 100 ms).
- The system state badge indicates `DISCONNECTED` or `INITIALIZING` (as uplink Wi-Fi has not yet been linked).
- System vitals show: Uptime counting upward, Memory Free (~195 KB), and SoftAP Client Count = 1.

> **Presenter Talk Track:**  
> *"Notice that right out of the box, with zero cloud pairing, zero mobile apps, and zero internet connection, ResilienceNode is fully functional. Its embedded web engine is served directly out of internal flash ROM."*

---

### Scenario 2: Live Uplink Baseline Telemetry

**Objective:** Connect the node to the target facility Wi-Fi and observe real-time health telemetry across the network hierarchy.

#### Procedure:
1. On the dashboard at `http://192.168.4.1`, click the **Settings / Config** icon (or scroll to the configuration card).
2. Enter the target network credentials:
   - **Target SSID:** `DemoNetwork`
   - **Password:** `YourPassword123`
3. Click **Save & Connect**.
4. Within 3 seconds, observe the Station IP field update with a leased DHCP address (e.g., `192.168.1.145`).
5. Observe the Status Badge transition through `RECOVERING` $\rightarrow$ `HEALTHY` (glowing emerald green).
6. Observe the live 1 Hz telemetry cards:
   - **Round Trip Time (RTT):** Real-time latency graph rendering consecutive probe pings (12–25 ms).
   - **Jitter:** RFC 3550 statistical variance (typically 2–5 ms).
   - **Packet Loss:** Displays `0.0%`.
   - **Multi-Target Probe Matrix:** Gateway (`192.168.1.1`), DNS 1 (`8.8.8.8`), and DNS 2 (`1.1.1.1`) all show green checkmarks.

> **Presenter Talk Track:**  
> *"ResilienceNode isn't just checking if Wi-Fi is linked. It concurrently pings the local gateway, Google DNS, and Cloudflare DNS every two seconds. The graph you see is streaming live from the node's internal ring buffer."*

---

### Scenario 3: Upstream WAN Disruption ("The Blind Outage Paradox")

**Objective:** Simulate an ISP or modem failure while the local router remains powered on. Prove that traditional monitoring fails, while ResilienceNode accurately isolates the fault.

#### Procedure:
1. Simulate WAN failure:
   - *Option A:* Disconnect the Ethernet/WAN cable from the demo router.
   - *Option B:* Turn off cellular mobile data on the hotspot phone (while keeping the Wi-Fi hotspot broadcast active).
2. Look at the ResilienceNode dashboard at `http://192.168.4.1` on your laptop:
   - Notice that your laptop has **NOT** lost connection to the dashboard, because you are connected via the out-of-band SoftAP!
3. Watch the probe matrix update:
   - **Gateway (`192.168.1.1`):** `REACHABLE` (Local Wi-Fi and router are functioning!).
   - **DNS 1 (`8.8.8.8`):** `FAILED / TIMEOUT`.
   - **DNS 2 (`1.1.1.1`):** `FAILED / TIMEOUT`.
4. After 3 consecutive failed cycles (6 seconds), the Incident Engine FSM triggers:
   - Status Badge switches to **`OUTAGE`** (vivid crimson red).
   - An active incident banner appears: *"Active Incident #1: WAN_DNS_TIMEOUT — Elapsed: 8s"*.
   - Onboard LED begins rapid double-flash sequence.

> **Presenter Talk Track:**  
> *"This is the core value proposition. If you were using Datadog or Uptime Robot, your dashboard would simply show 'Device Offline'—you wouldn't know if the power failed, the router died, or the ISP dropped. ResilienceNode proves immediately: the local router is alive (Gateway responds in 2ms), but the upstream WAN has failed. And we never lost visibility."*

---

### Scenario 4: Black-Box Incident Logging & Reboot Survivability

**Objective:** Prove that ResilienceNode acts as an indestructible black box, persisting incident records into NVS flash memory across power outages.

#### Procedure:
1. While the incident is recorded, physically unplug the ResilienceNode USB power cable.
2. Wait 5 seconds (simulating total power failure during an incident).
3. Re-insert the USB cable to power the device back on.
4. Reconnect evaluator laptop to `ResilienceNode-XXXX` and refresh `http://192.168.4.1`.
5. Scroll down to the **Historical Incidents Log** table.
6. Verify that Incident #1 is clearly listed:
   - **ID:** 1
   - **Type:** `OUTAGE`
   - **Trigger:** `WAN_DNS_TIMEOUT`
   - **Packet Loss:** `100%`
   - **Timestamp / Duration:** Intact!

> **Presenter Talk Track:**  
> *"In industrial environments, outages are often accompanied by power surges or forced breaker resets. ResilienceNode's event store commits incidents into non-volatile flash using wear-leveling atomic sector writes. Even after sudden power removal, the audit trail is preserved."*

---

### Scenario 5: Autonomous Link Recovery & Diagnostic Export

**Objective:** Demonstrate autonomous link recovery and one-click compliance export for post-mortem analysis.

#### Procedure:
1. Restore internet connectivity (reconnect the WAN cable or turn cellular data back on).
2. Watch the dashboard:
   - Within 2 seconds, the first DNS probe succeeds.
   - Status Badge enters **`RECOVERING`** (yellow-amber).
   - A recovery counter counts down: *"Validating stability: Probe 1/5, 2/5, 3/5..."*
   - Once 5 consecutive probes succeed, the badge transitions to **`HEALTHY`** (emerald green).
   - The active incident is closed, and its total downtime duration is finalized.
3. Click the **Export CSV** button in the dashboard toolbar.
4. Your browser instantly downloads `resiliencenode_report.csv`.
5. Open the CSV file in Excel or text editor to inspect the full event schema:
   ```csv
   id,type,trigger,start_s,end_s,duration_s,loss_pct,peak_rtt_ms
   1,OUTAGE,WAN_DNS_TIMEOUT,124,198,74,100.0,0
   ```

> **Presenter Talk Track:**  
> *"When the connection stabilizes, ResilienceNode requires five consecutive clean probes before clearing the incident, preventing link-flapping false positives. The technician can then download the complete CSV audit log directly to their phone and email it to the ISP as proof of SLA violation."*

---

## 🎯 Evaluator Verification Checklist

- [ ] SoftAP broadcasts independently with unique MAC ID (`ResilienceNode-XXXX`).
- [ ] Embedded dashboard opens with zero internet or cloud connection at `192.168.4.1`.
- [ ] Real-time 1 Hz telemetry shows instantaneous RTT, Jitter, and Loss.
- [ ] Tiered probing differentiates between local LAN and upstream WAN failures.
- [ ] State machine enforces hysteresis during outage and recovery.
- [ ] Black-box logs survive power cycling without data corruption.
- [ ] Diagnostic export downloads cleanly in CSV format.

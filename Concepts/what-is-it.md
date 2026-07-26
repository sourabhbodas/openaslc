Here is the complete conceptual design specification exported in Markdown format, followed by a dedicated, step-by-step MVP setup task list.

---

# Soft PLC & Agentic Logic Controller (SLC) Specification

## System Overview

The **Agentic Soft PLC / SLC System** converts standard Single Board Computers (SBCs)—such as Raspberry Pi, BeagleBone, x86 IPCs, or ESP32/STM32 microcontrollers—into industrial-grade, deterministic logic controllers.

The system supports standalone deployment for MSMEs as well as distributed, multi-agent control across networked boards for educational and commercial automation.

---

## 1. System Architecture & Topology

The platform operates on a **Primary / Secondary (Leader / Worker)** topology:

```
                         ┌──────────────────────────────────────────────┐
                         │             Browser / Client UI              │
                         └──────────────────────┬───────────────────────┘
                                                │ Web Interface (HTTP/WS)
                                                ▼
                         ┌──────────────────────────────────────────────┐
                         │                 PRIMARY NODE                 │
                         │ - Hosts Central Web UI & Fleet Orchestrator  │
                         │ - Manages Global Topology & Logic Router     │
                         │ - Master Logger & Network Capture Aggregator │
                         │ - Local Soft PLC Runtime (Runs local IO)     │
                         └──────┬───────────────────────────────┬───────┘
                                │                               │
                   Internal Control Protocol             Broadcasting / State Sync
                   (REST / WebSockets API)               (UDP Multicast / ZeroMQ)
                                │                               │
            ┌───────────────────┴──────────────┐   ┌────────────┴─────────────────────┐
            ▼                                  ▼   ▼                                  ▼
┌───────────────────────┐          ┌───────────────────────┐          ┌───────────────────────┐
│   SECONDARY NODE 01   │          │   SECONDARY NODE 02   │          │   SECONDARY NODE 03   │
│ - Soft PLC Engine     │          │ - Soft PLC Engine     │          │ - Soft PLC Engine     │
│ - Telemetry Reporter  │          │ - Telemetry Reporter  │          │ - Telemetry Reporter  │
│ - Local Logging / Ext │          │ - Local Logging / Ext │          │ - Local Logging / Ext │
└───────────────────────┘          └───────────────────────┘          └───────────────────────┘

```

### Node Roles

* **Primary Node (Fleet Leader):**
* Serves the single-page visual Web UI.
* Hosts the central version-controlled database (`SQLite`).
* Orchestrates deployment of logic fragments to secondary nodes.
* Aggregates system metrics (CPU, RAM, storage, I/O states) across the fleet.


* **Secondary Nodes (Workers):**
* Execute assigned control flows deterministically using the local C++20 engine.
* Report telemetry and hardware descriptors (GPIO, ADC, system resources) on startup.
* Fall back to autonomous local operation if connection to the Primary Node is lost.



---

## 2. C++20 Core Execution Engine

### Deterministic Scan Cycle

Each node executes a real-time cycle (target: 10ms–20ms) using POSIX `SCHED_FIFO` real-time scheduling and locked memory pages (`mlockall`):

```
┌────────────────────────────────────────────────────────┐
│             Deterministic Scan Loop (10ms)             │
├────────────────────────────────────────────────────────┤
│ 1. Read Inputs   : Copy HAL hardware state -> %I      │
│ 2. Execute Logic : Process active Action Flows        │
│ 3. Write Outputs : Copy %Q -> HAL hardware driver     │
│ 4. Housekeeping  : Handle async WebSockets / Loggers   │
└────────────────────────────────────────────────────────┘

```

### Process Memory Map

Memory is explicitly structured into three distinct process images:

* **`%I` (Input Bank):** Snapshot of physical/remote digital and analog inputs.
* **`%Q` (Output Bank):** Process outputs ready to be committed to physical pins/relays.
* **`%M` (Internal Memory):** Flags, timers, counters, state variables, and retain memory.

---

## 3. Storage, Logging & Audit Trail

### Asynchronous Ring-Buffered Logging

To preserve real-time determinism during the scan loop, disk writes bypass the execution thread:

* Scan thread pushes event/log messages into a lock-free ring buffer in RAM.
* A background task (`std::jthread`) flushes the queue to physical disk asynchronously.
* Automatically routes high-volume logs to mounted external storage (e.g., `/mnt/ext_sd_card` or USB drives) to reduce eMMC/flash wear.

### Version-Controlled Configuration Store (`SQLite 3`)

All configuration states are managed via an embedded Git-like version tree inside `config_archive.db` on the Primary Node:

* **`commits` Table:** Stores commit ID (SHA-256), author, timestamp, parent pointer, and commit message.
* **`snapshots` Table:** Holds the complete JSON configuration per node for a given commit.
* **`deployments` Table:** Tracks active deployed states with automated rollback capability upon hardware failure detection.

---

## 4. Hardware Abstraction Layer (HAL) & Networking

* **Local Driver Interface:** Abbreviated standard wrapper around `libgpiod` (v2) for Linux GPIOs, I2C/SPI expansion controllers, and PWM channels.
* **Fieldbus Protocol Support:** Native integration of Modbus TCP / Modbus RTU (RS485) for commercial industrial I/O expansion modules.
* **Inter-Node Bus:** Lightweight real-time pub/sub messaging (ZeroMQ / MQTT Sparkplug B) for cross-SBC signal wiring.

---

## 5. Web UI & Deployment Paradigm

* **Zero-Install Web Server:** C++ engine hosts the web frontend directly using an embedded server (`Crow` / `httplib`).
* **Visual Drag-and-Drop Canvas:** Browser client renders node diagrams (using libraries like Rete.js or React Flow), converting visual graphs into JSON logic payloads sent via REST endpoints.
* **Single-Container Deployment:** Packaged into a single OCI Docker container for zero-friction MSME installation and classroom deployment.

---


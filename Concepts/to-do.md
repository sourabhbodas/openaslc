---

# To-Do Task List: Simple Mock & MVP Roadmap

This checklist guides the construction of a minimal, working proof-of-concept (MVP) running on a single Linux board (or local Linux VM) using C++20 and a local web interface.

---

## Phase 1: Minimal C++20 Real-Time Execution Core

* [x] **1.1 Setup Project Skeleton**
* [x] Initialize CMake project targeting C++20 (`-std=c++20`).
* [x] Configure dependencies (`pthread`, `Catch2` for testing).


* [x] **1.2 Build Process Image Memory Map**
* [x] Implement `MemoryMap` struct with fixed byte arrays (`%I` [512B], `%Q` [512B], `%M` [1024B]).
* [x] Add thread-safe atomic access methods or double-buffering mechanisms for read/write isolation.


* [x] **1.3 Build Real-Time Scan Loop Driver**
* [x] Create `AslcEngine` class leveraging `std::jthread` and `std::stop_token`.
* [x] Implement `SCHED_FIFO` real-time thread priority setup and `mlockall()` memory locking.
* [x] Build cyclic timing loop using `std::chrono::steady_clock` enforcing a fixed 20ms period.


* [x] **1.4 Implement Mock HAL Driver**
* [x] Create `IIODriver` virtual C++ interface (`initialize()`, `read_inputs()`, `write_outputs()`).
* [x] Build `MockDriver` that toggles virtual inputs in memory (`%I`) and prints output changes (`%Q`) to the console.


* [x] **1.5 Build & Test Automation Script**
* [x] Create `build_and_test.py` Python script to configure, build, and run CTest test suite.



---

## Phase 2: Logic Interpreter & Basic Action Flows

* [x] **2.1 Define AST / JSON Logic Format**
* [x] Draft a minimal JSON schema representing simple logic blocks (e.g., `AND`, `OR`, `NOT`, `TON_TIMER`).


* [x] **2.2 Implement Basic Bytecode / AST Interpreter**
* [x] Build an execution unit in C++ that parses the JSON schema and maps inputs to outputs inside `%M` / `%Q`.
* [x] Test simple logic evaluation: `If (%I[0] AND %I[1]) -> Set %Q[0] = 1`.


* [x] **2.3 Enable Hot-Reload Mechanism**
* [x] Add a thread-safe configuration swap mechanism so user logic can be updated between scan cycles without stopping the real-time loop.



---

## Phase 3: Embedded Web Server & Live Canvas UI

* [x] **3.1 Integrate Embedded C++ Web Server**
* [x] Import header-only HTTP library (`httplib.h` or `Crow`).
* [x] Set up static file hosting route (`/www` folder) to serve web assets.
* [x] Create REST API endpoint `POST /api/deploy` to receive updated JSON logic configs.
* [x] Set up WebSocket endpoint `WS /ws/telemetry` to stream real-time `%I` and `%Q` byte states to the client at 10Hz.


* [x] **3.2 Build MVP Web Interface Frontend**
* [x] Create a lightweight single-page HTML/JS application (using Rete.js, React Flow, or basic HTML5 Canvas).
* [x] Render basic I/O nodes and logic blocks (`Input Pin`, `Output Pin`, `AND Gate`).
* [x] Connect WebSocket feed to visually light up active signal wires (green for active signal, grey for off).



---

## Phase 4: Local Storage, SQLite Archiving & Logging

* [x] **4.1 Implement Asynchronous Ring-Buffer Logger**
* [x] Build a lock-free ring buffer queue for log events.
* [x] Implement a background thread that pops logs from the queue and writes them to a rotating local file (`/tmp/softplc.log`).


* [x] **4.2 Integrate SQLite Version Control Archive**
* [x] Integrate SQLite 3 database dependency (`SQLiteCpp` or native `sqlite3`).
* [x] Create database schema initialization script (`commits`, `snapshots`, `deployments` tables).
* [x] Implement C++ methods for `commit_config(author, message, json_payload)` and `get_commit_history()`.
* [x] Wire UI button **"Save & Deploy"** to create a new database commit before swapping runtime logic.



---

## Phase 5: Hardware Integration & Packaging

* [ ] **5.1 Integrate Linux `libgpiod` Driver**
* [ ] Implement `LinuxGpiodDriver` inheriting from `IIODriver` to read and write physical GPIO pins on a Raspberry Pi or similar board.


* [ ] **5.2 Create Native Executable Build**
* [ ] Statically link the C++20 engine (and its dependencies — `libgpiod`, web server/UI assets) into a single self-contained binary per target architecture (e.g. `armv7`, `aarch64`, `x86_64`), so it runs directly on Ubuntu/embedded Linux with no container runtime or package installation step.
* [ ] Build a lightweight installer script/bootstrap step that drops the binary, registers the node in the hive (derives/confirms its node UID, see `Concepts/private/hive_io_forwarding.md`), and installs it as a system service (e.g. `systemd` unit) so it starts on boot with hardware access (GPIO group membership / capabilities) already granted.


* [ ] **5.3 Create Docker Container Build (optional packaging path)**
* [ ] Write a `Dockerfile` compiling the C++20 engine and bundling web assets into a minimal alpine or debian-slim image, for environments (x86 IPCs, CI, classroom demos) where a container runtime is preferred over the native install.
* [ ] Create `docker-compose.yml` configured to grant `--privileged` host hardware GPIO access.


* [ ] **5.4 MVP End-to-End Test Run**
* [ ] Flash single board, install via native binary (or start container), access UI over local web browser, connect a button to Input 1 and an LED to Output 1, build logic on screen, click deploy, and verify physical operation.
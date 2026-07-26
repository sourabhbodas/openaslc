---

# To-Do Task List: Simple Mock & MVP Roadmap

This checklist guides the construction of a minimal, working proof-of-concept (MVP) running on a single Linux board (or local Linux VM) using C++20 and a local web interface.

---

## Phase 1: Minimal C++20 Real-Time Execution Core

* [ ] **1.1 Setup Project Skeleton**
* [ ] Initialize CMake project targeting C++20 (`-std=c++20`).
* [ ] Configure dependencies (`pthread`, `Catch2` for testing).


* [ ] **1.2 Build Process Image Memory Map**
* [ ] Implement `MemoryMap` struct with fixed byte arrays (`%I` [512B], `%Q` [512B], `%M` [1024B]).
* [ ] Add thread-safe atomic access methods or double-buffering mechanisms for read/write isolation.


* [ ] **1.3 Build Real-Time Scan Loop Driver**
* [ ] Create `SoftPlcEngine` class leveraging `std::jthread` and `std::stop_token`.
* [ ] Implement `SCHED_FIFO` real-time thread priority setup and `mlockall()` memory locking.
* [ ] Build cyclic timing loop using `std::chrono::steady_clock` enforcing a fixed 20ms period.


* [ ] **1.4 Implement Mock HAL Driver**
* [ ] Create `IIODriver` virtual C++ interface (`initialize()`, `read_inputs()`, `write_outputs()`).
* [ ] Build `MockDriver` that toggles virtual inputs in memory (`%I`) and prints output changes (`%Q`) to the console.



---

## Phase 2: Logic Interpreter & Basic Action Flows

* [ ] **2.1 Define AST / JSON Logic Format**
* [ ] Draft a minimal JSON schema representing simple logic blocks (e.g., `AND`, `OR`, `NOT`, `TON_TIMER`).


* [ ] **2.2 Implement Basic Bytecode / AST Interpreter**
* [ ] Build an execution unit in C++ that parses the JSON schema and maps inputs to outputs inside `%M` / `%Q`.
* [ ] Test simple logic evaluation: `If (%I[0] AND %I[1]) -> Set %Q[0] = 1`.


* [ ] **2.3 Enable Hot-Reload Mechanism**
* [ ] Add a thread-safe configuration swap mechanism so user logic can be updated between scan cycles without stopping the real-time loop.



---

## Phase 3: Embedded Web Server & Live Canvas UI

* [ ] **3.1 Integrate Embedded C++ Web Server**
* [ ] Import header-only HTTP library (`httplib.h` or `Crow`).
* [ ] Set up static file hosting route (`/www` folder) to serve web assets.
* [ ] Create REST API endpoint `POST /api/deploy` to receive updated JSON logic configs.
* [ ] Set up WebSocket endpoint `WS /ws/telemetry` to stream real-time `%I` and `%Q` byte states to the client at 10Hz.


* [ ] **3.2 Build MVP Web Interface Frontend**
* [ ] Create a lightweight single-page HTML/JS application (using Rete.js, React Flow, or basic HTML5 Canvas).
* [ ] Render basic I/O nodes and logic blocks (`Input Pin`, `Output Pin`, `AND Gate`).
* [ ] Connect WebSocket feed to visually light up active signal wires (green for active signal, grey for off).



---

## Phase 4: Local Storage, SQLite Archiving & Logging

* [ ] **4.1 Implement Asynchronous Ring-Buffer Logger**
* [ ] Build a lock-free ring buffer queue for log events.
* [ ] Implement a background thread that pops logs from the queue and writes them to a rotating local file (`/tmp/softplc.log`).


* [ ] **4.2 Integrate SQLite Version Control Archive**
* [ ] Integrate SQLite 3 database dependency (`SQLiteCpp` or native `sqlite3`).
* [ ] Create database schema initialization script (`commits`, `snapshots`, `deployments` tables).
* [ ] Implement C++ methods for `commit_config(author, message, json_payload)` and `get_commit_history()`.
* [ ] Wire UI button **"Save & Deploy"** to create a new database commit before swapping runtime logic.



---

## Phase 5: Hardware Integration & Docker Packaging

* [ ] **5.1 Integrate Linux `libgpiod` Driver**
* [ ] Implement `LinuxGpiodDriver` inheriting from `IIODriver` to read and write physical GPIO pins on a Raspberry Pi or similar board.


* [ ] **5.2 Create Docker Container Build**
* [ ] Write a `Dockerfile` compiling the C++20 engine and bundling web assets into a minimal alpine or debian-slim image.
* [ ] Create `docker-compose.yml` configured to grant `--privileged` host hardware GPIO access.


* [ ] **5.3 MVP End-to-End Test Run**
* [ ] Flash single board, start container, access UI over local web browser, connect a button to Input 1 and an LED to Output 1, build logic on screen, click deploy, and verify physical operation.
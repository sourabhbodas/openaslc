#ifndef OPENASLC_TELEMETRY_SERVER_HPP
#define OPENASLC_TELEMETRY_SERVER_HPP

#include "openaslc/memory_map.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace openaslc {

// Minimal RFC 6455 WebSocket server used solely to push %I/%Q telemetry to
// browser clients at a fixed rate. Hand-rolled on raw sockets (rather than a
// WebSocket library) so nothing shipped inside openaslc_app carries a
// non-MIT license -- see Phase 3 plan notes for why Crow/Asio were rejected.
// Deliberately independent of AslcEngine: it polls MemoryMap's own
// thread-safe snapshot accessors from its own thread rather than hooking
// AslcEngine::set_cycle_callback (that slot is already used by
// LogicRuntime::execute_cycle, and AslcEngine only supports one callback).
class TelemetryServer {
public:
    explicit TelemetryServer(std::shared_ptr<MemoryMap> memory_map, int port = 8081,
                              std::chrono::milliseconds broadcast_period = std::chrono::milliseconds(100));
    ~TelemetryServer();

    TelemetryServer(const TelemetryServer&) = delete;
    TelemetryServer& operator=(const TelemetryServer&) = delete;

    void start();
    void stop();

    [[nodiscard]] bool is_running() const noexcept { return is_running_.load(); }
    [[nodiscard]] int get_port() const noexcept { return port_; }

private:
    void accept_loop();
    void client_loop(std::uintptr_t client_socket);

    std::shared_ptr<MemoryMap> memory_map_;
    int port_;
    std::chrono::milliseconds broadcast_period_;
    std::uintptr_t listen_socket_;
    std::atomic<bool> is_running_{false};
    std::jthread accept_thread_;
    std::mutex clients_mutex_;
    std::vector<std::jthread> client_threads_;
};

} // namespace openaslc

#endif // OPENASLC_TELEMETRY_SERVER_HPP

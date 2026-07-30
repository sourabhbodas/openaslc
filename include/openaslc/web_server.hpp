#ifndef OPENASLC_WEB_SERVER_HPP
#define OPENASLC_WEB_SERVER_HPP

#include "openaslc/logic_interpreter.hpp"
#include "openaslc/memory_map.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <thread>

namespace httplib {
class Server;
} // namespace httplib

namespace openaslc {

class ConfigArchive;
class RingBufferLogger;

// Embedded HTTP server: hosts the static web UI (www/) and accepts new logic
// programs over POST /api/deploy. Telemetry streaming lives in a separate
// TelemetryServer (its own WebSocket listener on its own port) -- see that
// class for why the two are kept apart. No authentication -- acceptable for
// a local MVP (neither Concepts/to-do.md nor what-is-it.md call for it yet),
// but a real, known gap rather than a silently assumed one.
class WebServer {
public:
    // Forwards a simulated-input write (byte offset, bit index, value) to
    // whatever driver can actually set it -- e.g. MockDriver::set_mock_input_bit.
    // WebServer deliberately doesn't know about MockDriver: on real hardware
    // there's nothing to simulate, so main.cpp simply omits this callback and
    // /api/input is never registered.
    using SetInputCallback = std::function<void(std::size_t byte, uint8_t bit, bool value)>;

    // config_archive/logger are both optional (nullptr by default): when a
    // ConfigArchive is supplied, a successful /api/deploy also records a commit +
    // deployment and /api/history is registered; when omitted (e.g. a plain test
    // server), deploy behaves exactly as it did before Phase 4. Same for logger --
    // deploy attempts are recorded if present, silently skipped if not.
    WebServer(LogicRuntime& logic_runtime, std::filesystem::path www_dir, int port = 8080,
              SetInputCallback set_input_callback = nullptr,
              ConfigArchive* config_archive = nullptr, RingBufferLogger* logger = nullptr);
    ~WebServer();

    WebServer(const WebServer&) = delete;
    WebServer& operator=(const WebServer&) = delete;

    void start();
    void stop();

    [[nodiscard]] bool is_running() const noexcept { return is_running_.load(); }
    [[nodiscard]] int get_port() const noexcept { return port_; }

private:
    LogicRuntime& logic_runtime_;
    std::filesystem::path www_dir_;
    int port_;
    SetInputCallback set_input_callback_;
    ConfigArchive* config_archive_;
    RingBufferLogger* logger_;
    std::unique_ptr<httplib::Server> server_;
    std::atomic<bool> is_running_{false};
    std::jthread server_thread_;
};

} // namespace openaslc

#endif // OPENASLC_WEB_SERVER_HPP

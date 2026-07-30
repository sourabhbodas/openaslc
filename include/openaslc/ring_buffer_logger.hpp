#ifndef OPENASLC_RING_BUFFER_LOGGER_HPP
#define OPENASLC_RING_BUFFER_LOGGER_HPP

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string_view>
#include <thread>
#include <vector>

namespace openaslc {

enum class LogLevel { Info, Warn, Error };

// Async logger: log() must never block the calling thread on disk I/O, so a scan
// thread or REST handler can call it freely. A genuinely lock-free MPSC ring
// buffer (as Concepts/what-is-it.md describes) is well-known but subtle to get
// right under a slow consumer; every other concurrent structure in this codebase
// (MemoryMap, LogicRuntime) already uses a plain mutex instead, so this does too --
// the critical section is just an array write + index bump (tens of ns,
// uncontended), and the part that actually matters for determinism (no heap
// allocation on the hot path) is preserved with a fixed-size POD slot rather than
// std::string. Same "best-effort, stated honestly" tradeoff as
// AslcEngine::setup_realtime_priority().
class RingBufferLogger {
public:
    static constexpr std::size_t kCapacity = 1024;
    static constexpr std::size_t kMaxMessageLen = 160;

    explicit RingBufferLogger(std::filesystem::path log_path,
                               std::size_t max_file_bytes = 5 * 1024 * 1024,
                               int max_backups = 3);
    ~RingBufferLogger();

    RingBufferLogger(const RingBufferLogger&) = delete;
    RingBufferLogger& operator=(const RingBufferLogger&) = delete;

    void start();
    void stop();

    // Formats into a fixed-size slot and returns immediately -- never touches the
    // filesystem. If the flush thread can't keep up and the buffer is full, the
    // oldest unflushed entry is overwritten (dropped_count() increments) rather
    // than blocking the caller.
    void log(LogLevel level, std::string_view message);

    [[nodiscard]] uint64_t dropped_count() const noexcept { return dropped_.load(); }
    [[nodiscard]] bool is_running() const noexcept { return is_running_.load(); }

private:
    struct LogEntry {
        std::chrono::system_clock::time_point timestamp;
        LogLevel level;
        std::array<char, kMaxMessageLen> message{};
        std::size_t message_len = 0;
    };

    void flush_loop(std::stop_token stop_token);
    void flush_pending();
    void write_entries(const std::vector<LogEntry>& entries);
    void rotate_if_needed();

    std::filesystem::path log_path_;
    std::size_t max_file_bytes_;
    int max_backups_;

    std::mutex mutex_;
    std::condition_variable_any cv_;
    std::array<LogEntry, kCapacity> buffer_;
    std::size_t head_ = 0;   // next write position
    std::size_t count_ = 0;  // entries currently buffered, unflushed
    std::atomic<uint64_t> dropped_{0};

    std::atomic<bool> is_running_{false};
    std::jthread flush_thread_;
};

} // namespace openaslc

#endif // OPENASLC_RING_BUFFER_LOGGER_HPP

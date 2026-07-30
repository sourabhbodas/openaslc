#include "openaslc/ring_buffer_logger.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace openaslc {

namespace {

const char* level_name(LogLevel level) {
    switch (level) {
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "?";
}

std::string format_timestamp(std::chrono::system_clock::time_point tp) {
    auto tt = std::chrono::system_clock::to_time_t(tp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()) % 1000;
    std::tm tm_buf{};
#ifdef _WIN32
    gmtime_s(&tm_buf, &tt);
#else
    gmtime_r(&tt, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S") << '.' << std::setfill('0') << std::setw(3)
        << ms.count() << 'Z';
    return oss.str();
}

} // namespace

RingBufferLogger::RingBufferLogger(std::filesystem::path log_path, std::size_t max_file_bytes,
                                   int max_backups)
    : log_path_(std::move(log_path)), max_file_bytes_(max_file_bytes), max_backups_(max_backups) {
    if (log_path_.has_parent_path()) {
        std::filesystem::create_directories(log_path_.parent_path());
    }
}

RingBufferLogger::~RingBufferLogger() {
    stop();
}

void RingBufferLogger::start() {
    if (is_running_.load()) {
        return;
    }
    is_running_.store(true);
    flush_thread_ = std::jthread([this](std::stop_token stop_token) { flush_loop(stop_token); });
}

void RingBufferLogger::stop() {
    if (!is_running_.load()) {
        return;
    }
    is_running_.store(false);
    flush_thread_.request_stop();
    cv_.notify_all();
    if (flush_thread_.joinable()) {
        flush_thread_.join();
    }
    // Final drain so nothing logged right before shutdown is silently lost.
    flush_pending();
}

void RingBufferLogger::log(LogLevel level, std::string_view message) {
    std::lock_guard<std::mutex> lock(mutex_);

    LogEntry& entry = buffer_[head_];
    entry.timestamp = std::chrono::system_clock::now();
    entry.level = level;
    entry.message_len = std::min(message.size(), kMaxMessageLen);
    std::copy_n(message.data(), entry.message_len, entry.message.data());

    head_ = (head_ + 1) % kCapacity;
    if (count_ < kCapacity) {
        ++count_;
    } else {
        // Buffer was already full: this write just overwrote the oldest
        // unflushed entry rather than blocking the caller.
        dropped_.fetch_add(1, std::memory_order_relaxed);
    }
    cv_.notify_one();
}

void RingBufferLogger::flush_loop(std::stop_token stop_token) {
    while (!stop_token.stop_requested()) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, stop_token, std::chrono::milliseconds(200),
                         [&] { return count_ > 0 || stop_token.stop_requested(); });
        }
        flush_pending();
    }
}

void RingBufferLogger::flush_pending() {
    std::vector<LogEntry> drained;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (count_ == 0) {
            return;
        }
        drained.reserve(count_);
        std::size_t start = (head_ + kCapacity - count_) % kCapacity;
        for (std::size_t i = 0; i < count_; ++i) {
            drained.push_back(buffer_[(start + i) % kCapacity]);
        }
        count_ = 0;
    }
    write_entries(drained);
}

void RingBufferLogger::write_entries(const std::vector<LogEntry>& entries) {
    if (entries.empty()) {
        return;
    }
    std::ofstream out(log_path_, std::ios::app);
    for (const auto& entry : entries) {
        out << format_timestamp(entry.timestamp) << " [" << level_name(entry.level) << "] "
            << std::string_view(entry.message.data(), entry.message_len) << '\n';
    }
    out.close();

    rotate_if_needed();
}

void RingBufferLogger::rotate_if_needed() {
    std::error_code ec;
    auto size = std::filesystem::file_size(log_path_, ec);
    if (ec || size < max_file_bytes_) {
        return;
    }

    for (int i = max_backups_; i >= 2; --i) {
        auto src = log_path_.string() + "." + std::to_string(i - 1);
        auto dst = log_path_.string() + "." + std::to_string(i);
        if (std::filesystem::exists(src)) {
            std::filesystem::rename(src, dst, ec);
        }
    }
    std::filesystem::rename(log_path_, log_path_.string() + ".1", ec);
}

} // namespace openaslc

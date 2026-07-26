#ifndef OPENASLC_ASLC_ENGINE_HPP
#define OPENASLC_ASLC_ENGINE_HPP

#include "openaslc/memory_map.hpp"
#include "openaslc/io_driver.hpp"
#include <thread>
#include <chrono>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace openaslc {

class AslcEngine {
public:
    using CycleCallback = std::function<void(MemoryMap& memory_map, uint64_t cycle_count)>;

    explicit AslcEngine(std::shared_ptr<MemoryMap> memory_map, std::chrono::milliseconds period = std::chrono::milliseconds(20));
    ~AslcEngine();

    // Disable copy
    AslcEngine(const AslcEngine&) = delete;
    AslcEngine& operator=(const AslcEngine&) = delete;

    void start();
    void stop();

    void add_driver(std::shared_ptr<IIODriver> driver);

    [[nodiscard]] bool is_running() const noexcept { return is_running_.load(); }
    [[nodiscard]] uint64_t get_cycle_count() const noexcept { return cycle_count_.load(); }
    [[nodiscard]] std::chrono::milliseconds get_period() const noexcept { return cycle_period_; }

    void set_cycle_callback(CycleCallback callback) { cycle_callback_ = std::move(callback); }

private:
    void run(std::stop_token stop_token);
    void setup_realtime_priority();

    std::shared_ptr<MemoryMap> memory_map_;
    std::vector<std::shared_ptr<IIODriver>> drivers_;
    std::chrono::milliseconds cycle_period_;
    
    std::jthread worker_thread_;
    std::atomic<bool> is_running_{false};
    std::atomic<uint64_t> cycle_count_{0};

    CycleCallback cycle_callback_;
};

} // namespace openaslc

#endif // OPENASLC_ASLC_ENGINE_HPP

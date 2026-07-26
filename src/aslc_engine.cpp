#include "openaslc/aslc_engine.hpp"
#include <iostream>

#ifdef __linux__
#include <pthread.h>
#include <sys/mman.h>
#include <sched.h>
#endif

namespace openaslc {

AslcEngine::AslcEngine(std::shared_ptr<MemoryMap> memory_map, std::chrono::milliseconds period)
    : memory_map_(std::move(memory_map)), cycle_period_(period) {
    if (!memory_map_) {
        memory_map_ = std::make_shared<MemoryMap>();
    }
}

AslcEngine::~AslcEngine() {
    stop();
}

void AslcEngine::setup_realtime_priority() {
#ifdef __linux__
    // Lock process memory to prevent paging delays in real-time execution
    if (mlockall(MCL_CURRENT | MCL_FUTURE) != 0) {
        std::cerr << "[Warning] AslcEngine: mlockall failed (requires CAP_SYS_NICE or root permissions)" << std::endl;
    }

    // Set SCHED_FIFO real-time priority
    struct sched_param param;
    param.sched_priority = 80;
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) != 0) {
        std::cerr << "[Warning] AslcEngine: Setting SCHED_FIFO priority failed" << std::endl;
    }
#endif
}

void AslcEngine::add_driver(std::shared_ptr<IIODriver> driver) {
    if (driver) {
        drivers_.push_back(driver);
    }
}

void AslcEngine::start() {
    if (is_running_.load()) {
        return;
    }

    for (auto& driver : drivers_) {
        if (driver) {
            driver->initialize();
        }
    }

    is_running_.store(true);
    worker_thread_ = std::jthread([this](std::stop_token st) {
        this->run(st);
    });
}

void AslcEngine::stop() {
    if (!is_running_.load()) {
        return;
    }
    is_running_.store(false);
    if (worker_thread_.joinable()) {
        worker_thread_.request_stop();
        worker_thread_.join();
    }
}

void AslcEngine::run(std::stop_token stop_token) {
    setup_realtime_priority();

    auto next_cycle_time = std::chrono::steady_clock::now();

    while (!stop_token.stop_requested() && is_running_.load()) {
        next_cycle_time += cycle_period_;

        // 1. Hardware abstraction read step
        for (auto& driver : drivers_) {
            if (driver) {
                driver->read_inputs(*memory_map_);
            }
        }

        // 2. Logic execution callback
        if (cycle_callback_) {
            cycle_callback_(*memory_map_, cycle_count_.load());
        }

        // 3. Hardware abstraction write step
        for (auto& driver : drivers_) {
            if (driver) {
                driver->write_outputs(*memory_map_);
            }
        }

        cycle_count_.fetch_add(1, std::memory_order_relaxed);

        std::this_thread::sleep_until(next_cycle_time);
    }
}

} // namespace openaslc

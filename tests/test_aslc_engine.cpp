#include <catch2/catch_test_macros.hpp>
#include "openaslc/aslc_engine.hpp"
#include <thread>
#include <chrono>

TEST_CASE("AslcEngine lifecycle and cycle count verification", "[engine]") {
    auto mem_map = std::make_shared<openaslc::MemoryMap>();
    openaslc::AslcEngine engine(mem_map, std::chrono::milliseconds(10));

    REQUIRE_FALSE(engine.is_running());
    REQUIRE(engine.get_cycle_count() == 0);

    engine.start();
    REQUIRE(engine.is_running());

    // Allow engine to run for ~50ms (expected ~4-5 cycles)
    std::this_thread::sleep_for(std::chrono::milliseconds(55));

    engine.stop();
    REQUIRE_FALSE(engine.is_running());

    uint64_t count = engine.get_cycle_count();
    REQUIRE(count >= 3);
}

TEST_CASE("AslcEngine callback execution", "[engine]") {
    auto mem_map = std::make_shared<openaslc::MemoryMap>();
    openaslc::AslcEngine engine(mem_map, std::chrono::milliseconds(10));

    uint64_t callback_invocations = 0;
    engine.set_cycle_callback([&](openaslc::MemoryMap& map, uint64_t cycle) {
        callback_invocations++;
        map.write_byte(openaslc::MemoryArea::Memory, 0, static_cast<uint8_t>(cycle & 0xFF));
    });

    engine.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(35));
    engine.stop();

    REQUIRE(callback_invocations >= 2);
    REQUIRE(mem_map->read_byte(openaslc::MemoryArea::Memory, 0) != 0);
}

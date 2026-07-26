#include <catch2/catch_test_macros.hpp>
#include "openaslc/aslc_engine.hpp"
#include "openaslc/mock_driver.hpp"
#include <thread>
#include <chrono>

TEST_CASE("MockDriver input reading and output change detection", "[hal]") {
    auto mem_map = std::make_shared<openaslc::MemoryMap>();
    auto mock_driver = std::make_shared<openaslc::MockDriver>(false); // quiet mode for tests

    openaslc::AslcEngine engine(mem_map, std::chrono::milliseconds(10));
    engine.add_driver(mock_driver);

    // Simulate physical input toggling before engine start
    mock_driver->set_mock_input_bit(0, 2, true);

    // Logic block: Copy input %I[0].2 to output %Q[0].5
    engine.set_cycle_callback([](openaslc::MemoryMap& map, uint64_t cycle) {
        bool input_val = map.read_bit(openaslc::MemoryArea::Input, 0, 2);
        map.write_bit(openaslc::MemoryArea::Output, 0, 5, input_val);
    });

    engine.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    // Verify engine read %I[0].2 and logic set %Q[0].5
    REQUIRE(mem_map->read_bit(openaslc::MemoryArea::Input, 0, 2) == true);
    REQUIRE(mem_map->read_bit(openaslc::MemoryArea::Output, 0, 5) == true);

    // Toggle mock input off while running
    mock_driver->set_mock_input_bit(0, 2, false);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));

    REQUIRE(mem_map->read_bit(openaslc::MemoryArea::Input, 0, 2) == false);
    REQUIRE(mem_map->read_bit(openaslc::MemoryArea::Output, 0, 5) == false);

    engine.stop();
}

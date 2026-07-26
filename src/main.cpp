#include <iostream>
#include <thread>
#include <chrono>
#include "openaslc/aslc_engine.hpp"
#include "openaslc/mock_driver.hpp"

int main(int argc, char* argv[]) {
    std::cout << "OpenASLC Engine MVP Initializing..." << std::endl;

    auto memory_map = std::make_shared<openaslc::MemoryMap>();
    auto mock_driver = std::make_shared<openaslc::MockDriver>(true);

    openaslc::AslcEngine engine(memory_map, std::chrono::milliseconds(20));
    engine.add_driver(mock_driver);

    // Simulated PLC Logic: Output %Q[0].0 = Input %I[0].0
    engine.set_cycle_callback([](openaslc::MemoryMap& map, uint64_t cycle) {
        bool in0 = map.read_bit(openaslc::MemoryArea::Input, 0, 0);
        map.write_bit(openaslc::MemoryArea::Output, 0, 0, in0);
    });

    std::cout << "Starting AslcEngine with Mock HAL Driver..." << std::endl;
    engine.start();

    // Toggle mock input on cycle simulation
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    std::cout << "Simulating Physical Input %I[0].0 -> HIGH" << std::endl;
    mock_driver->set_mock_input_bit(0, 0, true);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::cout << "Simulating Physical Input %I[0].0 -> LOW" << std::endl;
    mock_driver->set_mock_input_bit(0, 0, false);

    std::this_thread::sleep_for(std::chrono::milliseconds(60));

    std::cout << "Stopping AslcEngine after " << engine.get_cycle_count() << " cycles." << std::endl;
    engine.stop();

    return 0;
}

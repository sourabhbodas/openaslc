#include <iostream>
#include <thread>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include "openaslc/aslc_engine.hpp"
#include "openaslc/mock_driver.hpp"
#include "openaslc/logic_interpreter.hpp"

int main(int argc, char* argv[]) {
    std::cout << "OpenASLC Engine MVP Initializing..." << std::endl;

    auto memory_map = std::make_shared<openaslc::MemoryMap>();
    auto mock_driver = std::make_shared<openaslc::MockDriver>(true);

    openaslc::AslcEngine engine(memory_map, std::chrono::milliseconds(20));
    engine.add_driver(mock_driver);

    const std::filesystem::path examples_dir = OPENASLC_EXAMPLES_DIR;

    std::shared_ptr<openaslc::LogicProgram> program_a;
    try {
        program_a = openaslc::LogicProgram::from_file(examples_dir / "logic_example.json",
                                                       engine.get_period());
    } catch (const openaslc::LogicParseError& e) {
        std::cerr << "Failed to load logic program: " << e.what() << std::endl;
        return 1;
    }

    openaslc::LogicRuntime runtime(engine.get_period(), program_a);
    engine.set_cycle_callback([&runtime](openaslc::MemoryMap& map, uint64_t cycle) {
        runtime.execute_cycle(map, cycle);
    });

    std::cout << "Starting AslcEngine with Mock HAL Driver and JSON-driven logic..." << std::endl;
    engine.start();

    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    std::cout << "Simulating Physical Inputs %I[0].0/.1/.2 -> HIGH" << std::endl;
    mock_driver->set_mock_input_bit(0, 0, true);
    mock_driver->set_mock_input_bit(0, 1, true);
    mock_driver->set_mock_input_bit(0, 2, true);

    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    std::cout << "Hot-reloading logic program (no engine stop/restart)..." << std::endl;
    try {
        auto program_b = openaslc::LogicProgram::from_file(
            examples_dir / "logic_example_reload.json", engine.get_period());
        runtime.reload(program_b);
    } catch (const openaslc::LogicParseError& e) {
        std::cerr << "Failed to load reload logic program: " << e.what() << std::endl;
        engine.stop();
        return 1;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::cout << "Simulating Physical Inputs %I[0].0/.1/.2 -> LOW" << std::endl;
    mock_driver->set_mock_input_bit(0, 0, false);
    mock_driver->set_mock_input_bit(0, 1, false);
    mock_driver->set_mock_input_bit(0, 2, false);

    std::this_thread::sleep_for(std::chrono::milliseconds(60));

    std::cout << "Stopping AslcEngine after " << engine.get_cycle_count() << " cycles." << std::endl;
    engine.stop();

    return 0;
}

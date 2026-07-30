#include <iostream>
#include <thread>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include "openaslc/aslc_engine.hpp"
#include "openaslc/mock_driver.hpp"
#include "openaslc/logic_interpreter.hpp"
#include "openaslc/web_server.hpp"
#include "openaslc/telemetry_server.hpp"
#include "openaslc/ring_buffer_logger.hpp"
#include "openaslc/config_archive.hpp"

int main(int argc, char* argv[]) {
    std::cout << "OpenASLC Engine MVP Initializing..." << std::endl;

    const std::filesystem::path data_dir = OPENASLC_DATA_DIR;
    openaslc::RingBufferLogger logger(data_dir / "softplc.log");
    logger.start();
    openaslc::ConfigArchive archive(data_dir / "config_archive.db");

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
        logger.log(openaslc::LogLevel::Error, std::string("failed to load initial logic program: ") + e.what());
        return 1;
    }
    logger.log(openaslc::LogLevel::Info, "loaded initial logic program (" +
                                              std::to_string(program_a->rule_count()) + " rules)");

    openaslc::LogicRuntime runtime(engine.get_period(), program_a);
    engine.set_cycle_callback([&runtime](openaslc::MemoryMap& map, uint64_t cycle) {
        runtime.execute_cycle(map, cycle);
    });

    std::cout << "Starting AslcEngine with Mock HAL Driver and JSON-driven logic..." << std::endl;
    logger.log(openaslc::LogLevel::Info, "AslcEngine starting");
    engine.start();

    const std::filesystem::path www_dir = OPENASLC_WWW_DIR;
    openaslc::WebServer web_server(runtime, www_dir, 8080,
                                    [mock_driver](std::size_t byte, uint8_t bit, bool value) {
                                        mock_driver->set_mock_input_bit(byte, bit, value);
                                    },
                                    &archive, &logger);
    openaslc::TelemetryServer telemetry_server(memory_map, 8081);
    web_server.start();
    telemetry_server.start();
    logger.log(openaslc::LogLevel::Info, "WebServer + TelemetryServer started");
    std::cout << "Web UI:   http://localhost:8080" << std::endl;
    std::cout << "Telemetry: ws://localhost:8081/ws/telemetry" << std::endl;

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
        logger.log(openaslc::LogLevel::Info, "hot-reloaded logic program (" +
                                                  std::to_string(program_b->rule_count()) +
                                                  " rules)");
    } catch (const openaslc::LogicParseError& e) {
        std::cerr << "Failed to load reload logic program: " << e.what() << std::endl;
        logger.log(openaslc::LogLevel::Error, std::string("hot-reload failed: ") + e.what());
        engine.stop();
        return 1;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::cout << "Simulating Physical Inputs %I[0].0/.1/.2 -> LOW" << std::endl;
    mock_driver->set_mock_input_bit(0, 0, false);
    mock_driver->set_mock_input_bit(0, 1, false);
    mock_driver->set_mock_input_bit(0, 2, false);

    std::this_thread::sleep_for(std::chrono::milliseconds(60));

    std::cout << "Demo sequence complete (" << engine.get_cycle_count() << " cycles so far)."
              << std::endl;
    std::cout << "Open the Web UI above, or press Enter here to stop." << std::endl;
    std::cin.get();

    std::cout << "Stopping AslcEngine after " << engine.get_cycle_count() << " cycles." << std::endl;
    logger.log(openaslc::LogLevel::Info, "shutting down after " +
                                              std::to_string(engine.get_cycle_count()) + " cycles");
    telemetry_server.stop();
    web_server.stop();
    engine.stop();
    logger.stop();

    return 0;
}

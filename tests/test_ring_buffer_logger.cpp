#include <catch2/catch_test_macros.hpp>
#include "openaslc/ring_buffer_logger.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

using namespace openaslc;
using namespace std::chrono_literals;

namespace {

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path);
    std::ostringstream oss;
    oss << in.rdbuf();
    return oss.str();
}

} // namespace

TEST_CASE("RingBufferLogger writes logged messages to disk in order", "[logging]") {
    auto dir = std::filesystem::temp_directory_path() / "openaslc_test_logger";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto log_path = dir / "test.log";

    {
        RingBufferLogger logger(log_path);
        logger.start();
        logger.log(LogLevel::Info, "first message");
        logger.log(LogLevel::Warn, "second message");
        logger.log(LogLevel::Error, "third message");
        logger.stop();
    }

    REQUIRE(std::filesystem::exists(log_path));
    auto contents = read_file(log_path);

    auto first = contents.find("first message");
    auto second = contents.find("second message");
    auto third = contents.find("third message");
    REQUIRE(first != std::string::npos);
    REQUIRE(second != std::string::npos);
    REQUIRE(third != std::string::npos);
    REQUIRE(first < second);
    REQUIRE(second < third);
    REQUIRE(contents.find("[INFO]") != std::string::npos);
    REQUIRE(contents.find("[WARN]") != std::string::npos);
    REQUIRE(contents.find("[ERROR]") != std::string::npos);
}

TEST_CASE("RingBufferLogger rotates the log file once it exceeds max_file_bytes", "[logging]") {
    auto dir = std::filesystem::temp_directory_path() / "openaslc_test_logger_rotate";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto log_path = dir / "rotate.log";

    // Tiny threshold so a handful of messages force at least one rotation.
    RingBufferLogger logger(log_path, /*max_file_bytes=*/200, /*max_backups=*/2);
    logger.start();
    for (int i = 0; i < 20; ++i) {
        logger.log(LogLevel::Info, "padding message number " + std::to_string(i));
    }
    logger.stop();

    REQUIRE(std::filesystem::exists(log_path.string() + ".1"));
}

TEST_CASE("RingBufferLogger never blocks the caller even past capacity", "[logging]") {
    auto dir = std::filesystem::temp_directory_path() / "openaslc_test_logger_overflow";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    auto log_path = dir / "overflow.log";

    // Don't start() the flush thread: nothing drains the buffer, so pushing past
    // capacity must overwrite the oldest entry rather than block.
    RingBufferLogger logger(log_path);
    for (std::size_t i = 0; i < RingBufferLogger::kCapacity + 10; ++i) {
        logger.log(LogLevel::Info, "msg " + std::to_string(i));
    }
    REQUIRE(logger.dropped_count() == 10);
}

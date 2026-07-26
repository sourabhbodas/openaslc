#include <catch2/catch_test_macros.hpp>
#include "openaslc/memory_map.hpp"

TEST_CASE("MemoryMap basic initialization and size checks", "[memory_map]") {
    openaslc::MemoryMap mem;
    REQUIRE(mem.get_size(openaslc::MemoryArea::Input) == 512);
    REQUIRE(mem.get_size(openaslc::MemoryArea::Output) == 512);
    REQUIRE(mem.get_size(openaslc::MemoryArea::Memory) == 1024);

    REQUIRE(mem.read_byte(openaslc::MemoryArea::Input, 0) == 0);
    REQUIRE(mem.read_byte(openaslc::MemoryArea::Output, 0) == 0);
    REQUIRE(mem.read_byte(openaslc::MemoryArea::Memory, 0) == 0);
}

TEST_CASE("MemoryMap byte read and write operations", "[memory_map]") {
    openaslc::MemoryMap mem;

    mem.write_byte(openaslc::MemoryArea::Input, 10, 0xAB);
    REQUIRE(mem.read_byte(openaslc::MemoryArea::Input, 10) == 0xAB);

    mem.write_byte(openaslc::MemoryArea::Output, 500, 0xFF);
    REQUIRE(mem.read_byte(openaslc::MemoryArea::Output, 500) == 0xFF);

    mem.write_byte(openaslc::MemoryArea::Memory, 1023, 0x42);
    REQUIRE(mem.read_byte(openaslc::MemoryArea::Memory, 1023) == 0x42);
}

TEST_CASE("MemoryMap bit read and write operations", "[memory_map]") {
    openaslc::MemoryMap mem;

    REQUIRE(mem.read_bit(openaslc::MemoryArea::Input, 0, 3) == false);

    mem.write_bit(openaslc::MemoryArea::Input, 0, 3, true);
    REQUIRE(mem.read_bit(openaslc::MemoryArea::Input, 0, 3) == true);
    REQUIRE(mem.read_byte(openaslc::MemoryArea::Input, 0) == 0x08);

    mem.write_bit(openaslc::MemoryArea::Input, 0, 3, false);
    REQUIRE(mem.read_bit(openaslc::MemoryArea::Input, 0, 3) == false);
    REQUIRE(mem.read_byte(openaslc::MemoryArea::Input, 0) == 0x00);
}

TEST_CASE("MemoryMap out of bounds protection", "[memory_map]") {
    openaslc::MemoryMap mem;

    REQUIRE_THROWS_AS(mem.read_byte(openaslc::MemoryArea::Input, 512), std::out_of_range);
    REQUIRE_THROWS_AS(mem.write_byte(openaslc::MemoryArea::Output, 512, 0x01), std::out_of_range);
    REQUIRE_THROWS_AS(mem.read_byte(openaslc::MemoryArea::Memory, 1024), std::out_of_range);
    REQUIRE_THROWS_AS(mem.write_bit(openaslc::MemoryArea::Input, 0, 8, true), std::invalid_argument);
}

TEST_CASE("MemoryMap bulk read and snapshot isolation", "[memory_map]") {
    openaslc::MemoryMap mem;
    std::vector<uint8_t> sample_data = {0x01, 0x02, 0x03, 0x04};

    mem.write_bytes(openaslc::MemoryArea::Memory, 100, sample_data);
    auto read_back = mem.read_bytes(openaslc::MemoryArea::Memory, 100, 4);
    REQUIRE(read_back == sample_data);

    auto snapshot = mem.get_area_snapshot(openaslc::MemoryArea::Memory);
    REQUIRE(snapshot.size() == 1024);
    REQUIRE(snapshot[100] == 0x01);
    REQUIRE(snapshot[103] == 0x04);
}

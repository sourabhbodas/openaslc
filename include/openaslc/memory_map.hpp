#ifndef OPENASLC_MEMORY_MAP_HPP
#define OPENASLC_MEMORY_MAP_HPP

#include <array>
#include <cstdint>
#include <cstddef>
#include <shared_mutex>
#include <vector>
#include <optional>
#include <stdexcept>

namespace openaslc {

enum class MemoryArea {
    Input,   // %I
    Output,  // %Q
    Memory   // %M
};

class MemoryMap {
public:
    static constexpr std::size_t INPUT_SIZE = 512;
    static constexpr std::size_t OUTPUT_SIZE = 512;
    static constexpr std::size_t MEMORY_SIZE = 1024;

    MemoryMap() = default;

    // Get region size
    [[nodiscard]] std::size_t get_size(MemoryArea area) const noexcept;

    // Byte level read/write
    [[nodiscard]] uint8_t read_byte(MemoryArea area, std::size_t offset) const;
    void write_byte(MemoryArea area, std::size_t offset, uint8_t value);

    // Bit level read/write (bit_index 0 to 7)
    [[nodiscard]] bool read_bit(MemoryArea area, std::size_t byte_offset, uint8_t bit_index) const;
    void write_bit(MemoryArea area, std::size_t byte_offset, uint8_t bit_index, bool value);

    // Bulk byte operations
    std::vector<uint8_t> read_bytes(MemoryArea area, std::size_t offset, std::size_t count) const;
    void write_bytes(MemoryArea area, std::size_t offset, const std::vector<uint8_t>& data);

    // Snapshots for double buffering/telemetry isolation
    [[nodiscard]] std::vector<uint8_t> get_area_snapshot(MemoryArea area) const;
    void copy_snapshot_to(MemoryArea area, const std::vector<uint8_t>& snapshot);

private:
    std::array<uint8_t, INPUT_SIZE> input_area_{0};
    std::array<uint8_t, OUTPUT_SIZE> output_area_{0};
    std::array<uint8_t, MEMORY_SIZE> memory_area_{0};

    mutable std::shared_mutex rw_mutex_;

    uint8_t* get_area_ptr(MemoryArea area);
    const uint8_t* get_area_ptr(MemoryArea area) const;
    void validate_bounds(MemoryArea area, std::size_t offset, std::size_t count = 1) const;
};

} // namespace openaslc

#endif // OPENASLC_MEMORY_MAP_HPP

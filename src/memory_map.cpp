#include "openaslc/memory_map.hpp"
#include <mutex>
#include <algorithm>

namespace openaslc {

std::size_t MemoryMap::get_size(MemoryArea area) const noexcept {
    switch (area) {
        case MemoryArea::Input:  return INPUT_SIZE;
        case MemoryArea::Output: return OUTPUT_SIZE;
        case MemoryArea::Memory: return MEMORY_SIZE;
    }
    return 0;
}

uint8_t* MemoryMap::get_area_ptr(MemoryArea area) {
    switch (area) {
        case MemoryArea::Input:  return input_area_.data();
        case MemoryArea::Output: return output_area_.data();
        case MemoryArea::Memory: return memory_area_.data();
    }
    return nullptr;
}

const uint8_t* MemoryMap::get_area_ptr(MemoryArea area) const {
    switch (area) {
        case MemoryArea::Input:  return input_area_.data();
        case MemoryArea::Output: return output_area_.data();
        case MemoryArea::Memory: return memory_area_.data();
    }
    return nullptr;
}

void MemoryMap::validate_bounds(MemoryArea area, std::size_t offset, std::size_t count) const {
    std::size_t max_size = get_size(area);
    if (offset + count > max_size) {
        throw std::out_of_range("MemoryMap access out of bounds");
    }
}

uint8_t MemoryMap::read_byte(MemoryArea area, std::size_t offset) const {
    validate_bounds(area, offset, 1);
    std::shared_lock lock(rw_mutex_);
    return get_area_ptr(area)[offset];
}

void MemoryMap::write_byte(MemoryArea area, std::size_t offset, uint8_t value) {
    validate_bounds(area, offset, 1);
    std::unique_lock lock(rw_mutex_);
    get_area_ptr(area)[offset] = value;
}

bool MemoryMap::read_bit(MemoryArea area, std::size_t byte_offset, uint8_t bit_index) const {
    if (bit_index > 7) {
        throw std::invalid_argument("Bit index must be 0..7");
    }
    uint8_t byte_val = read_byte(area, byte_offset);
    return (byte_val & (1U << bit_index)) != 0;
}

void MemoryMap::write_bit(MemoryArea area, std::size_t byte_offset, uint8_t bit_index, bool value) {
    if (bit_index > 7) {
        throw std::invalid_argument("Bit index must be 0..7");
    }
    validate_bounds(area, byte_offset, 1);
    std::unique_lock lock(rw_mutex_);
    uint8_t* ptr = get_area_ptr(area);
    if (value) {
        ptr[byte_offset] |= (1U << bit_index);
    } else {
        ptr[byte_offset] &= ~(1U << bit_index);
    }
}

std::vector<uint8_t> MemoryMap::read_bytes(MemoryArea area, std::size_t offset, std::size_t count) const {
    validate_bounds(area, offset, count);
    std::shared_lock lock(rw_mutex_);
    const uint8_t* ptr = get_area_ptr(area) + offset;
    return std::vector<uint8_t>(ptr, ptr + count);
}

void MemoryMap::write_bytes(MemoryArea area, std::size_t offset, const std::vector<uint8_t>& data) {
    validate_bounds(area, offset, data.size());
    std::unique_lock lock(rw_mutex_);
    uint8_t* ptr = get_area_ptr(area) + offset;
    std::copy(data.begin(), data.end(), ptr);
}

std::vector<uint8_t> MemoryMap::get_area_snapshot(MemoryArea area) const {
    std::size_t size = get_size(area);
    std::shared_lock lock(rw_mutex_);
    const uint8_t* ptr = get_area_ptr(area);
    return std::vector<uint8_t>(ptr, ptr + size);
}

void MemoryMap::copy_snapshot_to(MemoryArea area, const std::vector<uint8_t>& snapshot) {
    validate_bounds(area, 0, snapshot.size());
    std::unique_lock lock(rw_mutex_);
    uint8_t* ptr = get_area_ptr(area);
    std::copy(snapshot.begin(), snapshot.end(), ptr);
}

} // namespace openaslc

#include "openaslc/mock_driver.hpp"

namespace openaslc {

MockDriver::MockDriver(bool print_changes)
    : print_changes_(print_changes),
      mock_inputs_(MemoryMap::INPUT_SIZE, 0),
      previous_outputs_(MemoryMap::OUTPUT_SIZE, 0) {}

bool MockDriver::initialize() {
    std::fill(previous_outputs_.begin(), previous_outputs_.end(), 0);
    return true;
}

void MockDriver::read_inputs(MemoryMap& memory_map) {
    memory_map.write_bytes(MemoryArea::Input, 0, mock_inputs_);
}

void MockDriver::write_outputs(const MemoryMap& memory_map) {
    auto current_outputs = memory_map.get_area_snapshot(MemoryArea::Output);

    if (print_changes_) {
        for (std::size_t i = 0; i < current_outputs.size(); ++i) {
            if (current_outputs[i] != previous_outputs_[i]) {
                std::cout << "[MockDriver Output Change] %Q[" << i << "]: 0x" 
                          << std::hex << static_cast<int>(previous_outputs_[i])
                          << " -> 0x" << static_cast<int>(current_outputs[i])
                          << std::dec << std::endl;
            }
        }
    }

    previous_outputs_ = std::move(current_outputs);
}

void MockDriver::set_mock_input_bit(std::size_t byte_offset, uint8_t bit_index, bool value) {
    if (byte_offset >= mock_inputs_.size() || bit_index > 7) {
        return;
    }
    if (value) {
        mock_inputs_[byte_offset] |= (1U << bit_index);
    } else {
        mock_inputs_[byte_offset] &= ~(1U << bit_index);
    }
}

void MockDriver::toggle_mock_input_bit(std::size_t byte_offset, uint8_t bit_index) {
    if (byte_offset >= mock_inputs_.size() || bit_index > 7) {
        return;
    }
    mock_inputs_[byte_offset] ^= (1U << bit_index);
}

} // namespace openaslc

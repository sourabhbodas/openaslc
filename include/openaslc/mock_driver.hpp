#ifndef OPENASLC_MOCK_DRIVER_HPP
#define OPENASLC_MOCK_DRIVER_HPP

#include "openaslc/io_driver.hpp"
#include <vector>
#include <iostream>

namespace openaslc {

class MockDriver : public IIODriver {
public:
    explicit MockDriver(bool print_changes = true);

    bool initialize() override;
    void read_inputs(MemoryMap& memory_map) override;
    void write_outputs(const MemoryMap& memory_map) override;

    // Helper methods to simulate hardware inputs
    void set_mock_input_bit(std::size_t byte_offset, uint8_t bit_index, bool value);
    void toggle_mock_input_bit(std::size_t byte_offset, uint8_t bit_index);

private:
    bool print_changes_;
    std::vector<uint8_t> mock_inputs_;
    std::vector<uint8_t> previous_outputs_;
};

} // namespace openaslc

#endif // OPENASLC_MOCK_DRIVER_HPP

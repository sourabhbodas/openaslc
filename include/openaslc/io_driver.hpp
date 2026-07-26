#ifndef OPENASLC_IO_DRIVER_HPP
#define OPENASLC_IO_DRIVER_HPP

#include "openaslc/memory_map.hpp"

namespace openaslc {

class IIODriver {
public:
    virtual ~IIODriver() = default;

    virtual bool initialize() = 0;
    virtual void read_inputs(MemoryMap& memory_map) = 0;
    virtual void write_outputs(const MemoryMap& memory_map) = 0;
};

} // namespace openaslc

#endif // OPENASLC_IO_DRIVER_HPP

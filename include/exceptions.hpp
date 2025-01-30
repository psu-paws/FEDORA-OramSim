#pragma once

#include <exception>
#include <memory_interface.hpp>

class MemoryAccessOutOfRange: public std::exception {
    public:
    const uint64_t offending_address;
    const uint64_t valid_range_start;
    const uint64_t valid_range_end;

    public:
    MemoryAccessOutOfRange(uint64_t offending_address, uint64_t valid_range_start, uint64_t valid_range_end) :
        offending_address(offending_address), valid_range_start(valid_range_start), valid_range_end(valid_range_end)
    {}

    const char* what() const noexcept {
        return "Illegal Memory accessed!";
    }
};
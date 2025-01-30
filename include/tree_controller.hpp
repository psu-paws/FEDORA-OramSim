#pragma once
#include <optional>
#include <memory_interface.hpp>
#include <oram_defs.hpp>

class TreeController {
    std::optional<addr_t> current_path() const;
    void read_path(addr_t path);
    void write_path();
    std::optional<std::pair<BlockMetadata*, byte_t*>> find_block(addr_t block_address);
    
};
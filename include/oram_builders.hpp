#pragma once

#include <oram.hpp>
#include <stdint.h>

int create_oram_entry_point(int argc, const char** argv);

unique_memory_t createBinaryPathOram(
    uint64_t size, uint64_t block_size, uint64_t blocks_per_bucket,
    uint64_t max_position_map_size = 32768UL, bool recursive = false,
    uint64_t recursive_level = 0,
    std::string_view layout_type = "BasicHeapLayout",
    uint64_t page_size = 64
);

unique_memory_t createRAWOram(
    uint64_t size, uint64_t block_size, uint64_t blocks_per_bucket,
    uint64_t num_accesses_per_eviction,
    uint64_t max_position_map_size = 32768UL, bool recursive = false,
    std::string_view layout_type = "BasicHeapLayout",
    uint64_t page_size = 64
);

unique_memory_t createPageOptimizedRAWOram(
    uint64_t size, uint64_t block_size, uint64_t blocks_per_bucket,
    uint64_t num_accesses_per_eviction,
    uint64_t stash_capacity,
    uint64_t max_position_map_size = 32768UL, bool recursive = false,
    uint64_t page_size = 4096,
    double max_load_factor = 1.0,
    uint64_t tree_order = 16,
    bool fast_init = false,
    std::string_view crypto_module_name = "PlainText"
);

unique_memory_t createBinaryPathOram2(
    uint64_t size, uint64_t block_size, uint64_t page_size = 4096,
    bool is_page_size_strict = true,
    uint64_t max_position_map_size = 32768UL, bool recursive = false,
    uint64_t recursive_level = 0, 
    double max_load_factor = 1.0,
    bool fast_init = false,
    uint64_t levels_per_page = 1,
    std::string_view crypto_module_name = "PlainText"
);
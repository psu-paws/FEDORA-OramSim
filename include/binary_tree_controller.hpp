#pragma once

#include <stdint.h>
#include <memory_interface.hpp>
#include <binary_tree_layout.hpp>
#include <oram_defs.hpp>
#include <stash.hpp>
#include <toml++/toml.h>
#include <limits>

class UntrustedMemoryController final{
    public:

    static UntrustedMemoryController create( 
        Memory *untrusted_memory, unique_tree_layout_t &&data_layout, unique_tree_layout_t &&metadata_layout, 
        uint64_t levels, uint64_t blocks_per_bucket, uint64_t block_size
    );

    static UntrustedMemoryController create( 
        Memory *untrusted_memory, std::string_view data_layout_type, std::string_view metadata_layout_type, 
        uint64_t levels, uint64_t blocks_per_bucket, uint64_t block_size
    );

    static UntrustedMemoryController create( 
        Memory *untrusted_memory, const toml::table &table, 
        uint64_t levels, uint64_t blocks_per_bucket, uint64_t block_size
    );

    private:
    UntrustedMemoryController(
        Memory *untrusted_memory, unique_tree_layout_t &&data_layout, unique_tree_layout_t &&metadata_layout, 
        uint64_t levels, uint64_t blocks_per_bucket, uint64_t block_size, bool data_use_block_access,
        bool metadata_use_block_access
    );

    public:
    void read_path(uint64_t path);
    BlockMetadata * get_metadata_pointer(uint64_t level);
    byte_t * get_data_pointer(uint64_t level);
    void write_path();
    std::optional<PathLocation> find_block(uint64_t logical_block_address, std::function<bool(uint64_t, uint64_t)> verifier = [](uint64_t level, uint64_t block_index){return true;}) const;
    bool find_and_remove_block(uint64_t logical_block_address, BlockMetadata *metadata_buffer, byte_t * data_block_buffer);
    inline bool find_and_remove_block(uint64_t logical_block_address, StashEntry &stash_entry) {
        return find_and_remove_block(logical_block_address, &stash_entry.metadata, stash_entry.block.data());
    }
    std::size_t try_evict_blocks(uint64_t max_count, uint64_t ignored_bits, BlockMetadata *metadatas, byte_t *data_blocks, uint64_t level_limit = std::numeric_limits<uint64_t>::max());
    toml::table to_toml() const;

    private:
    struct PageMapEntry{
        uint64_t page_index;
        uint64_t page_offset;

        PageMapEntry(uint64_t page_index, uint64_t page_offset) :
        page_index(page_index), page_offset(page_offset)
        {}

        PageMapEntry() : PageMapEntry(0, 0) {}

        
    };

    private:
    const bool data_use_block_access;
    const bool metadata_use_block_access;
    const uint64_t levels;
    const uint64_t page_size;
    const uint64_t block_size;
    const uint64_t blocks_per_bucket;
    const uint64_t block_access_start;
    const uint64_t data_non_block_access_start;
    const uint64_t metadata_non_block_access_start = 0;
    unique_tree_layout_t data_layout;
    unique_tree_layout_t metadata_layout;
    Memory *untrusted_memory;

    std::optional<uint64_t> current_path;
    std::vector<MemoryRequest> requests;
    std::vector<PageMapEntry> data_page_map;
    std::vector<PageMapEntry> metadata_page_map;
};
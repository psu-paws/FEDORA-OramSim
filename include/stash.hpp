#pragma once
#include <vector>
#include <filesystem>
#include <cstdint>

#include <oram_defs.hpp>

#include <block_callback.hpp>

struct StashEntry {
    BlockMetadata metadata;
    bytes_t block;

    public:
    inline StashEntry(std::size_t block_size) : block(block_size) {
    }
    inline std::size_t block_size() const noexcept {
        return this->block.size();
    }
    void write_to_memory(byte_t *metadata_address, byte_t *data_address) const;
};

class Stash {
    
    public:
    Stash(uint64_t block_size, uint64_t entries);

    void add_block(const BlockMetadata *metadata, const byte_t *data);
    void add_block(const StashEntry &stash_entry);
    // std::optional<Reference> find_block(uint64_t logical_block_address);
    // bool find_block_linear_scan(uint64_t logical_block_address, BlockMetadata *metadata, bytes_t *data);
    bool find_and_remove_block(uint64_t logical_block_address, BlockMetadata *metadata, byte_t *data);
    bool find_and_remove_block(uint64_t logical_block_address, StashEntry &stash_entry);
    // void add_new_block(uint64_t logical_block_address, uint64_t path);
    // std::vector<StashEntry> try_evict_blocks(uint64_t max_count , uint16_t ignored_bits, uint64_t path);
    std::size_t try_evict_blocks(uint64_t max_count, uint64_t ignored_bits, uint64_t path, BlockMetadata *metadatas, byte_t *data_blocks);
    inline std::size_t size() const noexcept {
        return this->num_blocks_in_stash;
    }
    inline std::size_t capacity() const noexcept {
        return this->metadata.size();
    }
    // void clear_invalid_blocks();
    void save_stash(const std::filesystem::path &location) const;
    void load_stash(const std::filesystem::path &location);

    void empty_stash(block_call_back callback);
    // bool verify_all_blocks_valid() const;
    protected:
    const uint64_t block_size;
    uint64_t num_blocks_in_stash;
    std::vector<BlockMetadata> metadata;
    bytes_t data_blocks;
};
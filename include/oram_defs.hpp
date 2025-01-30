#pragma once
#include <stdint.h>

#include <limits>
#include <cstdint>
#include <memory_defs.hpp>

/**
 * @brief Metadata for blocks in BinPathOram
 * 
 */
class BlockMetadata {
    private:
    uint64_t block_index;
    uint64_t path;

    public:

    inline BlockMetadata() {
        block_index = 0;
        path = 0;
    }
    
    inline BlockMetadata(uint64_t block_index, uint64_t path, bool valid) {
        this->block_index = block_index;
        this->path = path;

        // if (valid) {
        //     this->path |= (1UL << 63);
        // }

        this->path |= (valid ? (1UL << 63) : 0UL);
    }

    inline uint64_t get_block_index() const noexcept {
        return this->block_index;
    }

    inline uint64_t get_path() const noexcept {
        return this->path & (~(1UL << 63));
    }

    inline bool is_valid() const noexcept {
        return this->path & (1UL << 63);
    }

    inline void invalidate() noexcept {
        this->path &= (~(1UL << 63));
    }

    inline void set_path(uint64_t path) noexcept {
        if (this->is_valid()) {
            path |= (1UL << 63);
        }
        this->path = path;
    }

    inline void set_block_index(uint64_t block_index) noexcept {
        this->block_index = block_index;
    }

    inline bool is_this_block(addr_t block_id) const noexcept {
        return this->is_valid() && (this->get_block_index() == block_id);
    }
};

constexpr uint64_t block_metadata_size = sizeof(BlockMetadata);

struct PathLocation {
    uint64_t level;
    uint64_t block_index;
};

constexpr addr_t INVALID_BLOCK_ID = std::numeric_limits<uint64_t>::max();
constexpr std::size_t POSITION_MAP_ENTRY_SIZE = sizeof(addr_t);
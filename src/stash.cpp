#include <stash.hpp>
#include <cstring>
#include <assert.h>
#include <fstream>
#include <unordered_set>
#include <conditional_memcpy.hpp>
#include <absl/strings/str_format.h>
#include <iostream>

void 
StashEntry::write_to_memory(byte_t *metadata_address, byte_t *data_address) const {
    // write out metadata
    BlockMetadata *metadata_dest = (BlockMetadata *)metadata_address;
    *metadata_dest = this->metadata;
    
    // write out data
    std::memcpy(data_address, this->block.data(), this->block.size());
}

Stash::Stash(uint64_t block_size, uint64_t entries) :
block_size(block_size), num_blocks_in_stash(0),
metadata(entries), data_blocks(block_size * entries)
{}

void 
Stash::add_block(const BlockMetadata *metadata, const byte_t *data) {
    // assert(metadata->is_valid());
    this->num_blocks_in_stash += (metadata->is_valid() ? 1: 0);
    bool done = false;
    for (std::size_t i = 0; i < this->capacity(); i++) {
        bool free = !this->metadata[i].is_valid();
        bool do_copy = (!done) && free;
        conditional_memcpy(do_copy, &this->metadata[i], metadata, block_metadata_size);
        conditional_memcpy(do_copy, this->data_blocks.data() + i * this->block_size, data, this->block_size);
        done = done || free;
    }
    // stash.emplace_back(StashEntry{*metadata, bytes_t(block_size)});
    // std::memcpy(stash.back().block.data(), data, block_size);
    // return &(stash.back());
    if (!done) {
        throw std::runtime_error(absl::StrFormat("Stash Overflow detected, attempting to add new block when stash is already at capacity of %lu blocks", this->metadata.size()));
    }
}

void 
Stash::add_block(const StashEntry &stash_entry) {
    if (stash_entry.block_size() != this->block_size) {
        throw std::runtime_error("Given Stash Entry has mismatched block size");
    }
    this->add_block(&stash_entry.metadata, stash_entry.block.data());
}

// StashEntry * 
// Stash::add_new_block(uint64_t logical_block_address, uint64_t path, uint64_t block_size) {
//     stash.emplace_back(StashEntry{BlockMetadata(logical_block_address, path, true), bytes_t(block_size)});
//     return &stash.back();
// }

bool
Stash::find_and_remove_block(uint64_t logical_block_address, BlockMetadata *metadata, byte_t *data) {
    bool found = false;
    BlockMetadata invalid;
    for (std::size_t i = 0; i < this->capacity(); i++)
    {
        // if (stash[i].metadata.is_valid())
        // {
        //     if (stash[i].metadata.get_block_index() == logical_block_address)
        //     {
        //         return std::optional<StashEntry*>(&stash[i]);
        //     }
        // }
        bool is_target = this->metadata[i].is_this_block(logical_block_address);
        if (is_target && found) {
            throw std::runtime_error(absl::StrFormat("Duplicate blocks detected! a second block with id %lu was found in the stash!", logical_block_address));
        }
        found = found || is_target;
        // copy block to specified location
        conditional_memcpy(is_target, metadata, &this->metadata[i], block_metadata_size);
        conditional_memcpy(is_target, data, this->data_blocks.data() + i * this->block_size, this->block_size);
        // invalidate block in stash
        conditional_memcpy(is_target, &this->metadata[i], &invalid, block_metadata_size);

    }

    this->num_blocks_in_stash -= (found ? 1: 0);

    return found;
}

bool 
Stash::find_and_remove_block(uint64_t logical_block_address, StashEntry &stash_entry) {
    if (stash_entry.block_size() != this->block_size) {
        throw std::runtime_error("Given Stash Entry has mismatched block size");
    }
    return this->find_and_remove_block(logical_block_address, &stash_entry.metadata, stash_entry.block.data());
}

// std::vector<StashEntry>
// Stash::try_evict_blocks(uint64_t max_count , uint16_t ignored_bits, uint64_t path) {
//     path = path >> ignored_bits;
//     std::vector<StashEntry> evicted_blocks;
//     for (auto iter = this->stash.begin(); iter != this->stash.end(); iter++)
//     {
//         if ((*iter).metadata.is_valid())
//         {
//             // std::cout << absl::StrFormat("Stash index %lu, path %lu, comparing_path %lu\n", i, stash[i].metadata.get_path(), (stash[i].metadata.get_path() >> ignored_bits));
//             if (((*iter).metadata.get_path() >> ignored_bits) == path)
//             {      
//                 // check if we reached the limit
//                 if (evicted_blocks.size() < max_count) {
//                     evicted_blocks.emplace_back(std::move(*iter));
//                     (*iter).metadata = BlockMetadata();
//                     assert(!(*iter).metadata.is_valid());

//                     if (evicted_blocks.size() == max_count) {
//                         // if we reached the limit, no need to continue scanning
//                         break;
//                     }
//                 }
//             }
//         }
//     }

//     return evicted_blocks;
// }

std::size_t 
Stash::try_evict_blocks(uint64_t max_count, uint64_t ignored_bits, uint64_t path, BlockMetadata *metadatas, byte_t *data_blocks) {
    std::size_t num_blocks_evicted = 0;
    BlockMetadata invalid;
    for (std::size_t i = 0; i < this->capacity(); i++)
    {
        bool is_eviction_candidate = this->metadata[i].is_valid() && (this->metadata[i].get_path() >> ignored_bits) == (path >> ignored_bits);
        bool do_evict = (num_blocks_evicted < max_count) && is_eviction_candidate;
        // ugly hack-- but needed to prevent writing to beyond the end of the allocated space
        std::size_t offset = num_blocks_evicted == max_count ? max_count - 1: num_blocks_evicted;
        // if (num_blocks_evicted == max_count) {
        //     std::cout << absl::StreamFormat("max_count: %lu, num_blocks_evicted: %lu, offset: %lu\n", max_count, num_blocks_evicted, offset);
        // }
        conditional_memcpy(do_evict, metadatas + offset, &this->metadata[i], block_metadata_size);
        conditional_memcpy(
            do_evict,
            data_blocks + (this->block_size * offset),
            this->data_blocks.data() + i * this->block_size,
            this->block_size
        );

        conditional_memcpy(do_evict, &this->metadata[i], &invalid, block_metadata_size);

        num_blocks_evicted += do_evict ? 1: 0;
    }

    this->num_blocks_in_stash -= num_blocks_evicted;

    return num_blocks_evicted;
}

// void 
// Stash::clear_invalid_blocks() {
//     auto new_end = std::remove_if(this->stash.begin(), this->stash.end(), [](const StashEntry &entry) {
//         return !entry.metadata.is_valid();
//     });
//     this->stash.erase(new_end, this->stash.end());

//     assert(this->verify_all_blocks_valid());
// }

// size_t 
// Stash::size() const {
//     return this->stash.size();
// }

void 
Stash::save_stash(const std::filesystem::path &location) const {
    std::ofstream stash_file(location / "stash.bin", std::ios::out | std::ios::binary);
    for (std::size_t i = 0; i < this->capacity(); i++) {
        stash_file.write((const char *)&this->metadata[i], block_metadata_size);
        stash_file.write((const char *)this->data_blocks.data() + i * this->block_size, this->block_size);
    }
}

void 
Stash::load_stash(const std::filesystem::path &location) {
    if (std::filesystem::is_regular_file(location / "stash.bin")) {
        std::ifstream stash_file(location / "stash.bin", std::ios::in | std::ios::binary);
        BlockMetadata tmp;
        std::size_t i = 0;
        while(stash_file.read((char *) &tmp, block_metadata_size) && i < this->capacity()) {
            this->metadata[i] = tmp;
            stash_file.read((char *)this->data_blocks.data() + i * this->block_size, this->block_size);
            i++;
        }

        for (; i < this->capacity(); i++) {
            // invalidate all further blocks
            this->metadata[i] = BlockMetadata();
        }
    }

    this->num_blocks_in_stash = 0;

    for (std::uint64_t i = 0; i < this->capacity(); i++) {
        this->num_blocks_in_stash += (this->metadata[i].is_valid() ? 1 : 0);
    }
}

// bool 
// Stash::verify_all_blocks_valid() const {
//     std::unordered_set<uint64_t> block_indexes;
//     for (const auto &entry : this->stash) {
//         assert(entry.metadata.is_valid());
//         if (!entry.metadata.is_valid()) {
//             return false;
//         }

//         auto iter = block_indexes.find(entry.metadata.get_block_index());
//         assert(iter == block_indexes.end());
//         block_indexes.emplace(entry.metadata.get_block_index());
//     }

//     return true;
// }

void 
Stash::empty_stash(block_call_back callback) {
    for (std::size_t i = 0; i < this->capacity(); i++)
    {
        callback(&this->metadata[i], this->data_blocks.data() + i * this->block_size);
        this->metadata[i] = BlockMetadata();
    }

    this->num_blocks_in_stash = 0;
}
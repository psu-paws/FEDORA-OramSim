#pragma once
#include <stash.hpp>
#include <cstdint>
#include <memory_defs.hpp>

typedef std::function<std::uint64_t(std::uint64_t)> positionmap_updater;

class LLPathOramInterface {
    public:
        virtual std::uint64_t read_and_update_position_map(uint64_t logical_block_address, uint64_t new_path, bool is_dummy = false) = 0;
        virtual std::uint64_t read_and_update_position_map_function(uint64_t logical_block_address, positionmap_updater updater, bool is_dummy = false) {
            throw std::runtime_error("Not implemented");
        };
        virtual void find_and_remove_block_from_path(BlockMetadata *metadata, byte_t * data) = 0;

        virtual void find_and_remove_block_from_path(StashEntry &entry) {
            find_and_remove_block_from_path(&(entry.metadata), entry.block.data());
        }

        virtual void place_block_on_path(const BlockMetadata *metadata, const byte_t * data) = 0;

        virtual void place_block_on_path(const StashEntry &entry) {
            place_block_on_path(&(entry.metadata), entry.block.data());
        }

        virtual std::uint64_t num_paths() const noexcept = 0;

};
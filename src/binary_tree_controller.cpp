#include <binary_tree_controller.hpp>
#include <algorithm>
#include <absl/strings/str_format.h>
#include <conditional_memcpy.hpp>
#include <algorithm>

UntrustedMemoryController 
UntrustedMemoryController::create( 
    Memory *untrusted_memory, unique_tree_layout_t &&data_layout, unique_tree_layout_t &&metadata_layout, 
    uint64_t levels, uint64_t blocks_per_bucket, uint64_t block_size
) {
    // check if metadata and data can use block addressing
    bool metadata_use_block = (untrusted_memory->page_size() >= block_metadata_size * blocks_per_bucket) && (untrusted_memory->page_size() % (block_metadata_size * blocks_per_bucket) == 0);
    bool data_use_block =(untrusted_memory->page_size() >= block_size * blocks_per_bucket) && (untrusted_memory->page_size() % (block_size * blocks_per_bucket) == 0);

    return UntrustedMemoryController(untrusted_memory, std::move(data_layout), std::move(metadata_layout), levels, blocks_per_bucket, block_size, data_use_block, metadata_use_block);
}

UntrustedMemoryController 
UntrustedMemoryController::create( 
        Memory *untrusted_memory, std::string_view data_layout_type, std::string_view metadata_layout_type, 
        uint64_t levels, uint64_t blocks_per_bucket, uint64_t block_size
) {
    unique_tree_layout_t data_layout = BinaryTreeLayoutFactory::create(data_layout_type, levels, blocks_per_bucket * block_size, untrusted_memory->page_size(), 0UL);
    unique_tree_layout_t metadata_layout = BinaryTreeLayoutFactory::create(metadata_layout_type, levels, blocks_per_bucket * block_metadata_size, untrusted_memory->page_size(), data_layout->size());
    return UntrustedMemoryController::create(
        untrusted_memory,
        std::move(data_layout), std::move(metadata_layout),
        levels, blocks_per_bucket, block_size
    );
}

UntrustedMemoryController 
UntrustedMemoryController::create( 
    Memory *untrusted_memory, const toml::table &table, 
    uint64_t levels, uint64_t blocks_per_bucket, uint64_t block_size
) {
    return UntrustedMemoryController::create(
        untrusted_memory,
        table["data_layout_type"].value<std::string_view>().value(),
        table["metadata_layout_type"].value<std::string_view>().value(),
        levels, blocks_per_bucket, block_size
    );
}


UntrustedMemoryController::UntrustedMemoryController(
    Memory *untrusted_memory, unique_tree_layout_t &&data_layout, unique_tree_layout_t &&metadata_layout, 
    uint64_t levels, uint64_t blocks_per_bucket, uint64_t block_size,
    bool data_use_block_access, bool metadata_use_block_access
) :
data_use_block_access(data_use_block_access),
metadata_use_block_access(metadata_use_block_access),
levels(levels), page_size(untrusted_memory->page_size()),
block_size(block_size), blocks_per_bucket(blocks_per_bucket),
block_access_start((data_use_block_access ? 0 : this->levels) + (metadata_use_block_access ? 0 : this->levels)),
data_non_block_access_start(metadata_use_block_access ? 0 : this->levels),
data_layout(std::move(data_layout)),
metadata_layout(std::move(metadata_layout)),
untrusted_memory(untrusted_memory),
data_page_map(data_use_block_access ? this->levels : 0),
metadata_page_map(metadata_use_block_access ? this->levels : 0)
{}


void 
UntrustedMemoryController::read_path(uint64_t path) {
    this->current_path.emplace(path);
    BlockRequestMap access_map;
    // auto access_map = metadata_layout->get_request_map(path);
    // auto data_map = data_layout->get_request_map(path, this->levels);
    if (this->metadata_use_block_access) {
        access_map = metadata_layout->get_request_map(path);
    }

    if (this->data_use_block_access) {
        for (const auto &entries : data_layout->get_request_map(path, this->levels)) {
            auto iter = access_map.find(entries.first);
            if (iter == access_map.end()) {
                // if the block is not yet in access map
                // add it to the access map
                access_map.emplace(entries.first, std::move(entries.second));
            } else {
                // combine the vector with what is already there
                for (auto &entry : entries.second) {
                    iter->second.emplace_back(entry);
                }
            }
        }
    }

    // prepare the requests
    this->requests.resize(access_map.size() + block_access_start);

    if (!this->metadata_use_block_access) {
        this->metadata_layout->setup_path_access(this->requests.data() + metadata_non_block_access_start, MemoryRequestType::READ, path);
    }

    if (!this->data_use_block_access) {
        this->data_layout->setup_path_access(this->requests.data() + data_non_block_access_start, MemoryRequestType::READ, path);
    }

    uint64_t index = block_access_start;
    for (const auto &iter : access_map) {
        this->requests[index].address = iter.first * this->page_size;
        this->requests[index].size = this->page_size;
        this->requests[index].data.resize(this->page_size);
        this->requests[index].type = MemoryRequestType::READ;

        // populate page map
        for (const auto &entry : iter.second) {
            if (entry.request_id < this->levels) {
                // metadata
                assert(entry.request_offset == 0);
                this->metadata_page_map[entry.request_id] = PageMapEntry(index, entry.block_offset);
            } else {
                // data
                assert(entry.request_offset == 0);
                this->data_page_map[entry.request_id - this->levels] = PageMapEntry(index, entry.block_offset);
            }
        }
        index++;
    }

    // execute the read
    this->untrusted_memory->batch_access(this->requests);
    this->untrusted_memory->barrier();
}

BlockMetadata * 
UntrustedMemoryController::get_metadata_pointer(uint64_t level) {
    if (!this->current_path.has_value()) {
        // no path has been read yet
        throw std::runtime_error("No path has been read yet!");
    }

    if (level >= this->levels) {
        // invalid level
        throw std::runtime_error(absl::StrFormat("Level out of bounds. Expecting [0, %lu), but got %lu", this->levels, level));
    }

    byte_t *ptr;
    if (this->metadata_use_block_access) {
        const auto &entry = this->metadata_page_map[level];
        ptr = this->requests[entry.page_index].data.data() + entry.page_offset;
    } else {
        ptr = this->requests[level + metadata_non_block_access_start].data.data();
    }

    return (BlockMetadata *) ptr;
}

byte_t * 
UntrustedMemoryController::get_data_pointer(uint64_t level) {
    if (!this->current_path.has_value()) {
        // no path has been read yet
        throw std::runtime_error("No path has been read yet!");
    }

    if (level >= this->levels) {
        // invalid level
        throw std::runtime_error(absl::StrFormat("Level out of bounds. Expecting [0, %lu), but got %lu", this->levels, level));
    }

    byte_t *ptr;

    if(data_use_block_access) {
        const auto &entry = this->data_page_map[level];
        ptr = this->requests[entry.page_index].data.data() + entry.page_offset;
    } else {
        ptr = this->requests[level + data_non_block_access_start].data.data();
    }
    
    return ptr;
}

void 
UntrustedMemoryController::write_path() {
     if (!this->current_path.has_value()) {
        // no path has been read yet
        throw std::runtime_error("No path has been read yet!");
    }

    for (auto &request : this->requests) {
        // change all request types to write
        request.type = MemoryRequestType::WRITE;
    }

    // execute the write
    this->untrusted_memory->batch_access(this->requests);
    this->untrusted_memory->barrier();

    // clear the current path
    this->current_path.reset();
}

std::optional<PathLocation> 
UntrustedMemoryController::find_block(uint64_t logical_block_address, std::function<bool(uint64_t, uint64_t)> verifier) const {
    for (uint64_t level = 0; level < this->levels; level++) {
        const auto &page_map_entry = this->metadata_page_map[level];
        const BlockMetadata * metadata = (const BlockMetadata*)(this->requests[page_map_entry.page_index].data.data() + page_map_entry.page_offset);
        for (uint64_t slot_index = 0; slot_index < this->blocks_per_bucket; slot_index++) {
            if (metadata[slot_index].is_valid() && metadata[slot_index].get_block_index() == logical_block_address && verifier(level, slot_index)) {
                return std::optional<PathLocation>({level, slot_index});
            }
        }
    }

    return std::optional<PathLocation>();
}

bool 
UntrustedMemoryController::find_and_remove_block(uint64_t logical_block_address, BlockMetadata *metadata_buffer, byte_t * data_block_buffer) {
    bool found = false;
    BlockMetadata invalid;
    for (uint64_t level = 0; level < this->levels; level++) {
        BlockMetadata * metadata = this->get_metadata_pointer(level);
        byte_t * data_blocks = this->get_data_pointer(level);
        for (uint64_t slot_index = 0; slot_index < this->blocks_per_bucket; slot_index++) {
            bool is_target = metadata[slot_index].is_valid() && metadata[slot_index].get_block_index() == logical_block_address;
            if (is_target && found) {
                throw std::runtime_error(absl::StrFormat("Duplicate block with the same id detected, id is %lu\n", logical_block_address));
            }
            found = found || is_target;

            conditional_memcpy(is_target, metadata_buffer, &metadata[slot_index], block_metadata_size);
            conditional_memcpy(is_target, data_block_buffer, data_blocks + this->block_size * slot_index, this->block_size);

            // invalidate metadata
            conditional_memcpy(is_target, &metadata[slot_index], &invalid, block_metadata_size);
        }
    }

    return found;
}

std::size_t 
UntrustedMemoryController::try_evict_blocks(uint64_t max_count, uint64_t ignored_bits, BlockMetadata *metadatas, byte_t *data_blocks, uint64_t level_limit) {
    assert(this->current_path.has_value());
    std::size_t num_blocks_evicted = 0;
    BlockMetadata invalid;
    for (uint64_t i = 0; i < this->levels && i <= level_limit; i++) {
        uint64_t level = std::min(this->levels - 1, level_limit) - i;
        BlockMetadata * metadata = this->get_metadata_pointer(level);
        byte_t * current_level_data_blocks = this->get_data_pointer(level);
        for (uint64_t slot_index = 0; slot_index < this->blocks_per_bucket; slot_index++) {
            bool is_eviction_candidate = metadata[slot_index].is_valid() && (metadata[slot_index].get_path() >> ignored_bits) == (this->current_path.value() >> ignored_bits);
            bool do_evict = (num_blocks_evicted < max_count) && is_eviction_candidate;
            std::size_t offset = std::min(num_blocks_evicted, max_count - 1);
            conditional_memcpy(do_evict, metadatas + offset, &metadata[slot_index], block_metadata_size);
            conditional_memcpy(
                do_evict,
                data_blocks + (this->block_size * offset),
                current_level_data_blocks + slot_index * this->block_size,
                this->block_size
            );

            conditional_memcpy(do_evict, &metadata[slot_index], &invalid, block_metadata_size);

            num_blocks_evicted += (do_evict ? 1: 0);
        }
    }
    return num_blocks_evicted;
}

toml::table 
UntrustedMemoryController::to_toml() const {
    return toml::table{
        {"data_layout_type", this->data_layout->type()},
        {"metadata_layout_type", this->metadata_layout->type()}
    };
}
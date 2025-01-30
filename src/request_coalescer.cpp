#include "request_coalescer.hpp"

#include <assert.h>
#include <iostream>
#include <absl/strings/str_format.h>

RequestCoalescer::RequestCoalescer(uint64_t block_size) : block_size(block_size){};

void 
RequestCoalescer::add_request(uint64_t request_id, MemoryRequest &request) {
    this->add_request(request_id, request.address, request.size);
}

void 
RequestCoalescer::add_request(uint64_t request_id, uint64_t request_address, uint64_t request_size) {
    if (request_size == 0UL) {
        // abort if request size is 0
        return;
    }

    uint64_t start_block_index = request_address / this->block_size;
    uint64_t end_block_index = (request_address + request_size - 1) / this->block_size;

    uint64_t request_offset = 0UL;
    for (uint64_t block_index = start_block_index; block_index <= end_block_index; block_index++) {
        uint64_t block_offset, size;

        if (block_index == start_block_index) {
            // first block!
            block_offset = request_address - (block_index * this->block_size);
        } else {
            // all subsequent blocks should have a value of zero.
            block_offset = 0UL;
        }

        if (block_index == end_block_index) {
            // last block!
            size = request_size - request_offset;
        } else {
            // all other blocks 
            size = this->block_size - block_offset;
        }
        
        // std::cout << absl::StrFormat("Addr: 0x%08x B_index: 0x%04x B_off: %d, R_off: %d, size: %d\n", request.address, block_index, block_offset, request_offset, size);
        this->add_block_request(request_id, block_index, block_offset, request_offset, size);

        request_offset += size;
    }
}

void 
RequestCoalescer::add_block_request(uint64_t request_id, uint64_t block_index, uint64_t block_offset, uint64_t request_offset, uint64_t size) {
    auto block_iter = this->block_request_map.find(block_index);
    if (block_iter == this->block_request_map.end()) {
        auto emplace_result = this->block_request_map.emplace(block_index, std::vector<BlockRequestEntry>());
        assert(emplace_result.second); // this should always succeed
        block_iter = emplace_result.first;
    }

    // push request onto the map
    block_iter->second.emplace_back(request_id, block_offset, request_offset, size);
}

const BlockRequestMap &
RequestCoalescer::get_map_ref() const {
    return this->block_request_map;
}

BlockRequestMap 
RequestCoalescer::get_map() {
    BlockRequestMap return_map(std::move(this->block_request_map));
    this->block_request_map = BlockRequestMap();
    return return_map;
}
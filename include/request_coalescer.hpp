#pragma once

#include <memory_interface.hpp>
#include <map>

struct BlockRequestEntry {
    const uint64_t request_id; // some id to identify the request
    const uint64_t block_offset; // where data is with respect to the block
    const uint64_t request_offset; // where data is with respect to the request
    const uint64_t size; // size of the request in this block

    BlockRequestEntry(uint64_t request_id, uint64_t block_offset, uint64_t request_offset, uint64_t size) :
    request_id(request_id), block_offset(block_offset), request_offset(request_offset), size(size){};
};

typedef std::map<uint64_t, std::vector<BlockRequestEntry>> BlockRequestMap;

class RequestCoalescer final {
    public:
        RequestCoalescer(uint64_t block_size);
        void add_request(uint64_t request_id, MemoryRequest &request);
        void add_request(uint64_t request_id, uint64_t request_address, uint64_t request_size);
        const BlockRequestMap &get_map_ref() const;
        BlockRequestMap get_map();
    private:
        void add_block_request(uint64_t request_id, uint64_t block_index, uint64_t block_offset, uint64_t request_offset, uint64_t size);
    private:
        const uint64_t block_size;
        BlockRequestMap block_request_map;
};
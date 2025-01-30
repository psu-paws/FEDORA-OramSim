#include "binary_tree_layout.hpp"
#include "util.hpp"
#include <iostream>
#include <absl/strings/str_format.h>

void
BinaryTreeLayout::setup_path_access(MemoryRequest *requests, MemoryRequestType request_type, uint64_t path) const {
    for (uint64_t level = 0; level < this->levels; level++) {
        requests[level].type = request_type;
        requests[level].address = this->get_address(path, level);
        requests[level].size = this->bucket_size;
        requests[level].data.resize(this->bucket_size);
    }
}

BlockRequestMap 
BinaryTreeLayout::get_request_map(uint64_t path, uint64_t index_offset) {
    RequestCoalescer coalescer(this->page_size);
    for (uint64_t level = 0; level < this->levels; level++) {
        uint64_t address = this->get_address(path, level);
        coalescer.add_request(level + index_offset, address, this->bucket_size);
    }

    return coalescer.get_map();
}

std::unordered_map<std::string, unique_tree_layout_t (*)(uint64_t, uint64_t, uint64_t, uint64_t)> BinaryTreeLayoutFactory::tree_layout_creator_map = {
    {"BasicHeapLayout", BasicHeapLayout::create},
    {"TwoLevelHeapLayout", TwoLevelHeapLayout::create}
};

unique_tree_layout_t 
BinaryTreeLayoutFactory::create(std::string_view type, uint64_t levels, uint64_t bucket_size, uint64_t page_size, uint64_t offset) {
    auto creator_iter = tree_layout_creator_map.find(std::string(type));

    if (creator_iter == tree_layout_creator_map.end()) {
        // type was not found
        throw std::runtime_error(absl::StrFormat("Attempted to load unknown BinaryTreeLayout type %s!", type));
    }

    auto creator = creator_iter->second;
    return creator(levels, bucket_size, page_size, offset);
}

// unique_tree_layout_t 
// TreeAddressFactory::from_toml(const toml::table &table) {
//     std::string type = table["type"].value<std::string>().value();

//     auto loader_iter = tree_address_generator_loader_map.find(type);

//     if (loader_iter == tree_address_generator_loader_map.end()) {
//         // loader was not found
//         throw std::runtime_error(absl::StrFormat("Attempted to load unknown BinaryTreeLayout type %s!", type));
//     }

//     auto loader = loader_iter->second;
//     return loader(table);
// }

// unique_tree_layout_t 
// BasicHeapLayout::from_toml(const toml::table &table) {
//     uint64_t levels = parse_size(*(table["levels"].node()));
//     uint64_t bucket_size = parse_size(*(table["bucket_size"].node()));
//     uint64_t offset = parse_size(*(table["offset"].node()));

//     return unique_tree_layout_t(new BasicHeapLayout("", levels, bucket_size, offset));
// }

unique_tree_layout_t 
BasicHeapLayout::create(uint64_t levels, uint64_t bucket_size, uint64_t page_size, uint64_t offset) {
    return unique_tree_layout_t(new BasicHeapLayout("BasicHeapLayout", levels, bucket_size, page_size, offset));
}

BasicHeapLayout::BasicHeapLayout(std::string_view type, uint64_t levels, uint64_t bucket_size, uint64_t page_size, uint64_t offset) : 
BinaryTreeLayout(type, levels, bucket_size, page_size), offset(offset)
{}

uint64_t 
BasicHeapLayout::get_address(uint64_t path, uint64_t level) const {
    return (heap_get_index(level, path >> (this->levels - level - 1)) * this->bucket_size) + this->offset;
}

uint64_t 
BasicHeapLayout::size() const {
    return (1UL << this->levels) * this->bucket_size;
};

// toml::table 
// BasicHeapLayout::to_toml() const {
//     return toml::table{
//         {"type", "BasicHeapGenerator"},
//         {"levels", size_to_string(this->levels)},
//         {"bucket_size", size_to_string(this->bucket_size)},
//         {"offset", size_to_string(this->offset)}
//     };
// }

unique_tree_layout_t 
TwoLevelHeapLayout::create(uint64_t levels, uint64_t bucket_size, uint64_t page_size, uint64_t offset) {
    uint64_t num_bucket_per_page = page_size / bucket_size;
    uint64_t levels_per_page = num_bits(num_bucket_per_page) - 1;

    uint64_t first_page_levels = levels % levels_per_page;
    if (first_page_levels == 0) {
        first_page_levels = levels_per_page;
    }

    uint64_t num_page_levels = divide_round_up(levels, levels_per_page);

    std::vector<uint64_t> page_level_offsets = {0};

    uint64_t page_offset = 0;
    uint64_t page_count = 1;
    for (uint64_t i = 1; i < num_page_levels; i++) {
        page_offset += page_count * page_size;
        page_level_offsets.emplace_back(page_offset);
        
        if(i == 1) {
            page_count = page_count << first_page_levels;
        } else {
            page_count = page_count << levels_per_page;
        }
    }
    page_level_offsets.emplace_back(page_offset + page_count * page_size);

    return unique_tree_layout_t(new TwoLevelHeapLayout(
        "TwoLevelHeapLayout",
        levels, bucket_size, page_size, offset,
        num_page_levels, levels_per_page, first_page_levels,
        std::move(page_level_offsets)
    ));
    
}

// unique_tree_layout_t 
// TwoLevelHeapLayout::from_toml(const toml::table &table) {
//     uint64_t levels = parse_size(*(table["levels"].node()));
//     uint64_t bucket_size = parse_size(*(table["bucket_size"].node()));
//     uint64_t page_size = parse_size(*(table["page_size"].node()));
//     uint64_t offset = parse_size(*(table["offset"].node()));

//     return TwoLevelHeapLayout::create(levels, bucket_size, page_size, offset);
// }

TwoLevelHeapLayout::TwoLevelHeapLayout(
    std::string_view type,
    uint64_t levels, uint64_t bucket_size, uint64_t page_size, uint64_t offset, 
    uint64_t num_page_levels, uint64_t levels_per_page, uint64_t first_page_levels,
    std::vector<uint64_t> &&page_level_offsets
) : 
BinaryTreeLayout(type, levels, bucket_size, page_size), offset(offset),
num_page_levels(num_page_levels), levels_per_page(levels_per_page), first_page_levels(first_page_levels),
page_level_offsets(std::move(page_level_offsets))
{
}

uint64_t 
TwoLevelHeapLayout::get_address(uint64_t path, uint64_t level) const {
    // check if bucket in first page
    if (level < this->first_page_levels) {
        // nothing special here 
        return (heap_get_index(level, path >> (this->levels - level - 1)) * this->bucket_size) + this->offset;
    }

    // figure out which page level this is on
    uint64_t target_page_level = ((level - this->first_page_levels) / this->levels_per_page) + 1;
    uint64_t target_page_level_top_level = ((target_page_level - 1) * this->levels_per_page) + this->first_page_levels;
    uint64_t target_page_index = path >> (this->levels - target_page_level_top_level - 1);

    uint64_t target_page_offset = target_page_index * this->page_size + this->page_level_offsets[target_page_level];

    uint64_t intra_page_level = level - target_page_level_top_level;
    uint64_t intra_page_tree_width_at_level = 1UL << intra_page_level;
    uint64_t intra_page_tree_index = (path >> (this->levels - level - 1)) % intra_page_tree_width_at_level;

    uint64_t address = heap_get_index(intra_page_level, intra_page_tree_index) * this->bucket_size + target_page_offset + this->offset;

    // std::cout << absl::StrFormat("Path %lu Level %lu Address 0x%08lX\n", path, level, address);

    return address;
}

void 
TwoLevelHeapLayout::setup_path_access(MemoryRequest *requests, MemoryRequestType request_type, uint64_t path) const {
    uint64_t level = 0;
    for (uint64_t page_level = 0; page_level < this->num_page_levels; page_level++) {
        uint64_t page_offset = (path >> (this->levels - 1 - level)) * this->page_size + this->page_level_offsets[page_level];
        uint64_t intra_page_levels = page_level == 0 ? this->first_page_levels : this->levels_per_page;
        uint64_t intra_page_path = (path >> (this->levels - level - intra_page_levels)) & ((1UL << (intra_page_levels - 1)) - 1);
        for (uint64_t intra_page_level = 0; intra_page_level < intra_page_levels; intra_page_level++) {
            uint64_t intra_page_address = heap_get_index(intra_page_level, intra_page_path >> (intra_page_levels - 1 - intra_page_level)) * this->bucket_size;
            uint64_t address = intra_page_address + page_offset + this->offset;
            // uint64_t expected_address = this-> get_address(path, level);
            // assert(address == expected_address);
            requests[level].type = request_type;
            requests[level].address = address;
            requests[level].size = this->bucket_size;
            requests[level].data.resize(this->bucket_size);
            level++;
        }
    }
}

uint64_t 
TwoLevelHeapLayout::size() const {
    return this->page_level_offsets.back();
}

// toml::table 
// TwoLevelHeapLayout::to_toml() const {
//     return toml::table{
//         {"type", "TwoLevelHeapGenerator"},
//         {"levels", size_to_string(this->levels)},
//         {"bucket_size", size_to_string(this->bucket_size)},
//         {"page_size", size_to_string(this->page_size)},
//         {"offset", size_to_string(this->offset)}
//     };
// }
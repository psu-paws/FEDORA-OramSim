#include "oram.hpp"
#include "util.hpp"
#include "memory_loader.hpp"

#include <stdexcept>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <unordered_set>
#include <unordered_set>
#include <memory_adapters.hpp>
#include <absl/strings/str_format.h>

void 
BinaryPathOramStatistics::clear() {
    this->MemoryStatistics::clear();
    this->max_stash_size = 0;
    this->path_reads = 0;
    this->path_writes = 0;
    
    this->overall_time = std::chrono::nanoseconds::zero();
    this->path_read_time = std::chrono::nanoseconds::zero();
    this->path_write_time = std::chrono::nanoseconds::zero();
    this->position_map_access_time = std::chrono::nanoseconds::zero();
    this->stash_access_time = std::chrono::nanoseconds::zero();
    this->crypto_time = std::chrono::nanoseconds::zero();
    this->path_scan_time = std::chrono::nanoseconds::zero();
    this->valid_bit_tree_time = std::chrono::nanoseconds::zero();
}

toml::table 
BinaryPathOramStatistics::to_toml() const {
    auto table = this->MemoryStatistics::to_toml();
    table.emplace("max_stash_size", this->max_stash_size);
    table.emplace("path_reads", this->path_reads);
    table.emplace("path_writes", this->path_writes);
    
    table.emplace("overall_ns", this->overall_time.count());
    table.emplace("path_read_ns", this->path_read_time.count());
    if (this->path_reads != 0) {
        table.emplace("ns_per_path_read", this->path_read_time.count() / this->path_reads);
    }
    table.emplace("path_write_ns", this->path_write_time.count());

    if (this->path_writes != 0) {
        table.emplace("ns_per_path_write", this->path_write_time.count() / this->path_writes);
    }
    table.emplace("position_map_access_ns", this->position_map_access_time.count());
    table.emplace("stash_access_ns", this->stash_access_time.count());
    table.emplace("crypto_ns", this->crypto_time.count());
    table.emplace("path_scan_ns", this->path_scan_time.count());
    table.emplace("valid_bit_tree_ns", this->valid_bit_tree_time.count());
    #ifdef PROFILE_STASH_LOAD
    auto stash_list = toml::array();
    for (auto &stash_size : this->stash_load) {
        stash_list.emplace_back(stash_size);
    }
    table.emplace("stash_load", std::move(stash_list));
    #endif

    #ifdef PROFILE_TREE_LOAD
    auto tree_load_list = toml::array();
    for (auto &tree_load_entry: tree_load) {
        auto tree_load_entry_list = toml::array();
        for (auto &value : tree_load_entry) {
            tree_load_entry_list.emplace_back(value);
        }
        tree_load_list.emplace_back(std::move(tree_load_entry_list));
    }
    table.emplace("tree_load", std::move(tree_load_list));
    #endif
    return table;
}

void 
BinaryPathOramStatistics::from_toml(const toml::table &table) {
    this->MemoryStatistics::from_toml(table);
    this->max_stash_size = table["max_stash_size"].value<int64_t>().value();
    this->path_reads = table["path_reads"].value<int64_t>().value();
    this->path_writes = table["path_writes"].value<int64_t>().value();
}

void 
BinaryPathOramStatistics::log_stash_size(uint64_t stash_size) {
    #ifdef PROFILE_STASH_LOAD
        this->stash_load.emplace_back(stash_size);
    #endif
    if (stash_size > static_cast<uint64_t>(this->max_stash_size)){
        this->max_stash_size = stash_size;
    }
}

void 
BinaryPathOramStatistics::log_tree_load(std::vector<int64_t> &&tree_load) {
    #ifdef PROFILE_TREE_LOAD
        this->tree_load.emplace_back(std::move(tree_load));
    #endif
}

void 
BinaryPathOramStatistics::increment_path_read() {
    this->path_reads++;
}

void 
BinaryPathOramStatistics::increment_path_write() {
    this->path_writes++;
}

unique_memory_t 
BinaryPathOram::create(
    std::string_view name,
    unique_memory_t &&position_map, unique_memory_t &&untrusted_memory, 
    uint64_t block_size, uint64_t levels, uint64_t blocks_per_bucket, 
    uint64_t num_blocks,
    unique_tree_layout_t &&data_address_gen,
    unique_tree_layout_t &&metadata_address_gen,
    bool bypass_path_read_on_stash_hit
) {
    return unique_memory_t(new BinaryPathOram(
        "BinaryPathOram", name,
        std::move(position_map), std::move(untrusted_memory),
        std::move(data_address_gen),
        std::move(metadata_address_gen),
        block_size, levels, blocks_per_bucket,
        num_blocks, bypass_path_read_on_stash_hit,
        new BinaryPathOramStatistics()
    ));
}

unique_memory_t 
BinaryPathOram::create(
    std::string_view name,
    unique_memory_t &&position_map, unique_memory_t &&untrusted_memory, 
    uint64_t block_size, uint64_t levels, uint64_t blocks_per_bucket, 
    uint64_t num_blocks,
    bool bypass_path_read_on_stash_hit
) {
    unique_tree_layout_t data_address_gen = BinaryTreeLayoutFactory::create("BasicHeapLayout", levels, block_size * blocks_per_bucket, 0);
    unique_tree_layout_t metadata_address_gen = BinaryTreeLayoutFactory::create("BasicHeapLayout", levels, block_metadata_size * blocks_per_bucket, data_address_gen->size());
    return BinaryPathOram::create(
        name, std::move(position_map), std::move(untrusted_memory),
        block_size, levels, blocks_per_bucket, num_blocks,
        std::move(data_address_gen),
        std::move(metadata_address_gen) 
    );
}

BinaryPathOram::BinaryPathOram(
    std::string_view type, std::string_view name,
    unique_memory_t &&position_map, unique_memory_t &&untrusted_memory,
    unique_tree_layout_t data_address_gen,
    unique_tree_layout_t metadata_address_gen,
    uint64_t block_size, uint64_t levels, uint64_t blocks_per_bucket, 
    uint64_t num_blocks, bool bypass_path_read_on_stash_hit,
    BinaryPathOramStatistics *statistics
    ) : 
    Memory(type, name, block_size * num_blocks, statistics),
    position_map(std::move(position_map)), untrusted_memory(std::move(untrusted_memory)),
    block_size(block_size),
    block_size_bits(num_bits(block_size - 1)),
    blocks_per_bucket(blocks_per_bucket),
    levels(levels), num_blocks(num_blocks),
    total_data_size((1UL << levels) * block_size * blocks_per_bucket), // this is technically 1 more bucket than there are but it is nice keeping everything power of two
    total_meta_data_size((1UL << levels) * sizeof(BlockMetadata) * blocks_per_bucket), // same here
    bypass_path_read_on_stash_hit(bypass_path_read_on_stash_hit),
    stash(block_size, levels),
    untrusted_memory_controller(
        UntrustedMemoryController::create(
            this->untrusted_memory.get(), 
            std::move(data_address_gen), std::move(metadata_address_gen), 
            this->levels, this->blocks_per_bucket, this->block_size
        )
    ),
    oram_statistics(statistics),
    eviction_metadata_buffer(this->blocks_per_bucket),
    eviction_data_block_buffer(this->blocks_per_bucket * this->block_size)
{
    std::cout << absl::StrFormat("Binary Path ORAM requires %lu bytes, of which %lu is data and %lu is metadata.", total_data_size + total_meta_data_size, total_data_size, total_meta_data_size) << std::endl;
}

BinaryPathOram::BinaryPathOram(
    std::string_view type, const toml::table &table, 
    unique_memory_t &&position_map, unique_memory_t &&untrusted_memory,
    BinaryPathOramStatistics *statistics
) : 
Memory(type, table, parse_size(table["num_blocks"]) * parse_size(table["block_size"]), statistics),
position_map(std::move(position_map)), untrusted_memory(std::move(untrusted_memory)),
block_size(parse_size(table["block_size"])),
block_size_bits(num_bits(block_size - 1)), 
blocks_per_bucket(parse_size(table["blocks_per_bucket"])),
levels(parse_size(table["levels"])), 
num_blocks(parse_size(table["num_blocks"])),
total_data_size((1UL << levels) * block_size * blocks_per_bucket), // this is technically 1 more bucket than there are but it is nice keeping everything power of two
total_meta_data_size((1UL << levels) * sizeof(BlockMetadata) * blocks_per_bucket), // same here
bypass_path_read_on_stash_hit(table["bypass_path_read_on_stash_hit"].value_or(false)),
stash(block_size, levels),
untrusted_memory_controller(
    UntrustedMemoryController::create(
        this->untrusted_memory.get(), *(table["untrusted_memory_controller"].as_table()), 
        this->levels, this->blocks_per_bucket, this->block_size
    )
),
oram_statistics(statistics),
eviction_metadata_buffer(this->blocks_per_bucket),
eviction_data_block_buffer(this->blocks_per_bucket * this->block_size)
{}

void
BinaryPathOram::init() {
    this->position_map->init();
    this->untrusted_memory->init();

    // this->stash.clear();

    for (uint64_t i = 0; i < num_blocks; i++) {
        MemoryRequest position_map_write = {MemoryRequestType::WRITE, i * 8, 8, bytes_t(8)};
        uint64_t *path = (uint64_t *)position_map_write.data.data();
        *path = absl::Uniform(this->bit_gen, 0UL, (1UL << (this->levels - 1)));
        if (i % 1000000UL == 0) {
            std::cout << absl::StrFormat("Writing initial value %lu to position map entry %lu\n", *path, i);
        }
        this->position_map->access(position_map_write);
    }
}

uint64_t
BinaryPathOram::size() const
{
    return this->num_blocks * this->block_size;
}

uint64_t 
BinaryPathOram::page_size() const {
    return this->block_size;
}

bool BinaryPathOram::isBacked() const
{
    return this->untrusted_memory->isBacked();
}

uint64_t
BinaryPathOram::read_and_update_position_map(uint64_t logical_block_address, uint64_t new_path) {
    if (this->position_map->is_request_type_supported(MemoryRequestType::READ_WRITE)) {
        MemoryRequest position_map_update = {MemoryRequestType::READ_WRITE, logical_block_address * 8, 8, bytes_t(8)};
        *((uint64_t *)position_map_update.data.data()) = new_path;
        this->position_map->access(position_map_update);
        return *((uint64_t *) position_map_update.data.data());
    } else {
        MemoryRequest position_map_read = {MemoryRequestType::READ, logical_block_address * 8, 8, bytes_t()};
        this->position_map->access(position_map_read);

        MemoryRequest position_map_update = {MemoryRequestType::WRITE, logical_block_address * 8, 8, bytes_t(8)};
        *((uint64_t *)position_map_update.data.data()) = new_path;
        this->position_map->access(position_map_update);
        return *((uint64_t *) position_map_read.data.data());
    }
}

void BinaryPathOram::access(MemoryRequest &request)
{

    // if (request.size != this->block_size || request.address % block_size != 0)
    // {
    //     throw std::invalid_argument("Binary Path ORAM only supports block accesses");
    // }

    uint64_t logical_block_address = request.address / block_size;
    uint64_t logical_end_block_address = (request.address + request.size - 1UL) / block_size;
    
    if (logical_block_address != logical_end_block_address) {
         throw std::invalid_argument("Binary Path ORAM does not support access across block boundaries!");
    }

    this->Memory::log_request(request);

    uint64_t access_offset = request.address - logical_block_address * block_size;

    this->access_block(request.type, logical_block_address, request.data.data(), access_offset, request.size);

}

void 
BinaryPathOram::access_block(MemoryRequestType request_type, uint64_t logical_block_address, unsigned char *buffer, uint64_t offset, uint64_t length) {
    if (length == UINT64_MAX) {
        length = this->block_size - offset;
    }

    if (offset + length > this->block_size) {
         throw std::invalid_argument("Access crosses block boundaries");
    }

    // generate a new path for the block
    uint64_t new_path = absl::Uniform(this->bit_gen, 0UL, (1UL << (this->levels - 1)));

    // read and update position map
    uint64_t path_index = this->read_and_update_position_map(logical_block_address, new_path);

    // search the stash for the block
    // auto stash_search_result = this->stash.find_block(logical_block_address);
    StashEntry target_block(this->block_size);
    bool block_found = this->stash.find_and_remove_block(logical_block_address, target_block);

    bool bypass_path_read = (this->bypass_path_read_on_stash_hit && block_found);

    if (!bypass_path_read) {
        // read path
        this->untrusted_memory_controller.read_path(path_index);

        // place a barrier between read path and write path
        this->untrusted_memory->barrier();

        // // do a second search now the path read is complete
        // if (!stash_hit) {
        //     stash_search_result = this->stash.find_block(logical_block_address);
        // }
        
        // search and remove the block from path buffer;
        block_found = block_found || this->untrusted_memory_controller.find_and_remove_block(logical_block_address, target_block);

        if (!block_found)
        {
            // block has not been found
            if (request_type != MemoryRequestType::WRITE)
            {
                throw std::runtime_error(absl::StrFormat("Can not read block %lu, block does not exist in ORAM!", logical_block_address));
            }

            // create block
            // stash.emplace_back(StashEntry{BlockMetadata(logical_block_address, new_path, true), bytes_t(this->block_size)});
            // stash_index = stash.size() - 1;
            // entry = this->stash.add_new_block(logical_block_address, new_path, this->block_size);
            target_block.metadata = BlockMetadata(logical_block_address, path_index, true);
        }
    }

    // update block
    target_block.metadata.set_path(new_path);

    bytes_t temp_buffer(request_type == MemoryRequestType::READ_WRITE ? length : 0);

    switch (request_type)
    {
    case MemoryRequestType::READ:
        std::memcpy(buffer, target_block.block.data() + offset, length);
        break;
    case MemoryRequestType::WRITE:
        std::memcpy(target_block.block.data() + offset, buffer, length);
        break;
    case MemoryRequestType::READ_WRITE:
        // copy original contents into temp buffer
        std::memcpy(temp_buffer.data(), target_block.block.data() + offset, length);
        // write new value into block
        std::memcpy(target_block.block.data() + offset, buffer, length);
        // move original contents from temp buffer back into the request
        std::memcpy(buffer, temp_buffer.data(), length);
        break;
    default:
        throw std::invalid_argument("unkown memory request type");
        break;
    }

    // put block into stash
    stash.add_block(target_block);

    // bypass write back
    if (!bypass_path_read) {
        // // write_back
        // this->write_path(path_index);

        // do eviction
        for (uint64_t i = 0; i < this->levels; i++) {
            uint64_t level = this->levels - 1 - i;
            BlockMetadata *metadata_pointer = eviction_metadata_buffer.data();
            byte_t * data_block_pointer = eviction_data_block_buffer.data();

            // first scan current path for eviction
            std::size_t slots_available = this->blocks_per_bucket;
            auto num_evicted_from_path = this->untrusted_memory_controller.try_evict_blocks(slots_available, this->levels - 1 - level, metadata_pointer, data_block_pointer, level);
            metadata_pointer += num_evicted_from_path;
            data_block_pointer += (this->block_size * num_evicted_from_path);
            slots_available -= num_evicted_from_path;

            // then scan the stash
            auto num_evicted_from_stash = this->stash.try_evict_blocks(slots_available, this->levels - 1- level, path_index, metadata_pointer, data_block_pointer);
            metadata_pointer += num_evicted_from_stash;
            data_block_pointer += (this->block_size * num_evicted_from_stash);
            slots_available -= num_evicted_from_stash;
            
            // set any remaining blocks to invalid
            for (uint64_t j = 0; j < slots_available; j++) {
                metadata_pointer[j] = BlockMetadata();
            }

            // copy into the path buffer
            std::memcpy(this->untrusted_memory_controller.get_metadata_pointer(level), this->eviction_metadata_buffer.data(), block_metadata_size * this->blocks_per_bucket);
            std::memcpy(this->untrusted_memory_controller.get_data_pointer(level), this->eviction_data_block_buffer.data(), this->block_size * this->blocks_per_bucket);
        }

        this->untrusted_memory_controller.write_path();

        // barrier after write back
        this->untrusted_memory->barrier();

        // assert(this->stash.verify_all_blocks_valid());
    }
    this->oram_statistics->log_stash_size(this->stash.size());
    // std::cout << absl::StrFormat("After access stash has %lu blocks left.\n", stash.size());
}

// void 
// BinaryPathOram::read_path(uint64_t path) {
//     this->oram_statistics->increment_path_read();
//     this->untrusted_memory_controller.read_path(path);

//     // process path
//     for (uint64_t level = 0; level < levels; level++)
//     {
//         BlockMetadata *metadata = this->untrusted_memory_controller.get_metadata_pointer(level);
//         byte_t *data = this->untrusted_memory_controller.get_data_pointer(level);
//         for (uint64_t block_index = 0; block_index < this->blocks_per_bucket; block_index++)
//         {

//             // read metadata
//             if (metadata[block_index].is_valid())
//             {
//                 // push block onto stash if valid
//                 // stash.emplace_back(StashEntry{BlockMetadata(*metadata), bytes_t(this->block_size)});
//                 // std::memcpy(stash.back().block.data(), block_reads[i].data.data() + (this->block_size * j), this->block_size);
//                 this->stash.add_block(metadata + block_index, data + (this->block_size * block_index), this->block_size);
//             }
//         }
//     }


// }

// void 
// BinaryPathOram::write_path(uint64_t path) {
//     this->oram_statistics->increment_path_write();
//     for (uint64_t i = 0; i < levels; i++)
//     {
//         uint64_t level = levels - i - 1;

//         std::vector<StashEntry> evicted_blocks = this->stash.try_evict_blocks(this->blocks_per_bucket, this->levels - 1 - level, path);
//         BlockMetadata *metadata = this->untrusted_memory_controller.get_metadata_pointer(level);
//         byte_t *data = this->untrusted_memory_controller.get_data_pointer(level);

//         for (uint64_t j = 0; j < this->blocks_per_bucket; j++) {
//             if (j < evicted_blocks.size()) {
//                 evicted_blocks[j].write_to_memory((byte_t *)(metadata + j), data + j * this->block_size);
//             } else {
//                 *(metadata + j) = BlockMetadata();
//             }
//         }
//     }

//     // execute accesses
//     this->untrusted_memory_controller.write_path();

//     this->stash.clear_invalid_blocks();
// }

bool 
BinaryPathOram::is_request_type_supported(MemoryRequestType type) const {
    switch (type)
    {
    case MemoryRequestType::READ:
    case MemoryRequestType::WRITE:
    case MemoryRequestType::READ_WRITE:
        return true;
    
    default:
        return false;
    }
}

toml::table 
BinaryPathOram::to_toml_self() const {
    auto table = this->Memory::to_toml();
    table.emplace("block_size", size_to_string(this->block_size));
    table.emplace("levels", size_to_string(this->levels));
    table.emplace("blocks_per_bucket", size_to_string(this->blocks_per_bucket));
    table.emplace("num_blocks", size_to_string(this->num_blocks));

    // table.emplace("data_address_gen", this->data_address_gen->type());
    // table.emplace("metadata_address_gen", this->metadata_address_gen->type());
    table.emplace("untrusted_memory_controller", this->untrusted_memory_controller.to_toml());
    table.emplace("bypass_path_read_on_stash_hit", this->bypass_path_read_on_stash_hit);
    
    return table;
}

toml::table 
BinaryPathOram::to_toml() const {
    auto table = this->to_toml_self();
    table.emplace("position_map", this->position_map->to_toml());
    table.emplace("untrusted_memory", this->untrusted_memory->to_toml());

    return table;
}

void 
BinaryPathOram::save_to_disk(const std::filesystem::path &location) const {
    // write config file
    {
        std::ofstream config_file(location / "config.txt");
        config_file << "BinaryPathOram # type\n";
        config_file << absl::StrFormat("%lu # block size\n", this->block_size);
        config_file << absl::StrFormat("%lu # levels\n", this->levels);
        config_file << absl::StrFormat("%lu # number of blocks per bucket\n", this->blocks_per_bucket);
        config_file << absl::StrFormat("%lu # number of valid blocks\n", this->num_blocks);
        
        config_file.close();
    }

    {
        std::ofstream config_file(location / "config.toml");
        config_file << this->to_toml_self() << "\n";
    }

    // write out position map
    std::filesystem::path position_map_directory = location / "position_map";
    std::filesystem::create_directory(position_map_directory);
    this->position_map->save_to_disk(position_map_directory);

    // write out untrusted memory
    std::filesystem::path untrusted_memory_directory = location / "untrusted_memory";
    std::filesystem::create_directory(untrusted_memory_directory);
    this->untrusted_memory->save_to_disk(untrusted_memory_directory);

    // write out stash
    this->stash.save_stash(location);

}

unique_memory_t 
BinaryPathOram::load_from_disk(const std::filesystem::path &location, const toml::table &table) {
    unique_memory_t position_map = MemoryLoader::load(location / "position_map");
    unique_memory_t untrusted_memory  = MemoryLoader::load(location / "untrusted_memory");

    BinaryPathOram *oram = new BinaryPathOram(
        "BinaryPathOram", table, std::move(position_map), std::move(untrusted_memory), new BinaryPathOramStatistics()
    );

    oram->stash.load_stash(location);

    return unique_memory_t(oram);
}

unique_memory_t 
BinaryPathOram::load_from_disk(const std::filesystem::path &location) {
    return load_from_disk(location, toml::parse_file((location / "config.toml").string()));

}

void 
BinaryPathOram::start_logging(bool append){
    this->Memory::start_logging();
    this->position_map->start_logging(append);
    this->untrusted_memory->start_logging(append);
    
}

void 
BinaryPathOram::stop_logging() {
    this->Memory::stop_logging();
    this->position_map->stop_logging();
    this->untrusted_memory->stop_logging();
}

void 
BinaryPathOram::reset_statistics(bool from_file) {
    this->Memory::reset_statistics(from_file);
    this->untrusted_memory->reset_statistics(from_file);
    this->position_map->reset_statistics(from_file);
}

void 
BinaryPathOram::save_statistics() {
    this->Memory::save_statistics();
    this->untrusted_memory->save_statistics();
    this->position_map->save_statistics();
}

void 
BinaryPathOram::barrier() {
    this->Memory::barrier();
    this->position_map->barrier();
    this->untrusted_memory->barrier();
}

// unique_memory_t 
// RAWOram::create(
//     std::string_view name,
//     unique_memory_t &&position_map, unique_memory_t &&untrusted_memory, 
//     unique_memory_t &&block_valid_bitfield,
//     uint64_t block_size, uint64_t levels, uint64_t blocks_per_bucket, 
//     uint64_t num_blocks, uint64_t num_accesses_per_eviction
// ) {
//     return RAWOram::create(
//         name, std::move(position_map), std::move(untrusted_memory), std::move(block_valid_bitfield),
//         block_size, levels, blocks_per_bucket,
//         num_blocks, num_accesses_per_eviction,
//         "BasicHeapLayout", "BasicHeapLayout", "BasicHeapLayout"
//     );
// }

// RAWOram::RAWOram(
//     std::string_view type, std::string_view name,
//     unique_memory_t &&position_map, unique_memory_t &&untrusted_memory,
//     unique_memory_t &&block_valid_bitfield,
//     unique_tree_layout_t &&data_address_gen,
//     unique_tree_layout_t &&metadata_address_gen,
//     unique_tree_layout_t &&bitfield_address_gen,
//     uint64_t block_size, uint64_t levels, uint64_t blocks_per_bucket, 
//     uint64_t num_blocks, uint64_t num_accesses_per_eviction,
//     bool bypass_path_read_on_stash_hit,
//     BinaryPathOramStatistics *statistics
// ) : BinaryPathOram(
//     type, name, std::move(position_map), std::move(untrusted_memory),
//     std::move(data_address_gen), std::move(metadata_address_gen),
//     block_size, levels, blocks_per_bucket, num_blocks,
//     bypass_path_read_on_stash_hit,
//     statistics
// ),
// block_valid_bitfield(std::move(block_valid_bitfield)),
// num_accesses_per_eviction(num_accesses_per_eviction),
// bitfield_layout(std::move(bitfield_address_gen)),
// bitfield_requests(this->levels),
// eviction_counter(0),
// access_counter(0)
// {
// }

// RAWOram::RAWOram(
//     std::string_view type, const toml::table &table, 
//     unique_memory_t &&position_map, unique_memory_t &&untrusted_memory,
//     unique_memory_t &&block_valid_bitfield,
//     BinaryPathOramStatistics *statistics
// ) : BinaryPathOram(type, table, std::move(position_map), std::move(untrusted_memory), statistics) ,
// block_valid_bitfield(std::move(block_valid_bitfield)),
// num_accesses_per_eviction(parse_size(table["num_accesses_per_eviction"])),
// bitfield_layout(
//     BinaryTreeLayoutFactory::create(table["bitfield_layout"].value<std::string_view>().value(), this->levels, this->blocks_per_bucket, this->block_valid_bitfield->page_size(), 0UL)
// ),
// bitfield_requests(this->levels),
// eviction_counter(parse_size_or(table["eviction_counter"], 0)),
// access_counter(parse_size_or(table["access_counter"], 0))
// {}

// unique_memory_t 
// RAWOram::create(
//     std::string_view name,
//     unique_memory_t &&position_map, unique_memory_t &&untrusted_memory, 
//     unique_memory_t &&block_valid_bitfield,
//     uint64_t block_size, uint64_t levels, uint64_t blocks_per_bucket, 
//     uint64_t num_blocks, uint64_t num_accesses_per_eviction,
//     std::string_view data_layout_type,
//     std::string_view metadata_layout_type,
//     std::string_view bitfield_layout_type,
//     bool bypass_path_read_on_stash_hit
// ) {
//     unique_tree_layout_t data_layout = BinaryTreeLayoutFactory::create(data_layout_type, levels, block_size * blocks_per_bucket, untrusted_memory->page_size(), 0);
//     unique_tree_layout_t metadata_layout = BinaryTreeLayoutFactory::create(metadata_layout_type, levels, block_metadata_size * blocks_per_bucket, untrusted_memory->page_size(), data_layout->size());
//     unique_tree_layout_t bitfield_layout = BinaryTreeLayoutFactory::create(bitfield_layout_type, levels, blocks_per_bucket, untrusted_memory->page_size(), 0);

//     return unique_memory_t(new RAWOram(
//         "RAWOram", name,
//         std::move(position_map), std::move(untrusted_memory), std::move(block_valid_bitfield), 
//         std::move(data_layout), std::move(metadata_layout), std::move(bitfield_layout),
//         block_size, levels, blocks_per_bucket, num_blocks, num_accesses_per_eviction,
//         bypass_path_read_on_stash_hit,
//         new BinaryPathOramStatistics()
//     ));
// }

// unique_memory_t 
// RAWOram::create(
//         std::string_view name,
//         unique_memory_t &&position_map, unique_memory_t &&untrusted_memory, 
//         unique_memory_t &&block_valid_bitfield,
//         unique_tree_layout_t &&data_layout,
//         unique_tree_layout_t &&metadata_layout,
//         unique_tree_layout_t &&bitfield_layout,
//         uint64_t block_size, uint64_t levels, uint64_t blocks_per_bucket, 
//         uint64_t num_blocks, uint64_t num_accesses_per_eviction,
//         bool bypass_path_read_on_stash_hit
// ) {
//     return unique_memory_t(new RAWOram(
//         "RAWOram", name,
//         std::move(position_map), std::move(untrusted_memory), std::move(block_valid_bitfield), 
//         std::move(data_layout), std::move(metadata_layout), std::move(bitfield_layout),
//         block_size, levels, blocks_per_bucket, num_blocks, num_accesses_per_eviction,
//         bypass_path_read_on_stash_hit,
//         new BinaryPathOramStatistics()
//     ));
// }


// void 
// RAWOram::access_block(MemoryRequestType request_type, uint64_t logical_block_address, unsigned char *buffer, uint64_t offset, uint64_t length) {
//     if (length == UINT64_MAX) {
//         length = this->block_size - offset;
//     }

//     if (offset + length > this->block_size) {
//          throw std::invalid_argument("Access crosses block boundaries");
//     }

//     // generate a new path for the block
//     uint64_t new_path = absl::Uniform(this->bit_gen, 0UL, (1UL << (this->levels - 1)));

//     // read and update position map
//     uint64_t path_index = this->read_and_update_position_map(logical_block_address, new_path);

//     StashEntry target_block(this->block_size);

//     bool stash_result = this->stash.find_and_remove_block(logical_block_address, target_block);
//     bool bypass_path_read = (this->bypass_path_read_on_stash_hit && stash_result);
//     StashEntry *entry;

//     if (!bypass_path_read) {
//         // read path
//         this->read_path(path_index);

//         // int64_t stash_index = this->get_stash_index(logical_block_address);
//         if (!stash_result) {
//             stash_result = this->stash.find_block(logical_block_address);
//         }

//         if(!stash_result) {
//             // didn't find block in the stash....
//             // try the path next
//             auto path_buffer_index = this->untrusted_memory_controller.find_block(logical_block_address, is_block_on_path_valid_bond);
//             if (!path_buffer_index.has_value()) {
//                 // block has not been found
//                 if (request_type != MemoryRequestType::WRITE)
//                 {
//                     throw std::runtime_error(absl::StrFormat("Can not read block %lu, block does not exist in ORAM!", logical_block_address));
//                 }

//                 // create block
//                 entry = stash.add_new_block(logical_block_address, new_path, this->block_size);
//                 // stash.emplace_back(StashEntry{BlockMetadata(logical_block_address, new_path, true), bytes_t(this->block_size)});
//             } else {
//                 // block was in the read path
//                 const auto &path_location = path_buffer_index.value();

//                 // move the block from the path buffer to the stash
//                 entry = stash.add_block(
//                     this->untrusted_memory_controller.get_metadata_pointer(path_location.level) + path_location.block_index,
//                     this->untrusted_memory_controller.get_data_pointer(path_location.level) + path_location.block_index * this->block_size,
//                     this->block_size
//                 );

//                 // mark the block as invalid in the heap
//                 this->bitfield_requests[path_buffer_index.value().level].data[path_buffer_index.value().block_index] = 0;
//             }

//             // stash_index = stash.size() - 1;
//         } else {
//             entry = stash_result.value();
//         }
//     } else {
//         entry = stash_result.value();
//     }

//     // update block
//     entry->metadata.set_path(new_path);

//     bytes_t temp_buffer(request_type == MemoryRequestType::READ_WRITE ? length : 0);

//     switch (request_type)
//     {
//     case MemoryRequestType::READ:
//         std::memcpy(buffer, entry->block.data() + offset, length);
//         break;
//     case MemoryRequestType::WRITE:
//         std::memcpy(entry->block.data() + offset, buffer, length);
//         break;
//     case MemoryRequestType::READ_WRITE:
//         // copy original contents into temp buffer
//         std::memcpy(temp_buffer.data(), entry->block.data() + offset, length);
//         // write new value into block
//         std::memcpy(entry->block.data() + offset, buffer, length);
//         // move original contents from temp buffer back into the request
//         std::memcpy(buffer, temp_buffer.data(), length);
//         break;
//     default:
//         throw std::invalid_argument("unkown memory request type");
//         break;
//     }

//     // bypass bitfield write

//     if(!bypass_path_read) {

//         // write bitfield
//         for(uint64_t i = 0; i < this->levels; i++) {
//             this->bitfield_requests[i].type = MemoryRequestType::WRITE;
//         }
//         this->block_valid_bitfield->batch_access(this->bitfield_requests);
//         assert(this->stash.verify_all_blocks_valid());

//         // increment access counter
//         this->access_counter++;

//         // barrier
//         this->untrusted_memory->barrier();

//         if (this->access_counter >= this->num_accesses_per_eviction) {
//             // check if an eviction need to happen
//             this->eviction_access();
//             this->access_counter = 0;
//         }
//     }
// }

// bool 
// RAWOram::is_block_on_path_valid(uint64_t level, uint64_t block_index) const {
//     return this->bitfield_requests[level].data[block_index] != 0;
// }

// void 
// RAWOram::read_path(uint64_t path, bool to_stash) {
//     this->oram_statistics->increment_path_read();
//     // set up batch access to read path
//     // this doesn't do anything for DRAM based memory but should be better for disk based ones.
   
//     // this->metadata_address_gen->setup_path_access(this->path_access.data(), MemoryRequestType::READ, path);
//     // this->data_address_gen->setup_path_access(this->path_access.data() + this->levels, MemoryRequestType::READ, path);
//     this->bitfield_layout->setup_path_access(this->bitfield_requests.data(), MemoryRequestType::READ, path);

//     // execute the reads
//     // this->untrusted_memory->batch_access(this->path_access);
//     this->untrusted_memory_controller.read_path(path);
//     this->block_valid_bitfield->batch_access(this->bitfield_requests);

//     // MemoryRequest *metadata_reads = this->path_access.data();
//     // MemoryRequest *block_reads = this->path_access.data() + this->levels;

//     if (to_stash) {
//         for (uint64_t level = 0; level < levels; level++) {
//             BlockMetadata *metadata = this->untrusted_memory_controller.get_metadata_pointer(level);
//             byte_t *data = this->untrusted_memory_controller.get_data_pointer(level);
//             for (uint64_t block_index = 0; block_index < this->blocks_per_bucket; block_index++)
//             {

//                 // read metadata
//                 bool is_valid = metadata[block_index].is_valid();
//                 is_valid = is_valid && this->bitfield_requests[level].data[block_index];
//                 if (is_valid) {
//                     // push block onto stash if valid
//                     // stash.emplace_back(StashEntry{BlockMetadata(*metadata), bytes_t(this->block_size)});
//                     // std::memcpy(stash.back().block.data(), block_reads[i].data.data() + (this->block_size * j), this->block_size);
//                     this->stash.add_block(metadata + block_index, data + (this->block_size * block_index), this->block_size);
//                 }
//             }
//         }
//     }
// }

// void 
// RAWOram::eviction_access() {
//     // get actual path by reversing the bits of the index.
//     uint64_t eviction_path = reverse_bits(this->eviction_counter, this->levels - 1);
    
//     // std::cout << absl::StrFormat("EO access on path %lu, counter %lu\n", eviction_path, eviction_counter);

//     // read data independent eviction path and place it onto stash
//     this->read_path(eviction_path, true);

//     #if PROFILE_TREE_LOAD

//     std::vector<int64_t> path_load;

//     for(addr_t level = 0; level < this->levels; level++){
//         uint64_t counter = 0;
//         BlockMetadata *meta = this->untrusted_memory_controller.get_metadata_pointer(level);

//         for (addr_t block_index = 0; block_index < this->blocks_per_bucket; block_index++) {
//             if (meta[block_index].is_valid() && this->bitfield_requests[level].data[block_index]) {
//                 counter++;
//             }
//         }

//         path_load.emplace_back(counter);
//     }

//     this->oram_statistics->log_tree_load(std::move(path_load));

//     #endif

//     this->write_path(eviction_path);

//     // std::cout << absl::StrFormat("After eviction pass there is still %lu blocks in stash\n", this->stash.size());

//     // std::cout << this->eviction_counter << "\n";
//     this->eviction_counter++;
//     if (this->eviction_counter >= (1UL << (this->levels - 1))) {
//         this->eviction_counter = 0;
//     }
// }

// void 
// RAWOram::write_path(uint64_t path) {
//     this->oram_statistics->increment_path_write();
//     // std::cout << "Evicting blocks ";
//     for (uint64_t i = 0; i < levels; i++)
//     {
//         uint64_t level = levels - i - 1;

//         std::vector<StashEntry> evicted_blocks = this->stash.try_evict_blocks(this->blocks_per_bucket, this->levels - 1 - level, path);
//         BlockMetadata *metadata = this->untrusted_memory_controller.get_metadata_pointer(level);
//         byte_t *data = this->untrusted_memory_controller.get_data_pointer(level);

        
//         for (uint64_t j = 0; j < this->blocks_per_bucket; j++) {
//             if (j < evicted_blocks.size()) {
//                 // std::cout << evicted_blocks[j].metadata.get_block_index() << ", ";
//                 evicted_blocks[j].write_to_memory((byte_t *)(metadata + j), data + j * this->block_size);
//                 this->bitfield_requests[level].data[j] = 1;
//             } else {
//                 *(metadata + j) = BlockMetadata();
//                 this->bitfield_requests[level].data[j] = 0;
//             }
//         }

//         // std::cout << "| ";
//     }

//     // std::cout << "\n";

//     // execute accesses
//     // this->untrusted_memory->batch_access(this->path_access);
//     for (auto &request : this->bitfield_requests) {
//         request.type = MemoryRequestType::WRITE;
//     }
//     this->untrusted_memory_controller.write_path();
//     this->block_valid_bitfield->batch_access(this->bitfield_requests);

//     // this->stash.resize(write_index);
//     this->stash.clear_invalid_blocks();
//     this->oram_statistics->log_stash_size(this->stash.size());
// }

// toml::table 
// RAWOram::to_toml_self() const {
//     auto table = this->BinaryPathOram::to_toml_self();
//     table.emplace("num_accesses_per_eviction", size_to_string(this->num_accesses_per_eviction));
//     table.emplace("bitfield_layout", this->bitfield_layout->type());
//     table.emplace("eviction_counter", size_to_string(this->eviction_counter));
//     table.emplace("access_counter", size_to_string(this->access_counter));

//     return table;
// }

// toml::table 
// RAWOram::to_toml() const {
//     auto table = this->BinaryPathOram::to_toml();
//     table.emplace("num_accesses_per_eviction", size_to_string(this->num_accesses_per_eviction));
//     table.emplace("bitfield_layout", this->bitfield_layout->type());
//     table.emplace("eviction_counter", size_to_string(this->eviction_counter));
//     table.emplace("bitfield", this->block_valid_bitfield->to_toml());

//     return table;
// }

// void 
// RAWOram::save_to_disk(const std::filesystem::path &location) const {
//     // write config file
//     std::ofstream config_file(location / "config.toml");
//     config_file << this->to_toml_self() << "\n";
    
//     config_file.close();

//     // write out position map
//     std::filesystem::path position_map_directory = location / "position_map";
//     std::filesystem::create_directory(position_map_directory);
//     this->position_map->save_to_disk(position_map_directory);

//     // write out untrusted memory
//     std::filesystem::path untrusted_memory_directory = location / "untrusted_memory";
//     std::filesystem::create_directory(untrusted_memory_directory);
//     this->untrusted_memory->save_to_disk(untrusted_memory_directory);

//     // write out bitfield
//     std::filesystem::path bitfield_directory = location / "bitfield";
//     std::filesystem::create_directory(bitfield_directory);
//     this->block_valid_bitfield->save_to_disk(bitfield_directory);

//     // save stash
//     this->stash.save_stash(location);

// }

// unique_memory_t 
// RAWOram::load_from_disk(const std::filesystem::path &location, const toml::table &table) {
//     unique_memory_t position_map = MemoryLoader::load(location / "position_map");
//     unique_memory_t untrusted_memory = MemoryLoader::load(location / "untrusted_memory");
//     unique_memory_t bitfield = MemoryLoader::load(location / "bitfield");

//     RAWOram *oram = new RAWOram(
//         "RAWOram", table,
//         std::move(position_map), std::move(untrusted_memory), std::move(bitfield),
//         new BinaryPathOramStatistics()
//     );
//     oram->stash.load_stash(location, oram->block_size);

//     return unique_memory_t(oram);
// }

// unique_memory_t 
// RAWOram::load_from_disk(const std::filesystem::path &location) {
//     auto table = toml::parse_file((location / "config.toml").string());
//     return RAWOram::load_from_disk(location, table);
// }

// void 
// RAWOram::start_logging(bool append){
//     this->BinaryPathOram::start_logging(append);
//     this->block_valid_bitfield->start_logging(append);
// }

// void 
// RAWOram::stop_logging() {
//     this->BinaryPathOram::stop_logging();
//     this->block_valid_bitfield->stop_logging();
// }

// void 
// RAWOram::reset_statistics(bool from_file) {
//     this->BinaryPathOram::reset_statistics(from_file);
//     this->block_valid_bitfield->reset_statistics(from_file);
// }

// void 
// RAWOram::save_statistics() {
//     this->BinaryPathOram::save_statistics();
//     this->block_valid_bitfield->save_statistics();
// }
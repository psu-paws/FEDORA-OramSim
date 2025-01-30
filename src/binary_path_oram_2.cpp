#include <binary_path_oram_2.hpp>
#include <util.hpp>
#include <absl/strings/str_format.h>
#include <cmath>
#include <absl/random/random.h>
#include <memory_loader.hpp>
#include <conditional_memcpy.hpp>

BinaryPathOram2::Parameters
BinaryPathOram2::compute_parameters_known_oram_size(
    uint64_t block_size, uint64_t page_size, uint64_t levels_per_page,
    uint64_t num_blocks, CryptoModule * crypto_module,
    uint64_t min_num_slots, bool strict_bucket_size
) {
    // compute block index size
    std::size_t block_index_size = num_bytes(num_blocks - 1);
    std::cout << absl::StreamFormat("To index %lu blocks would require %lu bytes\n", num_blocks, block_index_size);

    std::size_t auth_tag_size = crypto_module->auth_tag_size();
    std::size_t nonce_size = crypto_module->nonce_size();
    std::size_t random_nonce_bytes = nonce_size - 2 * sizeof(std::uint64_t);

    const std::size_t child_counter_size = (1UL << levels_per_page) * sizeof(std::uint64_t);

    //compute bucket size
    const std::size_t num_buckets_per_page = (1UL << levels_per_page) - 1;
    std::cout << absl::StreamFormat("%lu levels per page means %lu buckets per page\n", levels_per_page, num_buckets_per_page);

    std::size_t bucket_size = (page_size - auth_tag_size - child_counter_size) / num_buckets_per_page;

    std::cout << absl::StreamFormat("Maximum bucket size is %lu\n", bucket_size);


    // preliminary calculation, assume path index is the same size as block index
    std::size_t num_blocks_per_bucket = bucket_size / (block_size + 2 * block_index_size + 1);
    std::cout << absl::StreamFormat("Preliminary blocks per bucket: %lu\n", num_blocks_per_bucket);

    // load factor
    std::size_t required_number_of_slots = min_num_slots;
    std::size_t required_number_of_buckets = divide_round_up(required_number_of_slots, num_blocks_per_bucket);
    std::cout << absl::StreamFormat("To have a minimum of %lu slots the tree needs %lu buckets\n", required_number_of_slots, required_number_of_buckets);

    // compute height of tree
    std::size_t levels = num_bits(required_number_of_buckets);
    std::cout << absl::StreamFormat("Tree height is %lu \n", levels);

    const std::size_t page_levels = divide_round_up(levels, levels_per_page);
    std::cout << absl::StreamFormat("Tree has %lu page levels\n", page_levels);

    // compute path index size
    std::size_t path_index_size = divide_round_up(std::max(1UL, levels - 1) + 1, 8UL);
    std::cout << absl::StreamFormat("Path index size is %lu \n", path_index_size);

    // recompute number of blocks per bucket
    num_blocks_per_bucket = bucket_size / (block_size + block_index_size + path_index_size + 1);
    std::cout << absl::StreamFormat("Each bucket of %lu bytes can hold %lu slots, 2x64-bit child counters, and one %lu byte auth tag\n", bucket_size, num_blocks_per_bucket, auth_tag_size);

    // compute total number of slots
    std::size_t total_number_of_buckets = (1UL << levels) - 1;
    std::size_t total_number_of_slots = total_number_of_buckets * num_blocks_per_bucket;
    double actual_load_factor = static_cast<double>(num_blocks) / static_cast<double>(total_number_of_slots);
    std::cout << absl::StreamFormat("Tree has %lu buckets or %lu total slots, actual load factor is %lf\n", total_number_of_buckets, total_number_of_slots, actual_load_factor);

    if (!strict_bucket_size) {
        bucket_size = (block_size + block_index_size + path_index_size + 1) * num_blocks_per_bucket;
        page_size = bucket_size * num_buckets_per_page + child_counter_size + auth_tag_size;
        std::cout << absl::StreamFormat("Each bucket only needs %lu bytes\n", bucket_size);
        std::cout << absl::StreamFormat("Each page only needs %lu bytes\n", page_size);
    }

    std::size_t page_level_start = 0;
    std::size_t page_level_size = 1;

    for (std::uint64_t i = 0; i < page_levels; i++) {
        page_level_start += page_level_size;
        page_level_size = page_level_size << levels_per_page;
    }

    std::cout << absl::StrFormat("BinaryPathORAM requires %sB of unsafe memory\n", size_to_string(page_level_start * page_size));

    Parameters parameters {
        .block_size = block_size,
        .page_size = page_size,
        .bucket_size = bucket_size,
        .blocks_per_bucket = num_blocks_per_bucket,
        .levels_per_page = levels_per_page,
        .levels = levels,
        .page_levels = page_levels,
        .num_blocks = num_blocks,
        .auth_tag_size = auth_tag_size,
        .random_nonce_bytes = random_nonce_bytes,
        .nonce_size = nonce_size,
        .block_index_size = block_index_size,
        .path_index_size = path_index_size,
        .untrusted_memory_size = page_level_start * page_size
    };

    return parameters;
}

BinaryPathOram2::Parameters 
BinaryPathOram2::compute_parameters(
    uint64_t block_size, uint64_t page_size, uint64_t levels_per_page, 
    uint64_t num_blocks, CryptoModule * crypto_module,
    double max_load_factor, bool strict_bucket_size
) {
    std::uint64_t min_number_of_slots = static_cast<std::uint64_t>(static_cast<double>(num_blocks) / max_load_factor);
    std::cout << absl::StreamFormat("To maintain a max load factor of %lf while holding %lu real blocks, %lu slots are needed\n", max_load_factor, num_blocks, min_number_of_slots);
    return compute_parameters_known_oram_size(
        block_size, page_size, levels_per_page, num_blocks, crypto_module, min_number_of_slots, strict_bucket_size
    );
}

unique_memory_t 
BinaryPathOram2::create(
        std::string_view name,
        unique_memory_t &&position_map, unique_memory_t &&untrusted_memory,
        std::unique_ptr<CryptoModule> &&crypto_module,
        Parameters parameters,
        uint64_t max_stash_size,
        bool bypass_path_read_on_stash_hit
) {
    return std::unique_ptr<Memory>(new BinaryPathOram2(
        name,
        std::move(position_map),
        std::move(untrusted_memory),
        std::move(crypto_module),
        new BinaryPathOramStatistics(),
        parameters,
        max_stash_size,
        bypass_path_read_on_stash_hit
    ));
}


BinaryPathOram2::BinaryPathOram2(
        std::string_view name,
        unique_memory_t &&position_map, unique_memory_t &&untrusted_memory,
        std::unique_ptr<CryptoModule> &&crypto_module,
        BinaryPathOramStatistics *statistics,
        Parameters parameters,
        uint64_t max_stash_size,
        bool bypass_path_read_on_stash_hit
) :
Memory("BinaryPathOram2", name, parameters.block_size * parameters.num_blocks, statistics) ,
position_map(std::move(position_map)),
untrusted_memory(std::move(untrusted_memory)),
crypto_module(std::move(crypto_module)),
parameters(parameters),
metadata_layout(parameters.path_index_size, parameters.block_index_size),
bypass_path_read_on_stash_hit(bypass_path_read_on_stash_hit),
stash(parameters.block_size, max_stash_size),
oram_statistics(statistics),
eviction_metadata_buffer(parameters.blocks_per_bucket),
eviction_data_block_buffer(parameters.blocks_per_bucket * parameters.block_size),
root_counter(0),
// access_counter(0),
nonce_buffer(parameters.nonce_size),
key(this->crypto_module->key_size()),
decrypted_path(parameters.page_levels * parameters.page_size),
currently_loaded_path(std::nullopt)
{
    for (addr_t i = 0; i < this->parameters.page_levels; i++) {
        this->path_access.emplace_back(MemoryRequestType::READ, 0, this->parameters.page_size);
        // this->valid_bitfield_access.emplace_back(MemoryRequestType::READ, 0, this->valid_bits_per_bucket);
    }

    // generate random key
    this->key.resize(this->crypto_module->key_size());
    this->crypto_module->random(this->key.data(), this->crypto_module->key_size());
    this->nonce_buffer.resize(this->crypto_module->nonce_size());
    this->crypto_module->random(this->nonce_buffer.data(), this->parameters.random_nonce_bytes);
    this->decrypted_path.resize(parameters.levels * parameters.bucket_size);
}

BinaryPathOram2::BinaryPathOram2(
    std::string_view type, const toml::table &table, 
    unique_memory_t &&position_map, unique_memory_t &&untrusted_memory,
    BinaryPathOramStatistics *statistics
) :
Memory("BinaryPathOram2", table, parse_size(table["block_size"]) * parse_size(table["num_blocks"]), statistics) ,
position_map(std::move(position_map)),
untrusted_memory(std::move(untrusted_memory)),
crypto_module(get_crypto_module_by_name(table["crypto_module"].value<std::string_view>().value())),
parameters(Parameters {
    .block_size = parse_size(table["block_size"]),
    .page_size = parse_size(table["page_size"]),
    .bucket_size = parse_size(table["bucket_size"]),
    .blocks_per_bucket = parse_size(table["blocks_per_bucket"]),
    .levels_per_page = parse_size(table["levels_per_page"]),
    .levels = parse_size(table["levels"]),
    .page_levels = parse_size(table["page_levels"]),
    .num_blocks = parse_size(table["num_blocks"]),
    .auth_tag_size = parse_size(table["auth_tag_size"]),
    .random_nonce_bytes = parse_size(table["random_nonce_bytes"]),
    .nonce_size = parse_size(table["nonce_size"]),
    .block_index_size = parse_size(table["block_index_size"]),
    .path_index_size = parse_size(table["path_index_size"]),
    .untrusted_memory_size = parse_size(table["untrusted_memory_size"])
}),
metadata_layout(parameters.path_index_size, parameters.block_index_size),
bypass_path_read_on_stash_hit(table["bypass_path_read_on_stash_hit"].value_or("false")),
stash(parameters.block_size, parse_size(table["max_stash_size"])),
oram_statistics(statistics),
eviction_metadata_buffer(parameters.blocks_per_bucket),
eviction_data_block_buffer(parameters.blocks_per_bucket * parameters.block_size),
root_counter(parse_size(table["root_counter"])),
// access_counter(0),
nonce_buffer(parameters.nonce_size),
key(this->crypto_module->key_size()),
decrypted_path(parameters.levels * parameters.page_size),
currently_loaded_path(std::nullopt)
{
    for (addr_t i = 0; i < this->parameters.page_levels; i++) {
        this->path_access.emplace_back(MemoryRequestType::READ, 0, this->parameters.page_size);
        // this->valid_bitfield_access.emplace_back(MemoryRequestType::READ, 0, this->valid_bits_per_bucket);
    }

    this->key.resize(this->crypto_module->key_size());
    hex_string_to_bytes(table["key"].value<std::string_view>().value(), this->key.data(), this->crypto_module->key_size());
    this->nonce_buffer.resize(this->crypto_module->nonce_size());
    hex_string_to_bytes(table["random_nonce"].value<std::string_view>().value(), this->nonce_buffer.data(), this->parameters.random_nonce_bytes);
    this->decrypted_path.resize(this->parameters.bucket_size * this->parameters.levels);
}

void 
BinaryPathOram2::init() {
    this->position_map->init();
    this->untrusted_memory->init();
    MemoryRequest position_map_access(MemoryRequestType::WRITE, 0, this->parameters.path_index_size);

    std::uint64_t position_map_entries_per_page = this->position_map->page_size() / this->parameters.path_index_size;
    for (uint64_t i = 0; i < this->parameters.num_blocks; i++) {
        std::uint64_t page_id = i / position_map_entries_per_page;
        std::uint64_t offset = i % position_map_entries_per_page;
        std::uint64_t address = page_id * this->position_map->page_size() + offset * this->parameters.path_index_size;
        uint64_t path = absl::Uniform(this->bit_gen, 0UL, this->num_paths());
        position_map_access.address = address;
        std::memcpy(position_map_access.data.data(), &path, this->parameters.path_index_size);
        // this->read_and_update_position_map(i, path, false);
        if (i % 1000000UL == 0) {
            std::cout << absl::StrFormat("Writing initial value %lu to position map entry %lu\n", path, i);
        }
        this->position_map->access(position_map_access);
    }

    MemoryRequest untrusted_memory_request(MemoryRequestType::WRITE, 0, this->parameters.page_size);
    bytes_t data_buffer(this->parameters.page_size);

    uint64_t total_pages = this->untrusted_memory->size() / this->parameters.page_size;

    uint64_t page_level_start = 0;
    uint64_t page_level_size = 1;

    for (uint64_t page_level = 0; page_level < this->parameters.page_levels; page_level++)
    {
        for (uint64_t page_level_offset = 0; page_level_offset < page_level_size; page_level_offset++)
        {
            uint64_t page_id = (page_level_start + page_level_offset);
            // uint64_t page_block_id_offset = page_id * this->parameters.blocks_per_bucket;
            untrusted_memory_request.address = page_id * this->parameters.page_size;

            // compute path
            // uint64_t path = level_offset << (this->parameters.levels - 1 - level);

            // clear both memory requests and buffer
            std::memset(untrusted_memory_request.data.data(), 0, untrusted_memory_request.size);
            std::memset(data_buffer.data(), 0, data_buffer.size());

            if ((page_id + 1) % 10000UL == 0) {
                std::cout << absl::StreamFormat("Writing page %lu of %lu\n", page_id + 1, total_pages);
            }

            // compute counter
            // uint64_t counter = 0UL - (1UL << level) + reverse_bits(level_offset, level);
            uint64_t counter = 0;

            // prepare nonce
            std::memcpy(this->nonce_buffer.data() + this->parameters.random_nonce_bytes, &(untrusted_memory_request.address), sizeof(std::uint64_t));
            std::memcpy(this->nonce_buffer.data() + this->parameters.random_nonce_bytes + sizeof(std::uint64_t), &counter, sizeof(std::uint64_t));

            // encrypt page
            this->crypto_module->encrypt(
                this->key.data(),
                this->nonce_buffer.data(),
                data_buffer.data(),
                this->parameters.page_size - this->parameters.auth_tag_size,
                untrusted_memory_request.data.data(),
                untrusted_memory_request.data.data() + this->parameters.page_size - this->parameters.auth_tag_size
            );
            
            // write page
            // this->valid_bit_tree_memory->access(bitfield_request);
            this->untrusted_memory->access(untrusted_memory_request);
        }
        page_level_start += page_level_size;
        page_level_size = page_level_size << this->parameters.levels_per_page;
    }
}

void 
BinaryPathOram2::fast_init() {
    this->root_counter = 0;
    std::cout << "Staring PageOptimizedRAWOram fast initialization\n";
    uint64_t total_buckets = (1UL << this->parameters.levels) - 1;
    uint64_t total_pages = this->untrusted_memory->size() / this->parameters.page_size;
    std::cout << absl::StreamFormat("ORAM has %lu buckets\n", total_buckets);

    uint64_t total_slots = total_buckets * this->parameters.blocks_per_bucket;
    std::cout << absl::StreamFormat("ORAM has %lu slots\n", total_slots);

    std::vector<uint64_t> block_ids;
    block_ids.reserve(total_slots);

    for (uint64_t i = 0; i < total_slots; i++) {
        if (i < this->parameters.num_blocks) {
            block_ids.push_back(i);
        } else {
            block_ids.push_back(INVALID_BLOCK_ID);
        }
    }  


    std::cout << "Shuffling block ids...\n";
    std::ranges::shuffle(block_ids, this->bit_gen);
    std::cout << "Done shuffling block ids\n";

    std::cout << "Initializing Position Map...\n";
    this->untrusted_memory->init();
    this->position_map->init();
    std::cout << "Done initializing position map.\n";

    // MemoryRequest bitfield_request(MemoryRequestType::WRITE, 0, divide_round_up(this->parameters.blocks_per_bucket, 8UL));
    MemoryRequest untrusted_memory_request(MemoryRequestType::WRITE, 0, this->parameters.page_size);
    // MemoryRequest position_map_request(MemoryRequestType::WRITE, 0, POSITION_MAP_ENTRY_SIZE);
    bytes_t data_buffer(this->parameters.page_size);

    uint64_t bucket_metadata_offset = this->parameters.block_size * this->parameters.blocks_per_bucket;
    uint64_t data_size;
    if (this->parameters.block_size >= 8) {
        data_size = 8;
    } else if (this->parameters.block_size >= 4) {
        data_size = 4;
    } else if (this->parameters.block_size >= 2) {
        data_size = 2;
    } else {
        data_size = 1;
    }

    uint64_t page_level_start_offset = 0;
    uint64_t page_level_size = 1;
    std::vector<std::uint64_t>::const_iterator block_id_iter = block_ids.begin();

    MemoryRequest position_map_access(MemoryRequestType::WRITE, 0, this->parameters.path_index_size);
    std::uint64_t position_map_entries_per_page = this->position_map->page_size() / this->parameters.path_index_size;

    for (uint64_t page_level = 0; page_level < this->parameters.page_levels; page_level++)
    {
        std::uint64_t sub_tree_height;
        if (page_level == this->parameters.page_levels - 1) {
            sub_tree_height = this->parameters.levels - page_level * this->parameters.levels_per_page;
        } else {
            sub_tree_height = this->parameters.levels_per_page;
        }

        uint64_t page_path_reminding_bits = (this->parameters.levels - 1 - this->parameters.levels_per_page * page_level);
        for (uint64_t page_offset = 0; page_offset < page_level_size; page_offset++)
        {
            uint64_t page_id = (page_level_start_offset + page_offset);
            // uint64_t page_block_id_offset = page_id * this->parameters.blocks_per_bucket;
            untrusted_memory_request.address = page_id * this->parameters.page_size;

            // compute path
            uint64_t page_path = page_offset << page_path_reminding_bits;
            // uint64_t path_lower_max = 1UL << (this->parameters.levels - 1 - level);

            // clear both memory requests and buffer
            std::memset(untrusted_memory_request.data.data(), 0, untrusted_memory_request.size);
            std::memset(data_buffer.data(), 0, data_buffer.size());
            // std::memset(bitfield_request.data.data(), 0, bitfield_request.size);
            std::uint64_t subtree_level_size = 1;
            std::uint64_t subtree_level_start_offset = 0;
            for (std::uint64_t sub_tree_level = 0; sub_tree_level < sub_tree_height; sub_tree_level++) {
                for (std::uint64_t subtree_offset = 0; subtree_offset < subtree_level_size; subtree_offset++) {
                    byte_t *bucket = data_buffer.data() + (subtree_level_start_offset + subtree_offset) * this->parameters.bucket_size;

                    std::uint64_t bucket_path_remaining_bits = page_path_reminding_bits - sub_tree_level;
                    std::uint64_t bucket_path = page_path | (subtree_offset << bucket_path_remaining_bits);

                    for (uint64_t i = 0; i < this->parameters.blocks_per_bucket; i++)
                    {
                        uint64_t block_id = *block_id_iter;
                        block_id_iter++;
                        if (block_id != INVALID_BLOCK_ID)
                        {
                            // write data block;
                            std::memcpy(bucket + (i * this->parameters.block_size), &block_id, data_size);

                            uint64_t path = bucket_path | absl::Uniform(this->bit_gen, 0UL, 1UL << bucket_path_remaining_bits);

                            // write metadata block;
                            byte_t *metadata = bucket + bucket_metadata_offset + i * this->metadata_layout.metadata_size();
                            // *metadata = BlockMetadata(block_id, path, true);
                            this->metadata_layout.set_valid(metadata, true);
                            this->metadata_layout.set_block_index(metadata, block_id);
                            this->metadata_layout.set_path_index(metadata, path);

                            // write valid bit
                            // auto byte_offset = i / 8;
                            // auto bit_offset = i % 8;
                            // bitfield_request.data[byte_offset] |= (1UL << bit_offset);

                            // write position map
                            // position_map_request.address = block_id * POSITION_MAP_ENTRY_SIZE;
                            // std::memcpy(position_map_request.data.data(), &path, POSITION_MAP_ENTRY_SIZE);
                            // this->position_map->access(position_map_request);
                            std::uint64_t page_id = block_id / position_map_entries_per_page;
                            std::uint64_t offset = block_id % position_map_entries_per_page;
                            std::uint64_t position_map_address = page_id * this->position_map->page_size() + offset * this->parameters.path_index_size;
                            position_map_access.address = position_map_address;
                            std::memcpy(position_map_access.data.data(), &path, this->parameters.path_index_size);
                            this->position_map->access(position_map_access);
                        }
                    }
                }

                subtree_level_start_offset += subtree_level_size;
                subtree_level_size *= 2;
            }

            if ((page_id + 1) % 10000UL == 0) {
                std::cout << absl::StreamFormat("Writing page %lu of %lu\n", page_id + 1, total_pages);
            }

            // compute counter
            // uint64_t counter = 0UL - (1UL << level) + reverse_bits(level_offset, level);
            uint64_t counter = 0;

            // prepare nonce
            std::memcpy(this->nonce_buffer.data() + this->parameters.random_nonce_bytes, &(untrusted_memory_request.address), sizeof(std::uint64_t));
            std::memcpy(this->nonce_buffer.data() + this->parameters.random_nonce_bytes + sizeof(std::uint64_t), &counter, sizeof(std::uint64_t));

            // encrypt page
            this->crypto_module->encrypt(
                this->key.data(),
                this->nonce_buffer.data(),
                data_buffer.data(),
                this->parameters.page_size - this->parameters.auth_tag_size,
                untrusted_memory_request.data.data(),
                untrusted_memory_request.data.data() + this->parameters.page_size - this->parameters.auth_tag_size
            );
            
            // write page
            // this->valid_bit_tree_memory->access(bitfield_request);
            this->untrusted_memory->access(untrusted_memory_request);
        }
        page_level_start_offset += page_level_size;
        page_level_size = page_level_size << this->parameters.levels_per_page;
    }
    // this->valid_bit_tree_controller->encrypt_contents(this->key.data());
}

uint64_t 
BinaryPathOram2::size() const {
    return this->parameters.num_blocks * this->parameters.block_size;
}

bool 
BinaryPathOram2::isBacked() const {
    return this->untrusted_memory->isBacked();
}

uint64_t 
BinaryPathOram2::page_size() const {
    return this->parameters.block_size;
}


void 
BinaryPathOram2::access(MemoryRequest &request) {
    auto start_time = std::chrono::steady_clock::now();
    uint64_t logical_block_address = request.address / this->parameters.block_size;
    uint64_t logical_end_block_address = (request.address + request.size - 1UL) / this->parameters.block_size;
    
    if (logical_block_address != logical_end_block_address) {
         throw std::invalid_argument("Path Optimized ORAM does not support access across block boundaries!");
    }

    this->Memory::log_request(request);

    uint64_t access_offset = request.address - logical_block_address * this->parameters.block_size;

    bool val = this->access_block(request.type, logical_block_address, request, access_offset, request.size);
    const std::uint64_t marker_address = std::numeric_limits<std::uint64_t>::max();
    conditional_memcpy(val, &request.address, &marker_address, sizeof(std::uint64_t));
    auto end_time = std::chrono::steady_clock::now();
    this->oram_statistics->add_overall_time(end_time - start_time);
}

bool 
BinaryPathOram2::is_request_type_supported(MemoryRequestType type) const {
    switch (type)
    {
    case MemoryRequestType::READ:
    case MemoryRequestType::WRITE:
    case MemoryRequestType::READ_WRITE:
    case MemoryRequestType::POP:
    case MemoryRequestType::DUMMY_POP:
    case MemoryRequestType::PUSH:
    case MemoryRequestType::DUMMY_PUSH:
        return true;
        break;
    default:
        return false;
        break;
    }
}

void 
BinaryPathOram2::start_logging(bool append) {
    this->Memory::start_logging(append);
    this->untrusted_memory->start_logging(append);
    this->position_map->start_logging(append);
}

void 
BinaryPathOram2::stop_logging() {
    this->Memory::stop_logging();
    this->untrusted_memory->stop_logging();
    this->position_map->stop_logging();
}

toml::table 
BinaryPathOram2::to_toml_self() const {
    auto table = this->Memory::to_toml();
    table.emplace("block_size", size_to_string(this->parameters.block_size));
    table.emplace("page_size", size_to_string(this->parameters.page_size));
    table.emplace("bucket_size", size_to_string(this->parameters.bucket_size));
    table.emplace("blocks_per_bucket", size_to_string(this->parameters.blocks_per_bucket));
    table.emplace("levels_per_page", size_to_string(this->parameters.levels_per_page));
    table.emplace("levels", size_to_string(this->parameters.levels));
    table.emplace("page_levels", size_to_string(this->parameters.page_levels));
    table.emplace("num_blocks", size_to_string(this->parameters.num_blocks));
    table.emplace("auth_tag_size", size_to_string(this->parameters.auth_tag_size));
    table.emplace("random_nonce_bytes", size_to_string(this->parameters.random_nonce_bytes));
    table.emplace("nonce_size", size_to_string(this->parameters.nonce_size));
    table.emplace("block_index_size", size_to_string(this->metadata_layout.block_index_size));
    table.emplace("path_index_size", size_to_string(this->metadata_layout.path_index_size));
    table.emplace("untrusted_memory_size", size_to_string(this->parameters.untrusted_memory_size));
    table.emplace("max_stash_size", size_to_string(this->stash.capacity()));
    table.emplace("key", bytes_to_hex_string(this->key.data(), this->crypto_module->key_size()));
    table.emplace("crypto_module", this->crypto_module->name());
    table.emplace("random_nonce", bytes_to_hex_string(this->nonce_buffer.data(), this->parameters.random_nonce_bytes));
    table.emplace("root_counter", size_to_string(this->root_counter));
    table.emplace("bypass_path_read_on_stash_hit", this->bypass_path_read_on_stash_hit);
    table.emplace("crypto_module", this->crypto_module->name());

    return table;
}

toml::table 
BinaryPathOram2::to_toml() const {
    auto table = this->to_toml_self();
    table.emplace("position_map", this->position_map->to_toml());
    table.emplace("untrusted_memory", this->untrusted_memory->to_toml());
    // table.emplace("valid_bit_tree_memory", this->valid_bit_tree_memory->to_toml());
    return table;
}

void 
BinaryPathOram2::save_to_disk(const std::filesystem::path &location) const {
    // write config file
    std::ofstream config_file(location / "config.toml");
    config_file << this->to_toml_self() << "\n";
    
    config_file.close();

    // write out position map
    std::filesystem::path position_map_directory = location / "position_map";
    std::filesystem::create_directory(position_map_directory);
    this->position_map->save_to_disk(position_map_directory);

    // write out untrusted memory
    std::filesystem::path untrusted_memory_directory = location / "untrusted_memory";
    std::filesystem::create_directory(untrusted_memory_directory);
    this->untrusted_memory->save_to_disk(untrusted_memory_directory);

    // save stash
    this->stash.save_stash(location);
}

unique_memory_t 
BinaryPathOram2::load_from_disk(const std::filesystem::path &location) {
    auto table = toml::parse_file((location / "config.toml").string());
    return BinaryPathOram2::load_from_disk(location, table);
}

unique_memory_t 
BinaryPathOram2::load_from_disk(const std::filesystem::path &location, const toml::table &table) {
    unique_memory_t position_map = MemoryLoader::load(location / "position_map");
    unique_memory_t untrusted_memory = MemoryLoader::load(location / "untrusted_memory");

    BinaryPathOram2 *oram = new BinaryPathOram2(
        "PageOptimizedRAWOram", table,
        std::move(position_map), std::move(untrusted_memory),
        new BinaryPathOramStatistics()
    );
    oram->stash.load_stash(location);

    return unique_memory_t(oram);
}

void 
BinaryPathOram2::reset_statistics(bool from_file) {
    this->Memory::reset_statistics(from_file);
    this->untrusted_memory->reset_statistics(from_file);
    this->position_map->reset_statistics(from_file);

    // #ifdef PROFILE_TREE_LOAD_EXTENDED
    // std::filesystem::path extended_tree_load_out_path(absl::StrFormat("%s_extended.log", this->name));
    // this->extended_tree_load_out.open(extended_tree_load_out_path);
    // this->extended_tree_load_log_counter = 0;
    // #endif
}

void 
BinaryPathOram2::save_statistics() {
    this->Memory::save_statistics();
    this->untrusted_memory->save_statistics();
    // this->valid_bit_tree_memory->save_statistics();
    this->position_map->save_statistics();

    // #ifdef PROFILE_TREE_LOAD_EXTENDED
    // this->extended_tree_load_out.close();
    // #endif
}

void 
BinaryPathOram2::barrier() {
    this->Memory::barrier();
    this->untrusted_memory->barrier();
    this->position_map->barrier();
}

bool 
BinaryPathOram2::access_block(
    MemoryRequestType request_type, uint64_t logical_block_address, MemoryRequest &request, 
    uint64_t offset, uint64_t length
) {
    bool place_block_in_stash = true;
    bool force_bypass_read = false;
    bool is_dummy = (request_type == MemoryRequestType::DUMMY_POP || request_type == MemoryRequestType::DUMMY_PUSH);
    bool ret_value = false;

    if (request_type == MemoryRequestType::POP || request_type == MemoryRequestType::DUMMY_POP) {
        place_block_in_stash = false;
    }

    if (request_type == MemoryRequestType::PUSH || request_type == MemoryRequestType::DUMMY_PUSH) {
        force_bypass_read = true;
    }


    if (length == UINT64_MAX) {
        length = this->parameters.block_size - offset;
    }

    if (offset + length > this->parameters.block_size) {
         throw std::invalid_argument("Access crosses block boundaries");
    }

    // generate a new path for the block
    uint64_t new_path = absl::Uniform(this->bit_gen, 0UL, this->num_paths());

    // read and update position map
    uint64_t path_index = this->read_and_update_position_map(logical_block_address, new_path, is_dummy);

    const uint64_t invalid_logical_block_address = std::numeric_limits<std::uint64_t>::max();
    conditional_memcpy(is_dummy, &logical_block_address, &invalid_logical_block_address, sizeof(std::uint64_t));

    StashEntry target_block(this->parameters.block_size);

    target_block.metadata.set_path(path_index);
    target_block.metadata.set_block_index(logical_block_address);

    bool block_found = false;
    if(!force_bypass_read) {
        this->find_and_remove_block_from_path(&(target_block.metadata), target_block.block.data());
    }
    block_found = target_block.metadata.is_valid();
    // if (!force_bypass_read) {
    //     auto stash_access_start = std::chrono::high_resolution_clock::now();
    //     block_found = this->stash.find_and_remove_block(logical_block_address, target_block);
    //     auto stash_access_end = std::chrono::high_resolution_clock::now();
    //     this->oram_statistics->add_stash_access_time(stash_access_end - stash_access_start);
    // }

    // bool bypass_path_read = (this->bypass_path_read_on_stash_hit && block_found);

    // if (!bypass_path_read) {
    //     // read path
    //     this->read_path(path_index);

    //     // int64_t stash_index = this->get_stash_index(logical_block_address);
    //     // if (!stash_result) {
    //     //     stash_result = this->stash.find_block(logical_block_address);
    //     // }
    //     block_found = block_found || this->find_and_remove_block_on_path(logical_block_address, &target_block.metadata, target_block.block.data());
    // }

    if(!block_found) {
        // block has not been found
        if (
            request_type != MemoryRequestType::WRITE 
            && request_type != MemoryRequestType::PUSH 
            && request_type != MemoryRequestType::DUMMY_PUSH
            && request_type != MemoryRequestType::DUMMY_POP
            && request_type != MemoryRequestType::POP
            ){
            throw std::runtime_error(absl::StrFormat("Can not read block %lu, block does not exist in ORAM!", logical_block_address));
        }

        // create block
        // entry = stash.add_new_block(logical_block_address, new_path, this->block_size);
        const bool t = true;
        conditional_memcpy(request_type == MemoryRequestType::POP, &ret_value, &t, sizeof(bool));
        target_block.metadata = BlockMetadata(logical_block_address, path_index, true);
        // stash.emplace_back(StashEntry{BlockMetadata(logical_block_address, new_path, true), bytes_t(this->block_size)});
        // stash_index = stash.size() - 1;
    }
    // update block
    target_block.metadata.set_path(new_path);

    BlockMetadata invalid;
    conditional_memcpy(is_dummy, &target_block.metadata, &invalid, block_metadata_size);

    bytes_t temp_buffer(request_type == MemoryRequestType::READ_WRITE ? length : 0);

    switch (request_type)
    {
    case MemoryRequestType::READ:
    case MemoryRequestType::POP:
    case MemoryRequestType::DUMMY_POP:
        std::memcpy(request.data.data(), target_block.block.data() + offset, length);
        break;
    case MemoryRequestType::WRITE:
    case MemoryRequestType::PUSH:
    case MemoryRequestType::DUMMY_PUSH:
        std::memcpy(target_block.block.data() + offset, request.data.data(), length);
        break;
    case MemoryRequestType::READ_WRITE:
        // copy original contents into temp buffer
        std::memcpy(temp_buffer.data(), target_block.block.data() + offset, length);
        // write new value into block
        std::memcpy(target_block.block.data() + offset, request.data.data(), length);
        // move original contents from temp buffer back into the request
        std::memcpy(request.data.data(), temp_buffer.data(), length);
        break;
    
    case MemoryRequestType::UPDATE:
        // run update function
        request.update_function(
            target_block.block.data() + offset, request.data.data()
        );
        break;
    default:
        throw std::invalid_argument("unkown memory request type");
        break;
    }

    // add block back into stash
    if (place_block_in_stash) {
        this->place_block_on_path(&(target_block.metadata), target_block.block.data());
    }
    // if (place_block_in_stash) {
    //     auto stash_access_start = std::chrono::high_resolution_clock::now();
    //     this->stash.add_block(target_block);
    //     auto stash_access_end = std::chrono::high_resolution_clock::now();
    //     this->oram_statistics->add_stash_access_time(stash_access_end - stash_access_start);
    // }

    // if (!bypass_path_read) {
    //     // increment access counter
    //     this->access_counter++;
    // }

    // bypass bitfield write

    // if(!bypass_path_read) {
    //     this->evict_and_write_path();
    // }

    return ret_value;
}

void 
BinaryPathOram2::find_and_remove_block_from_path(BlockMetadata *metadata, byte_t * data) {
    if (this->currently_loaded_path.has_value()) {
        // flush the last path first
        this->evict_and_write_path();
    }

    std::uint64_t path = metadata->get_path();
    std::uint64_t logical_block_address = metadata->get_block_index();

    BlockMetadata metadata_buf(logical_block_address, path, false);

    bool block_found = false;

    auto stash_access_start = std::chrono::high_resolution_clock::now();
    block_found = this->stash.find_and_remove_block(logical_block_address, &(metadata_buf), data);
    auto stash_access_end = std::chrono::high_resolution_clock::now();
    this->oram_statistics->add_stash_access_time(stash_access_end - stash_access_start);

    if (!block_found || !this->bypass_path_read_on_stash_hit) {
        this->read_path(path);

        block_found = block_found || this->find_and_remove_block_on_path_buffer(logical_block_address, &metadata_buf, data);
    }
    
    *metadata = metadata_buf;
}
void 
BinaryPathOram2::place_block_on_path(const BlockMetadata *metadata, const byte_t * data) {
    if (!this->currently_loaded_path.has_value()) {
        // do a path read first
        std::uint64_t random_path = absl::Uniform(this->bit_gen, 0UL, this->num_paths());

        this->read_path(random_path);
    }

    // place block on stash
    auto stash_access_start = std::chrono::steady_clock::now();
    this->stash.add_block(metadata, data);
    auto stash_access_end = std::chrono::steady_clock::now();
    this->oram_statistics->add_stash_access_time(stash_access_end - stash_access_start);

    this->evict_and_write_path();
}

void 
BinaryPathOram2::read_path(uint64_t path) {
    // set up read access
    this->oram_statistics->increment_path_read();
    this->currently_loaded_path = path;
    addr_t current_page_level_size = 1;
    addr_t page_level_start_offset = 0;
    // addr_t reversed_path = reverse_bits(path, this->tree_bits * this->levels);
    // addr_t mask = (1UL << this->tree_bits) - 1UL;
    for (addr_t page_level = 0; page_level < this->parameters.page_levels; page_level++) {
        addr_t current_level_offset = path >> (this->parameters.levels - 1 - page_level * this->parameters.levels_per_page);
        addr_t address = (page_level_start_offset + current_level_offset) * this->parameters.page_size;
        this->path_access[page_level].type = MemoryRequestType::READ;
        this->path_access[page_level].address = address;

        page_level_start_offset += current_page_level_size;
        current_page_level_size = current_page_level_size << this->parameters.levels_per_page;
    }

    auto path_read_start = std::chrono::high_resolution_clock::now();
    this->untrusted_memory->batch_access(this->path_access);
    // this->untrusted_memory->barrier();
    auto path_read_end = std::chrono::high_resolution_clock::now();
    this->oram_statistics->add_path_read_time(path_read_end - path_read_start);

    
    // this->valid_bit_tree_memory->barrier();

    // decrypt path
    auto crypto_start = std::chrono::steady_clock::now();
    for (addr_t page_level = 0; page_level < this->parameters.page_levels; page_level++) {
        // prepare counter
        addr_t current_level_offset = path >> (this->parameters.levels - 1 - page_level * this->parameters.levels_per_page);
        addr_t counter;
        if (page_level == 0) {
            counter = this->root_counter;
        } else {
            counter = get_counter(page_level - 1, current_level_offset % (1UL << this->parameters.levels_per_page));
        }
        
        // prepare nonce
        std::memcpy(this->nonce_buffer.data() + this->parameters.random_nonce_bytes, &(this->path_access[page_level].address), sizeof(std::uint64_t));
        std::memcpy(this->nonce_buffer.data() + this->parameters.random_nonce_bytes + sizeof(std::uint64_t), &counter, sizeof(std::uint64_t));

        // decrypt page
        auto verification_result = this->crypto_module->decrypt(
            this->key.data(),
            this->nonce_buffer.data(),
            this->path_access[page_level].data.data(),
            this->parameters.page_size - this->parameters.auth_tag_size,
            this->path_access[page_level].data.data() + this->parameters.page_size - this->parameters.auth_tag_size,
            this->decrypted_path.data() + page_level * this->parameters.page_size
        );

        if (!verification_result) {
            throw std::runtime_error("Auth Tag verification Failed");
        }
    }
    auto crypto_end = std::chrono::steady_clock::now();
    this->oram_statistics->add_crypto_time(crypto_end - crypto_start);
}

void 
BinaryPathOram2::evict_and_write_path() {
    this->oram_statistics->increment_path_write();
    addr_t path = this->currently_loaded_path.value();
    // evict blocks
    for (addr_t i = 0; i < this->parameters.levels; i++) {
        addr_t level = this->parameters.levels - 1 - i;
        BlockMetadata *metadata_ptr = this->eviction_metadata_buffer.data();
        byte_t *data_ptr = this->eviction_data_block_buffer.data();
        auto slots_available = this->parameters.blocks_per_bucket;
        auto num_evicted_from_path = this->try_evict_block_from_path_buffer(slots_available, (this->parameters.levels - 1 - level), this->currently_loaded_path.value(), metadata_ptr, data_ptr, level);
        metadata_ptr += num_evicted_from_path;
        // std::cout << absl::StreamFormat("%lu blocks evicted from path\n", num_evicted_from_path);
        data_ptr += (num_evicted_from_path * this->parameters.block_size);
        slots_available -= num_evicted_from_path;

        auto stash_access_start = std::chrono::high_resolution_clock::now();
        auto num_evicted_from_stash = this->stash.try_evict_blocks(slots_available, (this->parameters.levels - 1 - level), this->currently_loaded_path.value(), metadata_ptr, data_ptr);
        auto stash_access_end = std::chrono::high_resolution_clock::now();
        // std::cout << absl::StreamFormat("%lu blocks evicted from stash\n", num_evicted_from_stash);
        this->oram_statistics->add_stash_access_time(stash_access_end - stash_access_start);
        metadata_ptr += num_evicted_from_stash;
        data_ptr += (num_evicted_from_stash * this->parameters.block_size);
        slots_available -= num_evicted_from_stash;      

        // set remaining slots to invalid
        for (addr_t j = 0; j < slots_available; j++) {
            metadata_ptr[j] = BlockMetadata();
        }

        // copy results back into path buffer
        for (addr_t slot_index = 0; slot_index < this->parameters.blocks_per_bucket; slot_index++) {
            this->metadata_layout.from_block_metadata(this->get_metadata(level, slot_index), this->eviction_metadata_buffer[slot_index]);
            std::memcpy(this->get_data_block(level, slot_index), this->eviction_data_block_buffer.data() + slot_index * this->parameters.block_size, this->parameters.block_size);
        }
    }

    if (this->stash.size() > this->stash.capacity() * 3 / 4) {
        std::cout << absl::StreamFormat("%lu blocks in stash\n", this->stash.size());
    }

    // write path

    // encrypt
    auto crypto_start = std::chrono::steady_clock::now();
    for (addr_t i = 0; i < this->parameters.page_levels; i++) {
        
        // prepare counter
        std::uint64_t page_level = this->parameters.page_levels - 1 - i;
        this->path_access[page_level].type = MemoryRequestType::WRITE;
        addr_t current_level_offset = path >> (this->parameters.levels - 1 - page_level * this->parameters.levels_per_page);
        addr_t counter;
        if (page_level == 0) {
            counter = this->root_counter;
        } else {
            counter = get_counter(page_level - 1, current_level_offset % (1UL << this->parameters.levels_per_page));
        }
        counter += 1;

        if (page_level == 0) {
            this->root_counter = counter;
        } else {
            set_counter(page_level - 1, current_level_offset % (1UL << this->parameters.levels_per_page), counter);
        }
        
        // prepare nonce
        std::memcpy(this->nonce_buffer.data() + this->parameters.random_nonce_bytes, &(this->path_access[page_level].address), sizeof(std::uint64_t));
        std::memcpy(this->nonce_buffer.data() + this->parameters.random_nonce_bytes + sizeof(std::uint64_t), &counter, sizeof(std::uint64_t));

        // encrypt page
        this->crypto_module->encrypt(
            this->key.data(),
            this->nonce_buffer.data(),
            this->decrypted_path.data() + page_level * this->parameters.page_size,
            this->parameters.page_size - this->parameters.auth_tag_size,
            this->path_access[page_level].data.data(),
            this->path_access[page_level].data.data() + this->parameters.page_size - this->parameters.auth_tag_size
        );
    }
    auto crypto_end = std::chrono::steady_clock::now();
    this->oram_statistics->add_crypto_time(crypto_end - crypto_start);
    auto path_write_start = std::chrono::high_resolution_clock::now();
    
    this->untrusted_memory->batch_access(this->path_access);
    // this->untrusted_memory->barrier();
    this->currently_loaded_path = std::nullopt;

    auto path_write_end = std::chrono::high_resolution_clock::now();
    this->oram_statistics->add_path_write_time(path_write_end - path_write_start);
    // this->valid_bitfield->batch_access(this->valid_bitfield_access);
}

uint64_t 
BinaryPathOram2::read_and_update_position_map(uint64_t logical_block_address, uint64_t new_path, bool dummy) {
    auto position_map_access_start = std::chrono::high_resolution_clock::now();
    std::uint64_t old_path = 0;
    std::uint64_t position_map_entries_per_page = this->position_map->page_size() / this->parameters.path_index_size;
    std::uint64_t page_id = logical_block_address / position_map_entries_per_page;
    std::uint64_t offset = logical_block_address % position_map_entries_per_page;
    std::uint64_t address = page_id * this->position_map->page_size() + offset * this->parameters.path_index_size;

    const uint64_t dummy_block_index = absl::Uniform(this->bit_gen, 0UL, this->parameters.num_blocks);
    conditional_memcpy(dummy, &logical_block_address, &dummy_block_index, sizeof(std::uint64_t));
    const MemoryRequestType read = MemoryRequestType::READ;
    if (this->position_map->is_request_type_supported(MemoryRequestType::READ_WRITE)) {
        MemoryRequest position_map_update(MemoryRequestType::READ_WRITE, address, this->parameters.path_index_size);
        conditional_memcpy(dummy, &position_map_update.type, &read, sizeof(MemoryRequestType));
        // *((uint64_t *)position_map_update.data.data()) = new_path;
        std::memcpy(position_map_update.data.data(), &new_path, this->parameters.path_index_size);
        this->position_map->access(position_map_update);
        // return *((uint64_t *) position_map_update.data.data());
        std::memcpy(&old_path, position_map_update.data.data(), this->parameters.path_index_size);
    } else {
        MemoryRequest position_map_read(MemoryRequestType::READ, address, this->parameters.path_index_size);
        this->position_map->access(position_map_read);
        std::memcpy(&old_path, position_map_read.data.data(), this->parameters.path_index_size);

        MemoryRequest position_map_update(MemoryRequestType::WRITE, address, this->parameters.path_index_size);
        conditional_memcpy(dummy, &position_map_update.type, &read, sizeof(MemoryRequestType));
        std::memcpy(position_map_update.data.data(), &new_path, this->parameters.path_index_size);
        this->position_map->access(position_map_update);
    }
    auto position_map_access_end = std::chrono::high_resolution_clock::now();
    this->oram_statistics->add_position_map_access_time(position_map_access_end - position_map_access_start);
    return old_path;
}

std::size_t 
BinaryPathOram2::try_evict_block_from_path_buffer(std::size_t max_count, uint64_t ignored_bits, uint64_t path, BlockMetadata *metadatas, byte_t *data_blocks, uint64_t level_limit) {
    std::size_t num_blocks_evicted = 0;
    auto path_access_start = std::chrono::steady_clock::now();
    const bool f = false;
    // BlockMetadata invalid;
    BlockMetadata metadata_buffer;
    for (uint64_t i = 0; i < this->parameters.levels && i <= level_limit; i++) {
        uint64_t level = std::min(this->parameters.levels - 1, level_limit) - i;
        for (uint64_t slot_index = 0; slot_index < this->parameters.blocks_per_bucket; slot_index++) {
            byte_t * metadata = this->get_metadata(level, slot_index);
            byte_t * current_slot_data_block = this->get_data_block(level, slot_index);
            metadata_buffer = this->metadata_layout.to_block_metadata(metadata);
            bool block_valid = metadata_buffer.is_valid();
            bool is_eviction_candidate = metadata_buffer.is_valid() && (metadata_buffer.get_path() >> ignored_bits) == (path >> ignored_bits);
            bool do_evict = (num_blocks_evicted < max_count) && is_eviction_candidate;
            std::size_t offset = num_blocks_evicted == max_count ? max_count - 1: num_blocks_evicted;
           
            conditional_memcpy(do_evict, metadatas + offset, &metadata_buffer, block_metadata_size);
            conditional_memcpy(
                do_evict,
                data_blocks + (this->parameters.block_size * offset),
                current_slot_data_block,
                this->parameters.block_size
            );

            conditional_memcpy(do_evict, &block_valid, &f, sizeof(bool));

            this->metadata_layout.set_valid(metadata, block_valid);

            num_blocks_evicted += (do_evict ? 1: 0);
        }
    }
    auto path_access_end = std::chrono::steady_clock::now();
    this->oram_statistics->add_path_scan_time(path_access_end - path_access_start);
    return num_blocks_evicted;
}



bool 
BinaryPathOram2::find_and_remove_block_on_path_buffer(addr_t logical_block_address, BlockMetadata* metadata_buffer, byte_t *block_buffer) {
    bool found = false;
    auto path_access_start = std::chrono::steady_clock::now();
    const bool f = false;
    for (addr_t level = 0; level < this->parameters.levels; level++) {
        for (addr_t block = 0; block < this->parameters.blocks_per_bucket; block++) {
            // bool block_valid = this->valid_bit_tree_controller->is_valid(level, block);
            BlockMetadata meta = this->metadata_layout.to_block_metadata(this->get_metadata(level, block));
            bool block_valid = meta.is_valid();
            bool is_target = meta.is_valid() && meta.get_block_index() == logical_block_address;
            found = found || is_target;
            // if (is_target){
            //     std::cout << absl::StreamFormat("Block %lu found at level %lu in slot %lu\n", logical_block_address, level, block);
            // }
            // byte_t none = 0;
            conditional_memcpy(is_target, metadata_buffer, &meta, block_metadata_size);
            conditional_memcpy(is_target, block_buffer, this->get_data_block(level, block), this->parameters.block_size);
            conditional_memcpy(is_target, &block_valid, &f, sizeof(bool));

            this->metadata_layout.set_valid(this->get_metadata(level, block), block_valid);
        }
    }
    auto path_access_end = std::chrono::steady_clock::now();
    this->oram_statistics->add_path_scan_time(path_access_end - path_access_start);

    return found;
}

void 
BinaryPathOram2::empty_oram(block_call_back callback) {
    throw std::runtime_error("not implemented");
    // this->empty_oram_recursive(this->root_counter, 0, 0, this->path_access[0], this->decrypted_path.data(), callback);
    // this->root_counter++;

    // this->stash.empty_stash(callback);
}

void 
BinaryPathOram2::empty_oram_recursive(std::uint64_t counter, uint64_t level, uint64_t level_offset, MemoryRequest &request, byte_t * buffer, block_call_back callback) {
    // if (level >= this->parameters.levels) {
    //     return;
    // }

    // std::uint64_t bucket_index = (1UL << level) - 1 + level_offset;
    // request.address = bucket_index * this->parameters.bucket_size;
    // request.type = MemoryRequestType::READ;

    // this->untrusted_memory->access(request);

    // // prepare nonce
    // std::memcpy(this->nonce_buffer.data() + this->parameters.random_nonce_bytes, &(request.address), sizeof(std::uint64_t));
    // std::memcpy(this->nonce_buffer.data() + this->parameters.random_nonce_bytes + sizeof(std::uint64_t), &counter, sizeof(std::uint64_t));

    // // decrypt block
    // auto verification_result = this->crypto_module->decrypt(
    //     this->key.data(),
    //     this->nonce_buffer.data(),
    //     request.data.data(),
    //     this->parameters.bucket_size - this->parameters.auth_tag_size,
    //     request.data.data() + this->parameters.bucket_size - this->parameters.auth_tag_size,
    //     buffer
    // );

    // if (!verification_result) {
    //     throw std::runtime_error("Auth Tag verification Failed");
    // }

    // std::uint64_t left_child_counter = 0;
    // std::uint64_t right_child_counter = 0;

    // std::memcpy(&left_child_counter, buffer + this->parameters.bucket_size - this->parameters.auth_tag_size - 2 * sizeof(std::uint64_t), sizeof(std::uint64_t));
    // std::memcpy(&right_child_counter, buffer + this->parameters.bucket_size - this->parameters.auth_tag_size - 1 * sizeof(std::uint64_t), sizeof(std::uint64_t));

    // // write updated counters
    // left_child_counter += 1;
    // right_child_counter += 1;

    // std::memcpy(buffer + this->parameters.bucket_size - this->parameters.auth_tag_size - 2 * sizeof(std::uint64_t), &left_child_counter, sizeof(std::uint64_t));
    // std::memcpy(buffer + this->parameters.bucket_size - this->parameters.auth_tag_size - 1 * sizeof(std::uint64_t), &right_child_counter, sizeof(std::uint64_t));

    // left_child_counter -= 1;
    // right_child_counter -= 1;

    // BlockMetadata metadata;
    // for (uint64_t slot = 0; slot < this->parameters.blocks_per_bucket; slot++) {
    //     byte_t *data = buffer + slot * this->parameters.block_size;
    //     byte_t *metadata_location = buffer + this->parameters.blocks_per_bucket * this->parameters.block_size + slot * this->metadata_layout.metadata_size();
    //     metadata = this->metadata_layout.to_block_metadata(metadata_location);

    //     callback(&metadata, data);

    //     this->metadata_layout.set_valid(metadata_location, false);
    // }

    // // prepare nonce for encryption
    // counter += 1;
    // std::memcpy(this->nonce_buffer.data() + this->parameters.random_nonce_bytes + sizeof(std::uint64_t), &counter, sizeof(std::uint64_t));

    // // encrypt block
    // this->crypto_module->encrypt(
    //     this->key.data(),
    //     this->nonce_buffer.data(),
    //     buffer,
    //     this->parameters.bucket_size - this->parameters.auth_tag_size,
    //     request.data.data(),
    //     request.data.data() + this->parameters.bucket_size - this->parameters.auth_tag_size
    // );

    // // write back block
    // request.type = MemoryRequestType::WRITE;

    // this->untrusted_memory->access(request);

    // // recurse child
    // this->empty_oram_recursive(left_child_counter, level + 1, level_offset * 2, request, buffer, callback);

    // this->empty_oram_recursive(right_child_counter, level + 1, level_offset * 2 + 1, request, buffer, callback);
}
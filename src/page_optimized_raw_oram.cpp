#include <page_optimized_raw_oram.hpp>
#include <util.hpp>
#include <absl/strings/str_format.h>
#include <memory_loader.hpp>
#include <cmath>
#include <vector>
#include <algorithm>
#include <limits>

EvictionPathGenerator get_eviction_path_gen(addr_t levels, addr_t order, addr_t first_level_order = std::numeric_limits<addr_t>::max()) {
    std::vector<int64_t> level_sizes;
    for (addr_t i = 0; i < levels - 1; i++){
        if (i == 0 && first_level_order != std::numeric_limits<addr_t>::max()) {
            level_sizes.emplace_back(first_level_order);
        } else {
            level_sizes.emplace_back(order);
        }
    }

    return EvictionPathGenerator(std::move(level_sizes));
}

PageOptimizedRAWOram::ComputedParameters 
PageOptimizedRAWOram::compute_parameters(
    addr_t untrusted_memory_page_size,
    // addr_t valid_bitfield_page_size,
    addr_t block_size,
    addr_t num_blocks,
    addr_t dummy_tree_order,
    CryptoModule *crypto_module,
    double max_load_factor,
    bool is_page_size_strict
) {
    // a lot of the crypto only works with binary tree;
    // so hard coded here to make sure tree is always binary;
    const addr_t tree_order = 2;
    addr_t auth_tag_size = crypto_module->auth_tag_size();
    addr_t required_blocks;
    if (max_load_factor == 1.0) {
        required_blocks = num_blocks;
    } else {
        required_blocks = static_cast<addr_t>(std::ceil(static_cast<double>(num_blocks) / max_load_factor));
    }
    std::cout << absl::StrFormat("Tree need to hold %lu blocks to stay under the load factor of %lf \n", required_blocks, max_load_factor);
    std::size_t block_index_size = num_bytes(num_blocks - 1);
    std::cout << absl::StreamFormat("To index %lu blocks requires an index %lu bytes in size\n", num_blocks, block_index_size);
    addr_t blocks_per_bucket = (untrusted_memory_page_size - auth_tag_size) / (block_size + block_index_size * 2);
    std::cout << absl::StrFormat("Blocks per bucket is %lu\n", blocks_per_bucket);
    addr_t num_buckets = divide_round_up(required_blocks, blocks_per_bucket); 
    std::cout << absl::StrFormat("Tree needs %lu buckets(Pages)\n", num_buckets);
    // std::cout << absl::StrFormat("Actual load factor is %lf\n", static_cast<double>(num_blocks) / static_cast<double>(num_buckets * blocks_per_bucket));
    addr_t tree_bits = num_bits(tree_order - 1);
    std::cout << absl::StrFormat("Tree order of %lu requires %lu bits\n", tree_order, tree_bits);
    addr_t levels = 0;
    addr_t total_buckets = 0;
    addr_t buckets_this_level = 1;
    while(total_buckets < num_buckets) {
        total_buckets += buckets_this_level;
        buckets_this_level = buckets_this_level << tree_bits;
        levels++;
    }
    total_buckets -= (buckets_this_level >> tree_bits);
    addr_t top_level_order = divide_round_up(num_buckets - 1, total_buckets);
    total_buckets = total_buckets * top_level_order + 1;
    addr_t num_paths = levels >= 2 ? (1LU << (tree_bits * (levels - 2))) * top_level_order : 1;
    std::cout << absl::StrFormat("Number of paths in tree %lu\n", num_paths);
    addr_t path_index_size = num_bytes((num_paths * 2) - 1);
    std::cout << absl::StreamFormat("To index %lu paths requires index of %lu bytes\n", num_paths, path_index_size);
    blocks_per_bucket = (untrusted_memory_page_size - auth_tag_size) / (block_size + block_index_size + path_index_size);
    std::cout << absl::StreamFormat("Blocks per bucket is %lu after accounting for index size\n", blocks_per_bucket);
    if (!is_page_size_strict) {
        untrusted_memory_page_size = (block_size + block_index_size + path_index_size) * blocks_per_bucket + auth_tag_size;
        std::cout << absl::StreamFormat("Each untrusted memory page only needs %lu bytes\n", untrusted_memory_page_size);
    }
    addr_t total_blocks = total_buckets * blocks_per_bucket;
    std::cout << absl::StrFormat("Tree requires %lu levels containing %lu blocks\n", levels, total_blocks);
    std::cout << absl::StrFormat("Top of tree has order %lu\n", top_level_order);
    std::cout << absl::StrFormat("Load factor is %lf\n", ((double) num_blocks) / ((double) total_blocks));
    addr_t untrusted_memory_size = total_buckets * untrusted_memory_page_size;
    std::cout << absl::StrFormat("Tree requires %sB of untrusted memory \n", size_to_string(untrusted_memory_size));
    addr_t valid_bits_per_bucket = 1UL << num_bits(blocks_per_bucket - 1);
    std::cout << absl::StrFormat("Each bucket requires %lu bits in the valid bitmap \n", valid_bits_per_bucket);
    // addr_t valid_bitfield_size = valid_bits_per_bucket * total_buckets;
    // valid_bitfield_size = divide_round_up(valid_bitfield_size, valid_bitfield_page_size) * valid_bitfield_page_size;
    // std::cout << absl::StrFormat("Valid Bitfield requires %sb\n", size_to_string(valid_bitfield_size));

    return {
        .levels = levels,
        .top_level_order = top_level_order,
        .blocks_per_bucket = blocks_per_bucket,
        .num_paths = num_paths,
        .valid_bits_per_bucket = valid_bits_per_bucket,
        .untrusted_memory_size = untrusted_memory_size,
        .block_index_size = block_index_size,
        .path_index_size = path_index_size,
        .untrusted_memory_page_size = untrusted_memory_page_size
    };
}

unique_memory_t 
PageOptimizedRAWOram::create(
    std::string_view name,
    unique_memory_t &&position_map, unique_memory_t &&untrusted_memory,
    std::unique_ptr<ValidBitTreeController> &&valid_bit_tree_controller,
    unique_memory_t &&valid_bit_tree_memory,
    std::unique_ptr<CryptoModule> &&crypto_module,
    uint64_t block_size,
    uint64_t num_blocks,
    uint64_t num_accesses_per_eviction,
    uint64_t tree_order,
    uint64_t stash_capacity,
    double max_load_factor,
    bool bypass_path_read_on_stash_hit,
    bool unsecure_eviction_buffer
) {

    auto computed_parameters = PageOptimizedRAWOram::compute_parameters(
        untrusted_memory->page_size(),
        // valid_bitfield->page_size(),
        block_size,
        num_blocks,
        tree_order,
        crypto_module.get(),
        max_load_factor
    );

    assert(untrusted_memory->size() >= computed_parameters.untrusted_memory_size);
    // assert(valid_bitfield->size() >= computed_parameters.valid_bitfield_size);

    return unique_memory_t(
        new PageOptimizedRAWOram(
            "PageOptimizedRAWOram",
            name,
            std::move(position_map),
            std::move(untrusted_memory),
            std::move(valid_bit_tree_controller),
            std::move(valid_bit_tree_memory),
            std::move(crypto_module),
            computed_parameters,
            block_size,
            num_blocks,
            tree_order,
            num_accesses_per_eviction,
            bypass_path_read_on_stash_hit,
            unsecure_eviction_buffer,
            stash_capacity,
            new BinaryPathOramStatistics
        )
    );
}

PageOptimizedRAWOram::PageOptimizedRAWOram(
    std::string_view type, std::string_view name,
    unique_memory_t &&position_map, unique_memory_t &&untrusted_memory,
    std::unique_ptr<ValidBitTreeController> &&valid_bit_tree_controller,
    unique_memory_t &&valid_bit_tree_memory,
    std::unique_ptr<CryptoModule> &&crypto_module,
    const PageOptimizedRAWOram::ComputedParameters &computed_parameters,
    uint64_t block_size,
    uint64_t num_blocks,
    uint64_t tree_order,
    uint64_t num_accesses_per_eviction,
    bool bypass_path_read_on_stash_hit,
    bool unsecure_eviction_buffer,
    uint64_t stash_capacity,
    BinaryPathOramStatistics *statistics
) : 
Memory(type, name, num_blocks * block_size, statistics),
crypto_module(std::move(crypto_module)),
position_map(std::move(position_map)),
untrusted_memory(std::move(untrusted_memory)),
valid_bit_tree_controller(std::move(valid_bit_tree_controller)),
valid_bit_tree_memory(std::move(valid_bit_tree_memory)),
block_size(block_size),
block_size_bits(num_bits(block_size - 1)),
num_blocks(num_blocks),
tree_bits(num_bits(tree_order - 1)),
levels(computed_parameters.levels),
top_level_order(computed_parameters.top_level_order),
blocks_per_bucket(computed_parameters.blocks_per_bucket),
_num_paths(computed_parameters.num_paths),
valid_bits_per_bucket(blocks_per_bucket / 8UL * 8UL),
bypass_path_read_on_stash_hit(bypass_path_read_on_stash_hit),
unsecure_eviction_buffer(unsecure_eviction_buffer),
num_accesses_per_eviction(num_accesses_per_eviction),
random_nonce_bytes(this->crypto_module->nonce_size() - 2 * sizeof(std::uint64_t)),
auth_tag_bytes(this->crypto_module->auth_tag_size()),
untrusted_memory_page_size(this->untrusted_memory->page_size()),
position_map_page_size(this->position_map->page_size()),
num_position_map_entries_per_page(position_map_page_size / computed_parameters.path_index_size),
metadata_layout(computed_parameters.path_index_size, computed_parameters.block_index_size),
oram_statistics(statistics),
eviction_path_gen(get_eviction_path_gen(computed_parameters.levels, tree_order, computed_parameters.top_level_order)),
root_counter(0),
access_counter(0),
stash(block_size, stash_capacity),
eviction_metadata_buffer(this->blocks_per_bucket),
eviction_data_block_buffer(this->blocks_per_bucket * this->block_size),
ll_posmap(dynamic_cast<LLPathOramInterface *>(this->position_map.get())),
posmap_block_buffer(position_map_page_size)
{
    for (addr_t i = 0; i < this->levels; i++) {
        this->path_access.emplace_back(MemoryRequestType::READ, 0, this->untrusted_memory_page_size);
        // this->valid_bitfield_access.emplace_back(MemoryRequestType::READ, 0, this->valid_bits_per_bucket);
    }

    // generate random key
    this->key.resize(this->crypto_module->key_size());
    this->crypto_module->random(this->key.data(), this->crypto_module->key_size());
    this->nonce_buffer.resize(this->crypto_module->nonce_size());
    this->crypto_module->random(this->nonce_buffer.data(), this->random_nonce_bytes);
    this->decrypted_path.resize(this->untrusted_memory_page_size * this->levels);
}

PageOptimizedRAWOram::PageOptimizedRAWOram(
        std::string_view type, const toml::table &table, 
        unique_memory_t &&position_map, unique_memory_t &&untrusted_memory,
        unique_memory_t &&valid_bit_tree_memory,
        BinaryPathOramStatistics *statistics
) :
Memory(type, table, parse_size(*table["block_size"].node()) * parse_size(*table["num_blocks"].node()), statistics),
crypto_module(get_crypto_module_by_name(table["crypto_module"].value<std::string_view>().value())),
position_map(std::move(position_map)),
untrusted_memory(std::move(untrusted_memory)),
valid_bit_tree_controller(std::make_unique<ParentCounterValidBitTreeController>(*table["valid_bit_tree_controller"].as_table(), this->crypto_module.get(), valid_bit_tree_memory.get())),
valid_bit_tree_memory(std::move(valid_bit_tree_memory)),
block_size(parse_size(*table["block_size"].node())),
block_size_bits(num_bits(this->block_size)),
num_blocks(parse_size(*table["num_blocks"].node())),
tree_bits(parse_size(*table["tree_bits"].node())),
levels(parse_size(*table["levels"].node())),
top_level_order(parse_size(*table["top_level_order"].node())),
blocks_per_bucket(parse_size(*table["blocks_per_bucket"].node())),
_num_paths(parse_size(*table["num_paths"].node())),
valid_bits_per_bucket(parse_size(*table["valid_bits_per_bucket"].node())),
bypass_path_read_on_stash_hit(table["bypass_path_read_on_stash_hit"].value<bool>().value_or(false)),
unsecure_eviction_buffer(table["unsecure_eviction_buffer"].value<bool>().value_or(false)),
num_accesses_per_eviction(parse_size(*table["num_accesses_per_eviction"].node())),
random_nonce_bytes(this->crypto_module->nonce_size() - 2 * sizeof(std::uint64_t)),
auth_tag_bytes(this->crypto_module->auth_tag_size()),
untrusted_memory_page_size(this->untrusted_memory->page_size()),
position_map_page_size(this->position_map->page_size()),
num_position_map_entries_per_page(position_map_page_size / parse_size(table["path_index_size"])),
metadata_layout(parse_size(table["path_index_size"]), parse_size(table["block_index_size"])),
oram_statistics(statistics),
eviction_path_gen(table["eviction_path_gen"].as_table()),
root_counter(parse_size(table["root_counter"])),
access_counter(table["access_counter"].value<int64_t>().value_or(0)),
stash(block_size, parse_size_or(table["stash_capacity"], num_accesses_per_eviction * 3)),
eviction_metadata_buffer(this->blocks_per_bucket),
eviction_data_block_buffer(this->blocks_per_bucket * this->block_size),
ll_posmap(dynamic_cast<LLPathOramInterface *>(this->position_map.get())),
posmap_block_buffer(position_map_page_size)
{
    for (addr_t i = 0; i < this->levels; i++) {
        this->path_access.emplace_back(MemoryRequestType::READ, 0, this->untrusted_memory_page_size);
        // this->valid_bitfield_access.emplace_back(MemoryRequestType::READ, 0, this->valid_bits_per_bucket);
    }

    this->key.resize(this->crypto_module->key_size());
    hex_string_to_bytes(table["key"].value<std::string_view>().value(), this->key.data(), this->crypto_module->key_size());
    this->nonce_buffer.resize(this->crypto_module->nonce_size());
    hex_string_to_bytes(table["random_nonce"].value<std::string_view>().value(), this->nonce_buffer.data(), this->random_nonce_bytes);
    this->decrypted_path.resize(this->untrusted_memory_page_size * this->levels);
}

void 
PageOptimizedRAWOram::init() {
    this->position_map->init();
    this->untrusted_memory->init();

    // this->stash.clear();
    this->valid_bit_tree_controller->encrypt_contents(this->key.data());
    MemoryRequest position_map_write(MemoryRequestType::WRITE, 0, this->metadata_layout.path_index_size);
    for (uint64_t i = 0; i < num_blocks; i++) {
        position_map_write.address = get_position_map_address(i);
        // uint64_t *path = (uint64_t *)position_map_write.data.data();
        uint64_t path = absl::Uniform(this->bit_gen, 0UL, this->_num_paths);
        std::memcpy(position_map_write.data.data(), &path, this->metadata_layout.path_index_size);
        if (i % 1000000UL == 0) {
            std::cout << absl::StrFormat("Writing initial value %lu to position map entry %lu\n", path, i);
        }
        this->position_map->access(position_map_write);
    }

    MemoryRequest untrusted_memory_request(MemoryRequestType::WRITE, 0, this->untrusted_memory_page_size);
    bytes_t data_buffer(this->untrusted_memory_page_size); // just zeros

    uint64_t level_start_offset = 0;
    uint64_t level_size = 1;
    const uint64_t total_buckets = this->untrusted_memory->size() / this->untrusted_memory_page_size;

    for (uint64_t level = 0; level < this->levels; level++)
    {
        for (uint64_t level_offset = 0; level_offset < level_size; level_offset++)
        {
            uint64_t page_id = (level_start_offset + level_offset);
            untrusted_memory_request.address = page_id * this->untrusted_memory_page_size;

            // clear both memory requests and buffer
            std::memset(untrusted_memory_request.data.data(), 0, untrusted_memory_request.size);
            // std::memset(data_buffer.data(), 0, data_buffer.size());
            // std::memset(bitfield_request.data.data(), 0, bitfield_request.size);

            if ((page_id + 1) % 10000UL == 0) {
                std::cout << absl::StreamFormat("Writing page %lu of %lu\n", page_id + 1, total_buckets);
            }

            // compute counter
            // uint64_t counter = 0UL - (1UL << level) + reverse_bits(level_offset, level);
            uint64_t counter = 0;

            // prepare nonce
            std::memcpy(this->nonce_buffer.data() + this->random_nonce_bytes, &(untrusted_memory_request.address), sizeof(std::uint64_t));
            std::memcpy(this->nonce_buffer.data() + this->random_nonce_bytes + sizeof(std::uint64_t), &counter, sizeof(std::uint64_t));

            // encrypt page
            this->crypto_module->encrypt(
                this->key.data(),
                this->nonce_buffer.data(),
                data_buffer.data(),
                this->untrusted_memory_page_size - this->auth_tag_bytes,
                untrusted_memory_request.data.data(),
                untrusted_memory_request.data.data() + this->untrusted_memory_page_size - this->auth_tag_bytes
            );
            
            // write page
            this->untrusted_memory->access(untrusted_memory_request);
        }
        level_start_offset += level_size;
        level_size = (level == 0 ? level_size * this->top_level_order : level_size << this->tree_bits);
    }
}

void 
PageOptimizedRAWOram::fast_init() {
    this->root_counter = 0;
    std::cout << "Staring PageOptimizedRAWOram fast initialization\n";
    uint64_t total_buckets = this->untrusted_memory->size() / this->untrusted_memory_page_size;
    std::cout << absl::StreamFormat("ORAM has %lu buckets\n", total_buckets);

    uint64_t total_slots = total_buckets * this->blocks_per_bucket;
    std::cout << absl::StreamFormat("ORAM has %lu slots\n", total_slots);

    std::vector<uint64_t> block_ids;
    block_ids.reserve(total_slots);

    for (uint64_t i = 0; i < total_slots; i++) {
        if (i < this->num_blocks) {
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

    MemoryRequest bitfield_request(MemoryRequestType::WRITE, 0, divide_round_up(this->blocks_per_bucket, 8UL));
    MemoryRequest untrusted_memory_request(MemoryRequestType::WRITE, 0, this->untrusted_memory_page_size);
    MemoryRequest position_map_request(MemoryRequestType::WRITE, 0, this->metadata_layout.path_index_size);
    bytes_t data_buffer(this->untrusted_memory_page_size);

    uint64_t page_metadata_offset = this->block_size * this->blocks_per_bucket;
    uint64_t data_size;
    if (this->block_size >= 8) {
        data_size = 8;
    } else if (this->block_size >= 4) {
        data_size = 4;
    } else if (this->block_size >= 2) {
        data_size = 2;
    } else {
        data_size = 1;
    }

    uint64_t level_start_offset = 0;
    uint64_t level_size = 1;

    for (uint64_t level = 0; level < this->levels; level++)
    {
        for (uint64_t level_offset = 0; level_offset < level_size; level_offset++)
        {
            uint64_t page_id = (level_start_offset + level_offset);
            uint64_t page_block_id_offset = page_id * this->blocks_per_bucket;
            bitfield_request.address = this->valid_bit_tree_controller->get_address_of(level, level_offset);
            untrusted_memory_request.address = page_id * this->untrusted_memory_page_size;

            // compute path
            uint64_t path_upper = level_offset << ((this->levels - 1 - level) * this->tree_bits);
            uint64_t path_lower_limit = 1UL << ((this->levels - 1 - level) * this->tree_bits);

            // clear both memory requests and buffer
            std::memset(untrusted_memory_request.data.data(), 0, untrusted_memory_request.size);
            std::memset(data_buffer.data(), 0, data_buffer.size());
            std::memset(bitfield_request.data.data(), 0, bitfield_request.size);
            for (uint64_t i = 0; i < this->blocks_per_bucket; i++)
            {
                uint64_t block_id = block_ids[page_block_id_offset + i];
                if (block_id != INVALID_BLOCK_ID)
                {
                    uint64_t path = path_upper | absl::Uniform(this->bit_gen, 0UL, path_lower_limit);
                    // write data block;
                    std::memcpy(data_buffer.data() + (i * this->block_size), &block_id, data_size);

                    // write metadata block;
                    byte_t *metadata = data_buffer.data() + page_metadata_offset + i * this->metadata_layout.metadata_size();
                    // *metadata = BlockMetadata(block_id, path, true);
                    this->metadata_layout.set_block_index(metadata, block_id);
                    this->metadata_layout.set_path_index(metadata, path);

                    // write valid bit
                    auto byte_offset = i / 8;
                    auto bit_offset = i % 8;
                    bitfield_request.data[byte_offset] |= (1UL << bit_offset);

                    // write position map
                    position_map_request.address = get_position_map_address(block_id);
                    std::memcpy(position_map_request.data.data(), &path, this->metadata_layout.path_index_size);
                    this->position_map->access(position_map_request);
                }
            }

            if ((page_id + 1) % 10000UL == 0) {
                std::cout << absl::StreamFormat("Writing page %lu of %lu\n", page_id + 1, total_buckets);
            }

            // compute counter
            // uint64_t counter = 0UL - (1UL << level) + reverse_bits(level_offset, level);
            uint64_t counter = 0;

            // prepare nonce
            std::memcpy(this->nonce_buffer.data() + this->random_nonce_bytes, &(untrusted_memory_request.address), sizeof(std::uint64_t));
            std::memcpy(this->nonce_buffer.data() + this->random_nonce_bytes + sizeof(std::uint64_t), &counter, sizeof(std::uint64_t));

            // encrypt page
            this->crypto_module->encrypt(
                this->key.data(),
                this->nonce_buffer.data(),
                data_buffer.data(),
                this->untrusted_memory_page_size - this->auth_tag_bytes,
                untrusted_memory_request.data.data(),
                untrusted_memory_request.data.data() + this->untrusted_memory_page_size - this->auth_tag_bytes
            );
            
            // write page
            this->valid_bit_tree_memory->access(bitfield_request);
            this->untrusted_memory->access(untrusted_memory_request);
        }
        level_start_offset += level_size;
        level_size = (level == 0 ? level_size * this->top_level_order : level_size << this->tree_bits);
    }
    this->valid_bit_tree_controller->encrypt_contents(this->key.data());
}

uint64_t 
PageOptimizedRAWOram::size() const {
    return this->num_blocks * this->block_size;
}

bool 
PageOptimizedRAWOram::isBacked() const {
    return this->untrusted_memory->isBacked();
}

uint64_t 
PageOptimizedRAWOram::page_size() const {
    return this->block_size;
}


void 
PageOptimizedRAWOram::access(MemoryRequest &request) {
    auto start_time = std::chrono::steady_clock::now();
    uint64_t logical_block_address = request.address / block_size;
    uint64_t logical_end_block_address = (request.address + request.size - 1UL) / block_size;
    
    if (logical_block_address != logical_end_block_address) {
         throw std::invalid_argument("Path Optimized ORAM does not support access across block boundaries!");
    }

    this->Memory::log_request(request);

    uint64_t access_offset = request.address - logical_block_address * block_size;

    this->access_block(request.type, logical_block_address, request.data.data(), access_offset, request.size);
    auto end_time = std::chrono::steady_clock::now();
    this->oram_statistics->add_overall_time(end_time - start_time);
}

bool 
PageOptimizedRAWOram::is_request_type_supported(MemoryRequestType type) const {
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
PageOptimizedRAWOram::start_logging(bool append) {
    this->Memory::start_logging(append);
    this->untrusted_memory->start_logging(append);
    this->position_map->start_logging(append);
    this->valid_bit_tree_memory->start_logging(append);
}

void 
PageOptimizedRAWOram::stop_logging() {
    this->Memory::stop_logging();
    this->untrusted_memory->stop_logging();
    this->position_map->stop_logging();
    this->valid_bit_tree_memory->stop_logging();
}

toml::table 
PageOptimizedRAWOram::to_toml_self() const {
    auto table = this->Memory::to_toml();
    table.emplace("block_size", size_to_string(this->block_size));
    table.emplace("levels", size_to_string(this->levels));
    table.emplace("blocks_per_bucket", size_to_string(this->blocks_per_bucket));
    table.emplace("num_blocks", size_to_string(this->num_blocks));
    table.emplace("valid_bits_per_bucket", size_to_string(this->valid_bits_per_bucket));
    table.emplace("bypass_path_read_on_stash_hit", this->bypass_path_read_on_stash_hit);
    table.emplace("unsecure_eviction_buffer", this->unsecure_eviction_buffer);
    table.emplace("num_accesses_per_eviction", size_to_string(this->num_accesses_per_eviction));
    table.emplace("top_level_order", size_to_string(this->top_level_order));
    table.emplace("num_paths", size_to_string(this->_num_paths));
    table.emplace("tree_bits", size_to_string(this->tree_bits));
    table.emplace("eviction_path_gen", eviction_path_gen.to_toml());
    table.emplace("stash_capacity", size_to_string(this->stash.capacity()));
    table.emplace("valid_bit_tree_controller", this->valid_bit_tree_controller->to_toml());
    table.emplace("key", bytes_to_hex_string(this->key.data(), this->crypto_module->key_size()));
    table.emplace("crypto_module", this->crypto_module->name());
    table.emplace("random_nonce", bytes_to_hex_string(this->nonce_buffer.data(), this->random_nonce_bytes));
    table.emplace("root_counter", size_to_string(this->root_counter));
    table.emplace("path_index_size", size_to_string(this->metadata_layout.path_index_size));
    table.emplace("block_index_size", size_to_string(this->metadata_layout.block_index_size));

    return table;
}

toml::table 
PageOptimizedRAWOram::to_toml() const {
    auto table = this->to_toml_self();
    table.emplace("position_map", this->position_map->to_toml());
    table.emplace("untrusted_memory", this->untrusted_memory->to_toml());
    table.emplace("valid_bit_tree_memory", this->valid_bit_tree_memory->to_toml());
    return table;
}

void 
PageOptimizedRAWOram::save_to_disk(const std::filesystem::path &location) const {
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

    // write out bitfield
    std::filesystem::path bitfield_directory = location / "bitfield";
    std::filesystem::create_directory(bitfield_directory);
    this->valid_bit_tree_memory->save_to_disk(bitfield_directory);

    // save stash
    this->stash.save_stash(location);
}

unique_memory_t 
PageOptimizedRAWOram::load_from_disk(const std::filesystem::path &location) {
    auto table = toml::parse_file((location / "config.toml").string());
    return PageOptimizedRAWOram::load_from_disk(location, table);
}

unique_memory_t 
PageOptimizedRAWOram::load_from_disk(const std::filesystem::path &location, const toml::table &table) {
    unique_memory_t position_map = MemoryLoader::load(location / "position_map");
    unique_memory_t untrusted_memory = MemoryLoader::load(location / "untrusted_memory");
    unique_memory_t bitfield = MemoryLoader::load(location / "bitfield");

    PageOptimizedRAWOram *oram = new PageOptimizedRAWOram(
        "PageOptimizedRAWOram", table,
        std::move(position_map), std::move(untrusted_memory), std::move(bitfield),
        new BinaryPathOramStatistics()
    );
    oram->stash.load_stash(location);

    return unique_memory_t(oram);
}

void 
PageOptimizedRAWOram::reset_statistics(bool from_file) {
    this->Memory::reset_statistics(from_file);
    this->untrusted_memory->reset_statistics(from_file);
    this->valid_bit_tree_memory->reset_statistics(from_file);
    this->position_map->reset_statistics(from_file);

    #ifdef PROFILE_TREE_LOAD_EXTENDED
    std::filesystem::path extended_tree_load_out_path(absl::StrFormat("%s_extended.log", this->name));
    this->extended_tree_load_out.open(extended_tree_load_out_path);
    this->extended_tree_load_log_counter = 0;
    #endif
}

void 
PageOptimizedRAWOram::save_statistics() {
    this->Memory::save_statistics();
    this->untrusted_memory->save_statistics();
    this->valid_bit_tree_memory->save_statistics();
    this->position_map->save_statistics();

    #ifdef PROFILE_TREE_LOAD_EXTENDED
    this->extended_tree_load_out.close();
    #endif
}

void 
PageOptimizedRAWOram::barrier() {
    this->Memory::barrier();
    this->untrusted_memory->barrier();
    this->valid_bit_tree_memory->barrier();
    this->position_map->barrier();
}

void 
PageOptimizedRAWOram::access_block(
    MemoryRequestType request_type, uint64_t logical_block_address, unsigned char *buffer, 
    uint64_t offset, uint64_t length
) {
    bool place_block_in_stash = true;
    bool force_bypass_read = false;
    bool is_dummy = (request_type == MemoryRequestType::DUMMY_POP || request_type == MemoryRequestType::DUMMY_PUSH);

    if (request_type == MemoryRequestType::POP || request_type == MemoryRequestType::DUMMY_POP) {
        place_block_in_stash = false;
    }

    if (request_type == MemoryRequestType::PUSH || request_type == MemoryRequestType::DUMMY_PUSH) {
        force_bypass_read = true;
    }


    if (length == UINT64_MAX) {
        length = this->block_size - offset;
    }

    if (offset + length > this->block_size) {
         throw std::invalid_argument("Access crosses block boundaries");
    }

    // generate a new path for the block
    uint64_t new_path = absl::Uniform(this->bit_gen, 0UL, this->_num_paths);

    // read and update position map
    uint64_t path_index = this->read_and_update_position_map(logical_block_address, new_path, is_dummy);

    const uint64_t invalid_logical_block_address = std::numeric_limits<std::uint64_t>::max();
    conditional_memcpy(is_dummy, &logical_block_address, &invalid_logical_block_address, sizeof(std::uint64_t));

    StashEntry target_block(this->block_size);
    
    target_block.metadata.set_path(path_index);
    target_block.metadata.set_block_index(logical_block_address);

    bool block_found = false;
    if (!force_bypass_read) {
        this->find_and_remove_block_from_path(&(target_block.metadata), target_block.block.data());
    }
    block_found = target_block.metadata.is_valid();
    // if (!force_bypass_read) {
    //     auto stash_access_start = std::chrono::steady_clock::now();
    //     block_found = this->stash.find_and_remove_block(logical_block_address, target_block);
    //     auto stash_access_end = std::chrono::steady_clock::now();
    //     this->oram_statistics->add_stash_access_time(stash_access_end - stash_access_start);
    // }

    // bool bypass_path_read = (this->bypass_path_read_on_stash_hit && block_found);

    // if (!bypass_path_read && !force_bypass_read) {
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
            ){
            throw std::runtime_error(absl::StrFormat("Can not read block %lu, block does not exist in ORAM!", logical_block_address));
        }

        // create block
        // entry = stash.add_new_block(logical_block_address, new_path, this->block_size);
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
        std::memcpy(buffer, target_block.block.data() + offset, length);
        break;
    case MemoryRequestType::WRITE:
    case MemoryRequestType::PUSH:
    case MemoryRequestType::DUMMY_PUSH:
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

    if (place_block_in_stash) {
        this->place_block_on_path(&(target_block.metadata), target_block.block.data());
    }

    // add block back into stash
    // if (place_block_in_stash) {
    //     auto stash_access_start = std::chrono::steady_clock::now();
    //     this->stash.add_block(target_block);
    //     auto stash_access_end = std::chrono::steady_clock::now();
    //     this->oram_statistics->add_stash_access_time(stash_access_end - stash_access_start);
    // }

    // if (place_block_in_stash && !bypass_path_read) {
    //     // increment access counter
    //     this->access_counter++;
    // }

    // // bypass bitfield write

    // if(!bypass_path_read && !force_bypass_read) {
    //     // write bitfield
    //     // functionality moved to 
    //     // for(uint64_t i = 0; i < this->levels; i++) {
    //     //     this->valid_bitfield_access[i].type = MemoryRequestType::WRITE;
    //     // }
    //     // this->valid_bitfield->batch_access(this->valid_bitfield_access);
    //     auto valid_bit_tree_start = std::chrono::steady_clock::now();
    //     this->valid_bit_tree_controller->write_path(this->key.data());
    //     this->valid_bit_tree_memory->barrier();
    //     auto valid_bit_tree_end = std::chrono::steady_clock::now();
    //     this->oram_statistics->add_valid_bit_tree_time(valid_bit_tree_end - valid_bit_tree_start);
    // }

    // if (this->access_counter >= this->num_accesses_per_eviction) {
    //     // check if an eviction need to happen
    //     this->eviction_access();
    //     this->access_counter = 0;
    // }

    // std::cout << absl::StrFormat("Stash size is %lu\n", this->stash.size());
    // this->oram_statistics->log_stash_size(this->stash.size());

    #ifdef PROFILE_TREE_LOAD_EXTENDED
    this->log_extended_tree_load();
    #endif
}

void 
PageOptimizedRAWOram::find_and_remove_block_from_path(BlockMetadata *metadata, byte_t * data) {
    std::uint64_t path = metadata->get_path();
    std::uint64_t logical_block_address = metadata->get_block_index();

    BlockMetadata metadata_buf(logical_block_address, path, false);

    bool block_found = false;

    auto stash_access_start = std::chrono::steady_clock::now();
    block_found = this->stash.find_and_remove_block(logical_block_address, &metadata_buf, data);
    auto stash_access_end = std::chrono::steady_clock::now();
    this->oram_statistics->add_stash_access_time(stash_access_end - stash_access_start);

    if (!block_found || !this->bypass_path_read_on_stash_hit) {
        // read given path
        this->read_path(path);

        //search path for block
        block_found = block_found || this->find_and_remove_block_on_path_buffer(logical_block_address, &metadata_buf, data);

        // write bit_tree back
        auto valid_bit_tree_start = std::chrono::steady_clock::now();
        this->valid_bit_tree_controller->write_path(this->key.data());
        this->valid_bit_tree_memory->barrier();
        auto valid_bit_tree_end = std::chrono::steady_clock::now();
        this->oram_statistics->add_valid_bit_tree_time(valid_bit_tree_end - valid_bit_tree_start);
    }

    *metadata = metadata_buf;
}
void 
PageOptimizedRAWOram::place_block_on_path(const BlockMetadata *metadata, const byte_t * data) {
    auto stash_access_start = std::chrono::steady_clock::now();
    this->stash.add_block(metadata, data);
    auto stash_access_end = std::chrono::steady_clock::now();
    this->oram_statistics->add_stash_access_time(stash_access_end - stash_access_start);

    access_counter++;

    if (this->access_counter >= this->num_accesses_per_eviction) {
        // check if an eviction need to happen
        this->eviction_access();
        this->access_counter = 0;
    }
};

std::uint64_t 
PageOptimizedRAWOram::num_paths() const noexcept {
    return this->_num_paths;
};

void 
PageOptimizedRAWOram::read_path(uint64_t path) {
    // set up read access
    this->oram_statistics->increment_path_read();
    this->currently_loaded_path = path;
    addr_t current_level_size = 1;
    addr_t offset = 0;
    // addr_t reversed_path = reverse_bits(path, this->tree_bits * this->levels);
    // addr_t mask = (1UL << this->tree_bits) - 1UL;
    for (addr_t level = 0; level < this->levels; level++) {
        addr_t current_level_offset = path >> ((this->levels - 1 - level) * this->tree_bits);
        addr_t address = (offset + current_level_offset) * this->untrusted_memory_page_size;
        this->path_access[level].type = MemoryRequestType::READ;
        this->path_access[level].address = address;
        // addr_t valid_bitfield_address = (offset + current_level_offset) * this->valid_bits_per_bucket;
        // this->valid_bitfield_access[level].type = MemoryRequestType::READ;
        // this->valid_bitfield_access[level].address = valid_bitfield_address;

        offset += current_level_size;
        current_level_size = (level == 0 ? current_level_size * this->top_level_order : current_level_size << this->tree_bits);
    }

    auto path_read_start = std::chrono::steady_clock::now();
    this->untrusted_memory->batch_access(this->path_access);
    // this->untrusted_memory->barrier();
    auto path_read_end = std::chrono::steady_clock::now();
    this->oram_statistics->add_path_read_time(path_read_end - path_read_start);

    auto valid_bit_tree_start = std::chrono::steady_clock::now();
    this->valid_bit_tree_controller->read_path(this->key.data(), path);
    // this->valid_bit_tree_memory->barrier();
    auto valid_bit_tree_end = std::chrono::steady_clock::now();
    this->oram_statistics->add_valid_bit_tree_time(valid_bit_tree_end - valid_bit_tree_start);

    // decrypt path
    auto crypto_start = std::chrono::steady_clock::now();
    for (addr_t level = 0; level < this->levels; level++) {
        // prepare counter
        addr_t counter = this->root_counter / (1UL << level);
        addr_t current_level_offset = path >> ((this->levels - 1 - level) * this->tree_bits);
        if (reverse_bits(current_level_offset, level) < this->root_counter % (1UL << level)) {
            counter += 1;
        }
        
        // prepare nonce
        std::memcpy(this->nonce_buffer.data() + this->random_nonce_bytes, &(this->path_access[level].address), sizeof(std::uint64_t));
        std::memcpy(this->nonce_buffer.data() + this->random_nonce_bytes + sizeof(std::uint64_t), &counter, sizeof(std::uint64_t));

        // decrypt page
        auto verification_result = this->crypto_module->decrypt(
            this->key.data(),
            this->nonce_buffer.data(),
            this->path_access[level].data.data(),
            this->untrusted_memory_page_size - this->auth_tag_bytes,
            this->path_access[level].data.data() + this->untrusted_memory_page_size - this->auth_tag_bytes,
            this->decrypted_path.data() + level * this->untrusted_memory_page_size
        );

        if (!verification_result) {
            throw std::runtime_error("Auth Tag verification Failed");
        }
    }
    auto crypto_end = std::chrono::steady_clock::now();
    this->oram_statistics->add_crypto_time(crypto_end - crypto_start);

}

void 
PageOptimizedRAWOram::write_path() {
    this->oram_statistics->increment_path_write();
    addr_t path = this->currently_loaded_path.value();
    for (addr_t level = 0; level < this->levels; level++) {
        this->path_access[level].type = MemoryRequestType::WRITE;
        // this->valid_bitfield_access[level].type = MemoryRequestType::WRITE;
    }

    // encrypt
    auto crypto_start = std::chrono::steady_clock::now();
    for (addr_t level = 0; level < this->levels; level++) {
        // prepare counter
        addr_t counter = this->root_counter / (1UL << level);
        addr_t current_level_offset = path >> ((this->levels - 1 - level) * this->tree_bits);
        if (reverse_bits(current_level_offset, level) < this->root_counter % (1UL << level)) {
            counter += 1;
        }
        counter += 1;
        
        // prepare nonce
        std::memcpy(this->nonce_buffer.data() + this->random_nonce_bytes, &(this->path_access[level].address), sizeof(std::uint64_t));
        std::memcpy(this->nonce_buffer.data() + this->random_nonce_bytes + sizeof(std::uint64_t), &counter, sizeof(std::uint64_t));

        // encrypt page
        this->crypto_module->encrypt(
            this->key.data(),
            this->nonce_buffer.data(),
            this->decrypted_path.data() + level * this->untrusted_memory_page_size,
            this->untrusted_memory_page_size - this->auth_tag_bytes,
            this->path_access[level].data.data(),
            this->path_access[level].data.data() + this->untrusted_memory_page_size - this->auth_tag_bytes
        );
    }
    auto crypto_end = std::chrono::steady_clock::now();
    this->oram_statistics->add_crypto_time(crypto_end - crypto_start);
    auto path_write_start = std::chrono::steady_clock::now();
    this->untrusted_memory->batch_access(this->path_access);
    // this->valid_bitfield->batch_access(this->valid_bitfield_access);
    

    // this->untrusted_memory->barrier();
    auto path_write_end = std::chrono::steady_clock::now();
    this->oram_statistics->add_path_write_time(path_write_end - path_write_start);

    auto valid_bit_tree_start = std::chrono::steady_clock::now();
    this->valid_bit_tree_controller->write_path(this->key.data());
    // this->valid_bit_tree_memory->barrier();
    auto valid_bit_tree_end = std::chrono::steady_clock::now();
    this->oram_statistics->add_valid_bit_tree_time(valid_bit_tree_end - valid_bit_tree_start);
}

uint64_t 
PageOptimizedRAWOram::read_and_update_position_map(uint64_t logical_block_address, uint64_t new_path, bool dummy) {
    auto position_map_access_start = std::chrono::steady_clock::now();
    std::uint64_t old_path = 0;
    const MemoryRequestType read = MemoryRequestType::READ;
    const uint64_t dummy_block_index = absl::Uniform(this->bit_gen, 0UL, this->num_blocks);
    conditional_memcpy(dummy, &logical_block_address, &dummy_block_index, sizeof(std::uint64_t));
    if (this->position_map->is_request_type_supported(MemoryRequestType::READ_WRITE)) {
        MemoryRequest position_map_update(MemoryRequestType::READ_WRITE, get_position_map_address(logical_block_address), this->metadata_layout.path_index_size);
        conditional_memcpy(dummy, &position_map_update.type, &read, sizeof(MemoryRequestType));
        // *((uint64_t *)position_map_update.data.data()) = new_path;
        std::memcpy(position_map_update.data.data(), &new_path, this->metadata_layout.path_index_size);
        this->position_map->access(position_map_update);
        // return *((uint64_t *) position_map_update.data.data());
        std::memcpy(&old_path, position_map_update.data.data(), this->metadata_layout.path_index_size);
    } else {
        MemoryRequest position_map_update(MemoryRequestType::READ, get_position_map_address(logical_block_address), this->metadata_layout.path_index_size);
        this->position_map->access(position_map_update);
        std::memcpy(&old_path, position_map_update.data.data(), this->metadata_layout.path_index_size);

        // MemoryRequest position_map_update = {MemoryRequestType::WRITE, logical_block_address * 8, 8, bytes_t(8)};
        position_map_update.type = MemoryRequestType::WRITE;
        conditional_memcpy(dummy, &position_map_update.type, &read, sizeof(MemoryRequestType));
        std::memcpy(position_map_update.data.data(), &new_path, this->metadata_layout.path_index_size);
        this->position_map->access(position_map_update);
    }
    auto position_map_access_end = std::chrono::steady_clock::now();
    this->oram_statistics->add_position_map_access_time(position_map_access_end - position_map_access_start);
    return old_path;
}

std::uint64_t 
PageOptimizedRAWOram::read_and_update_position_map_function(uint64_t logical_block_address, positionmap_updater updater, bool is_dummy) {
    auto position_map_access_start = std::chrono::steady_clock::now();
    std::uint64_t old_path = 0;
    const MemoryRequestType read = MemoryRequestType::READ;
    const uint64_t dummy_block_index = absl::Uniform(this->bit_gen, 0UL, this->num_blocks);
    conditional_memcpy(is_dummy, &logical_block_address, &dummy_block_index, sizeof(std::uint64_t));
    if (this->ll_posmap != nullptr) {
        std::uint64_t position_map_page = this->get_position_map_page(logical_block_address);
        std::uint64_t position_map_offset_in_page = this->get_position_map_offset_in_page(logical_block_address);

        std::uint64_t new_posmap_path = absl::Uniform(this->bit_gen, 0UL, this->ll_posmap->num_paths());

        // read and update recursive positionmaps
        std::uint64_t old_posmap_path = this->ll_posmap->read_and_update_position_map(position_map_page, new_posmap_path);

        // get block
        this->posmap_block_buffer.metadata.set_block_index(position_map_page);
        this->posmap_block_buffer.metadata.set_path(old_posmap_path);
        this->ll_posmap->find_and_remove_block_from_path(this->posmap_block_buffer);

        if (!this->posmap_block_buffer.metadata.is_valid()) {
            throw std::runtime_error(absl::StrFormat("Unable to read block %lu from path %lu in position map.", position_map_page, old_posmap_path));
        }

        // update block
        std::memcpy(&old_path, posmap_block_buffer.block.data() + position_map_offset_in_page, this->metadata_layout.path_index_size);

        uint64_t new_path = updater(old_path);

        conditional_memcpy(!is_dummy, posmap_block_buffer.block.data() + position_map_offset_in_page, &new_path, this->metadata_layout.path_index_size);

        // place block back
        this->posmap_block_buffer.metadata.set_path(new_posmap_path);
        this->ll_posmap->place_block_on_path(this->posmap_block_buffer);
    } else {
        MemoryRequest position_map_update(MemoryRequestType::READ, get_position_map_address(logical_block_address), this->metadata_layout.path_index_size);
        this->position_map->access(position_map_update);
        std::memcpy(&old_path, position_map_update.data.data(), this->metadata_layout.path_index_size);

        uint64_t new_path = updater(old_path);

        // MemoryRequest position_map_update = {MemoryRequestType::WRITE, logical_block_address * 8, 8, bytes_t(8)};
        position_map_update.type = MemoryRequestType::WRITE;
        conditional_memcpy(is_dummy, &position_map_update.type, &read, sizeof(MemoryRequestType));
        std::memcpy(position_map_update.data.data(), &new_path, this->metadata_layout.path_index_size);
        this->position_map->access(position_map_update);
    }
    auto position_map_access_end = std::chrono::steady_clock::now();
    this->oram_statistics->add_position_map_access_time(position_map_access_end - position_map_access_start);
    return old_path;
}

std::size_t 
PageOptimizedRAWOram::try_evict_block_from_path_buffer(std::size_t max_count, uint64_t ignored_bits, uint64_t path, BlockMetadata *metadatas, byte_t *data_blocks, uint64_t level_limit) {
    std::size_t num_blocks_evicted = 0;
    auto path_scan_start = std::chrono::steady_clock::now();
    // BlockMetadata invalid;
    BlockMetadata metadata_buffer;
    for (uint64_t i = 0; i < this->levels && i <= level_limit; i++) {
        uint64_t level = std::min(this->levels - 1, level_limit) - i;
        for (uint64_t slot_index = 0; slot_index < this->blocks_per_bucket; slot_index++) {
            byte_t * metadata = this->get_metadata(level, slot_index);
            byte_t * current_slot_data_block = this->get_data_block(level, slot_index);
            bool block_valid = this->valid_bit_tree_controller->is_valid(level, slot_index);
            metadata_buffer = this->metadata_layout.to_block_metadata(metadata, block_valid);
            bool is_eviction_candidate = block_valid && (metadata_buffer.get_path() >> ignored_bits) == (path >> ignored_bits);
            bool do_evict = (num_blocks_evicted < max_count) && is_eviction_candidate;
            std::size_t offset = num_blocks_evicted == max_count ? max_count - 1: num_blocks_evicted;
           
            conditional_memcpy(do_evict, metadatas + offset, &metadata_buffer, block_metadata_size);
            conditional_memcpy(
                do_evict,
                data_blocks + (this->block_size * offset),
                current_slot_data_block,
                this->block_size
            );
            this->valid_bit_tree_controller->conditional_set_valid(level, slot_index, false, do_evict);

            // conditional_memcpy(do_evict, metadata, &invalid, block_metadata_size);

            num_blocks_evicted += (do_evict ? 1: 0);
        }
    }
    auto path_scan_end = std::chrono::steady_clock::now();
    this->oram_statistics->add_path_scan_time(path_scan_end - path_scan_start);
    return num_blocks_evicted;
}

void 
PageOptimizedRAWOram::eviction_access() {
    // addr_t cycle_index = this->eviction_counter % this->num_paths;
    // // std::cout << "CI: " << cycle_index << "\n";
    // addr_t path_lower = reverse_bits(cycle_index % this->top_level_order, this->tree_bits);
    // // std::cout << "PL: "<< path_lower << "\n";
    // addr_t path_upper = (cycle_index / this->top_level_order) << this->tree_bits;
    // // std::cout << "PU: " << path_upper << "\n";
    // // std::cout << "PC: " << (path_upper | path_lower) << "\n";
    // addr_t path = reverse_bits(path_upper | path_lower, (this->levels - 1) * this->tree_bits);
    addr_t path = this->eviction_path_gen.next_path();

    // std::cout << absl::StrFormat("Eviction access on path %lu, %lu\n", path, reverse_bits(path, this->levels - 1));

    // read path
    this->read_path(path);

    // for (addr_t level = 0; level < this->levels; level++) {
    //     // pull all valid blocks into stash
    //     addr_t valid_counter = 0;
    //     for (addr_t block_index = 0; block_index < this->blocks_per_bucket; block_index++) {
    //         if (this->valid_bitfield_access[level].data[block_index]) {
    //             this->stash.add_block(this->get_metadata(level, block_index), this->get_data_block(level, block_index), this->block_size);
    //             valid_counter++;
    //         }
    //     }
    //     // std::cout << absl::StrFormat("Level %lu has %lu vacancies\n", level, this->blocks_per_bucket - valid_counter);
    // }

    #ifdef PROFILE_TREE_LOAD
    std::vector<int64_t> tree_loads(this->levels);
    #endif
    // do eviction
    for (addr_t i = 0; i < this->levels; i++) {
        addr_t level = this->levels - 1 - i;
        addr_t num_blocks_evicted = 0;
        if (this->unsecure_eviction_buffer) {
            for (addr_t j = 0; j < this->blocks_per_bucket; j++) {
                BlockMetadata *metadata_ptr = this->eviction_metadata_buffer.data() + j;
                *metadata_ptr = BlockMetadata();
                byte_t *data_ptr = this->eviction_data_block_buffer.data() + j * this->block_size;
                auto slots_available = 1;
                auto num_evicted_from_path = this->try_evict_block_from_path_buffer(slots_available, (this->levels - 1 - level) * this->tree_bits, path, metadata_ptr, data_ptr, level);
                metadata_ptr += num_evicted_from_path;
                data_ptr += (num_evicted_from_path * this->block_size);
                slots_available -= num_evicted_from_path;

                auto stash_access_start = std::chrono::steady_clock::now();
                auto num_evicted_from_stash = this->stash.try_evict_blocks(slots_available, (this->levels - 1 - level) * this->tree_bits, path, metadata_ptr, data_ptr);
                auto stash_access_end = std::chrono::steady_clock::now();
                this->oram_statistics->add_stash_access_time(stash_access_end - stash_access_start);
                // metadata_ptr += num_evicted_from_stash;
                // data_ptr += (num_evicted_from_stash * this->block_size);
                slots_available -= num_evicted_from_stash;
                num_blocks_evicted += (1 - slots_available);
            }
        } else {
            BlockMetadata *metadata_ptr = this->eviction_metadata_buffer.data();
            byte_t *data_ptr = this->eviction_data_block_buffer.data();
            auto slots_available = this->blocks_per_bucket;
            auto num_evicted_from_path = this->try_evict_block_from_path_buffer(slots_available, (this->levels - 1 - level) * this->tree_bits, path, metadata_ptr, data_ptr, level);
            metadata_ptr += num_evicted_from_path;
            data_ptr += (num_evicted_from_path * this->block_size);
            slots_available -= num_evicted_from_path;

            auto stash_access_start = std::chrono::steady_clock::now();
            auto num_evicted_from_stash = this->stash.try_evict_blocks(slots_available, (this->levels - 1 - level) * this->tree_bits, path, metadata_ptr, data_ptr);
            auto stash_access_end = std::chrono::steady_clock::now();
            this->oram_statistics->add_stash_access_time(stash_access_end - stash_access_start);
            metadata_ptr += num_evicted_from_stash;
            data_ptr += (num_evicted_from_stash * this->block_size);
            slots_available -= num_evicted_from_stash;

            num_blocks_evicted = this->blocks_per_bucket - slots_available;     

            // set remaining slots to invalid
            for (addr_t j = 0; j < slots_available; j++) {
                metadata_ptr[j] = BlockMetadata();
            }
        }

        #ifdef PROFILE_TREE_LOAD
        tree_loads[level] = num_blocks_evicted;
        #endif

        // copy results back into path buffer
        for (addr_t slot_index = 0; slot_index < this->blocks_per_bucket; slot_index++) {
            this->metadata_layout.from_block_metadata(this->get_metadata(level, slot_index), this->eviction_metadata_buffer[slot_index]);
            std::memcpy(this->get_data_block(level, slot_index), this->eviction_data_block_buffer.data() + slot_index * this->block_size, this->block_size);
            // this->valid_bitfield_access[level].data[slot_index] = 0;
            this->valid_bit_tree_controller->set_valid(level, slot_index, slot_index < num_blocks_evicted);
            // const byte_t valid = 1;
            // conditional_memcpy(slot_index < (this->blocks_per_bucket - slots_available), &this->valid_bitfield_access[level].data[slot_index], &valid, sizeof(byte_t));
            // this->valid_bit_tree_controller->conditional_set_valid(level, slot_index, true, slot_index < (this->blocks_per_bucket - slots_available));
        }

    }

    #ifdef PROFILE_TREE_LOAD
    tree_loads.shrink_to_fit();
    oram_statistics->log_tree_load(std::move(tree_loads));
    #endif

    this->write_path();

    this->oram_statistics->log_stash_size(this->stash.size());
    if(this->stash.size() != 0) {
        std::cout << absl::StreamFormat("%lu blocks in stash\n", this->stash.size());
    }

    this->root_counter++;
}

// StashEntry
// PageOptimizedRAWOram::find_block_on_path(addr_t logical_block_address) {
//     StashEntry result = StashEntry{BlockMetadata(), bytes_t(this->block_size)};
//     for (addr_t level = 0; level < this->levels; level++) {
//         for (addr_t block = 0; block < this->blocks_per_bucket; block++) {
//             BlockMetadata meta = *this->get_metadata(level, block);
//             bool is_target = this->valid_bitfield_access[level].data[block] && this->get_metadata(level, block)->is_valid() && this->get_metadata(level, block)->get_block_index() == logical_block_address;
//             // if (){
//             //     // result = std::make_optional<PathLocation>({level, block});
//             // }

//             conditional_memcpy(is_target, &(result.metadata), &meta, block_metadata_size);
//             conditional_memcpy(is_target, result.block.data(), this->get_data_block(level, block), this->block_size);
//         }
//     }

//     return result;
// }

bool 
PageOptimizedRAWOram::find_and_remove_block_on_path_buffer(addr_t logical_block_address, BlockMetadata* metadata_buffer, byte_t *block_buffer) {
    bool found = false;
    auto path_scan_start = std::chrono::steady_clock::now();
    for (addr_t level = 0; level < this->levels; level++) {
        for (addr_t block = 0; block < this->blocks_per_bucket; block++) {
            bool block_valid = this->valid_bit_tree_controller->is_valid(level, block);
            BlockMetadata meta = this->metadata_layout.to_block_metadata(this->get_metadata(level, block), block_valid);
            // if (block_valid) {
            //     std::cout << absl::StreamFormat("Block %lu on level %lu\n", meta.get_block_index(), level);
            // }
            bool is_target = block_valid && meta.get_block_index() == logical_block_address;
            found = found || is_target;
            // if (is_target){
            //     std::cout << absl::StreamFormat("Block %lu found at level %lu in slot %lu\n", logical_block_address, level, block);
            // }
            // byte_t none = 0;
            conditional_memcpy(is_target, metadata_buffer, &meta, block_metadata_size);
            conditional_memcpy(is_target, block_buffer, this->get_data_block(level, block), this->block_size);
            // conditional_memcpy(is_target, &this->valid_bitfield_access[level].data[block], &none, 1);
            this->valid_bit_tree_controller->conditional_set_valid(level, block, false, is_target);
        }
    }
    auto path_scan_end = std::chrono::steady_clock::now();
    this->oram_statistics->add_path_scan_time(path_scan_end - path_scan_start);

    return found;
}


#ifdef PROFILE_TREE_LOAD_EXTENDED
void 
PageOptimizedRAWOram::log_extended_tree_load() {
        // if(!this->extended_tree_load_out) {
        //     return;
        // }

        // if (this->extended_tree_load_log_counter % 100UL == 0) {
        //     this->extended_tree_load_out << absl::StrFormat("access: %lu\n", this->extended_tree_load_log_counter);
        //     uint64_t level_start_offset = 0;
        //     uint64_t level_size = 1;
        //     MemoryRequest bitfield_request(MemoryRequestType::READ, 0, this->valid_bits_per_bucket);
        //     for (uint64_t level = 0; level < this->levels; level++) {
        //         for(uint64_t level_offset = 0; level_offset < level_size; level_offset++) {
        //             bitfield_request.address = (level_start_offset + level_offset) * this->valid_bits_per_bucket;
        //             this->valid_bitfield->access(bitfield_request);
        //             uint64_t valid_block_count = 0;
        //             for (uint64_t i = 0; i < this->blocks_per_bucket; i++) {
        //                 if(bitfield_request.data[i]) {
        //                     valid_block_count++;
        //                 }
        //             }
        //             this->extended_tree_load_out << valid_block_count << " ";
        //         }
        //         this->extended_tree_load_out << "\n";
        //         level_start_offset += level_size;
        //         level_size = (level == 0 ? level_size * this->top_level_order : level_size << this->tree_bits);
        //     }
        // }
        // this->extended_tree_load_log_counter++;
}
#endif
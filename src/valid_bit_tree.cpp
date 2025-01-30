#include <valid_bit_tree.hpp>

#include <util.hpp>
#include <iostream>

// InternalCounterValidBitTreeController::Parameters 
// InternalCounterValidBitTreeController::compute_parameters(CryptoModule *crypto, addr_t levels, addr_t page_size, addr_t valid_bits_per_bucket) {
//     Parameters result;

//     result.levels = levels;
//     result. page_size = page_size;

//     result.auth_tag_size = crypto->auth_tag_size();
//     result.bytes_per_bucket = divide_round_up(valid_bits_per_bucket, 8UL);

//     addr_t buckets_per_page = page_size / result.bytes_per_bucket;

//     result.levels_per_page = num_bits(buckets_per_page + 1) - 1;

//     result.root_page_levels = levels - (levels / result.levels_per_page * result.levels_per_page);

//     result.page_levels = divide_round_up(levels , result.levels_per_page);

//     addr_t page_count = 0;
//     addr_t level_size = 1;

//     for (addr_t level = 0; level < result.page_levels; level++) {
//         page_count += level_size;
//         level_size *= (level == 0 ? 1 << result.root_page_levels : 1 << result.levels_per_page);
//     }

//     result.required_memory_size = page_count * (result.page_size + result.auth_tag_size);

//     return result;
// }

// void 
// InternalCounterValidBitTreeController::read_path(byte_t *key, addr_t path) {
//     addr_t page_count = 0;
//     addr_t level_size = 1;
//     addr_t ignored_bits = this->parameters.levels - 1;
//     // compute addresses
//     for (addr_t page_level = 0; page_level < this->parameters.page_levels; page_level++) {
//         addr_t page_index = path << ignored_bits;

//         addr_t address = (page_count + page_index) * (this->parameters.page_size + this->parameters.auth_tag_size);

//         this->path_access_requests[page_level].address = address;
//         this->path_access_requests[page_level].type = MemoryRequestType::READ;

//         ignored_bits -= (page_level == 0 ? this->parameters.root_page_levels : this->parameters.levels_per_page);
//         page_count += level_size;
//         level_size *= (page_level == 0 ? 1 << this->parameters.root_page_levels : 1 << this->parameters.levels_per_page);
//     }

//     this->memory->batch_access(this->path_access_requests);

//     // decrypt path
//     for (addr_t page_level = 0; page_level < this->parameters.page_levels; page_level++) {
//         std::size_t levels_in_page = (page_level == 0 ? this->parameters.root_page_levels : this->parameters.levels_per_page);
//         std::size_t data_size = (1 << levels_in_page) * this->parameters.bytes_per_bucket;
//         addr_t page_id = this->path_access_requests[page_level].address / (this->parameters.page_size + this->parameters.auth_tag_size);
//         uint64_t counter = this->counters[page_id];
//         //set counter and page id in nonce
//         std::memcpy(this->nonce_buffer.get() + this->parameters.random_nonce_bytes, &page_id, sizeof(addr_t));
//         std::memcpy(this->nonce_buffer.get() + this->parameters.random_nonce_bytes + sizeof(addr_t), &counter, sizeof(addr_t));

//         bool verification_result = crypto_module->decrypt(
//             key, // key
//             this->nonce_buffer.get(), // nonce
//             this->path_access_requests[page_level].data.data(), //ciphertext
//             data_size, // message length
//             this->path_access_requests[page_level].data.data() + data_size, // auth tag,
//             this->decrypted_buffer.get() + page_level * this->parameters.page_size // message
//         );

//         if (!verification_result) {
//             throw std::runtime_error("Auth Tag verification Failed");
//         }
          
//     }
// }

// void
// InternalCounterValidBitTreeController::write_path(byte_t *key, addr_t path) {
//     addr_t page_count = 0;
//     addr_t level_size = 1;
//     addr_t ignored_bits = this->parameters.levels - 1;
//     // compute addresses
//     for (addr_t page_level = 0; page_level < this->parameters.page_levels; page_level++) {
//         addr_t page_index = path << ignored_bits;

//         addr_t address = (page_count + page_index) * (this->parameters.page_size + this->parameters.auth_tag_size);

//         this->path_access_requests[page_level].address = address;
//         this->path_access_requests[page_level].type = MemoryRequestType::READ;

//         ignored_bits -= (page_level == 0 ? this->parameters.root_page_levels : this->parameters.levels_per_page);
//         page_count += level_size;
//         level_size *= (page_level == 0 ? 1 << this->parameters.root_page_levels : 1 << this->parameters.levels_per_page);
//     }

//     // encrypt path
//     for (addr_t page_level = 0; page_level < this->parameters.page_levels; page_level++) {
//         std::size_t levels_in_page = (page_level == 0 ? this->parameters.root_page_levels : this->parameters.levels_per_page);
//         std::size_t data_size = (1 << levels_in_page) * this->parameters.bytes_per_bucket;
//         addr_t page_id = this->path_access_requests[page_level].address / (this->parameters.page_size + this->parameters.auth_tag_size);
//         // increment counter
//         this->counters[page_id]++;
//         uint64_t counter = this->counters[page_id];
//         //set counter and page id in nonce
//         std::memcpy(this->nonce_buffer.get() + this->parameters.random_nonce_bytes, &page_id, sizeof(addr_t));
//         std::memcpy(this->nonce_buffer.get() + this->parameters.random_nonce_bytes + sizeof(addr_t), &counter, sizeof(addr_t));

//         crypto_module->encrypt(
//             key, // key
//             this->nonce_buffer.get(), // nonce
//             this->decrypted_buffer.get() + page_level * this->parameters.page_size, // message
//             data_size, // message length
//             this->path_access_requests[page_level].data.data(), //ciphertext
//             this->path_access_requests[page_level].data.data() + data_size // auth tag,
//         );
          
//     }

//     this->memory->batch_access(this->path_access_requests);


// }


ParentCounterValidBitTreeController::Parameters 
ParentCounterValidBitTreeController::compute_parameters(CryptoModule *crypto, addr_t levels, addr_t page_size, addr_t valid_bits_per_bucket) {
    Parameters result;

    result.levels = levels;
    result.page_size = page_size;

    result.auth_tag_size = crypto->auth_tag_size();
    result.random_nonce_bytes = crypto->nonce_size() - sizeof(std::uint64_t) * 2;
    result.bytes_per_bucket = divide_round_up(valid_bits_per_bucket, 8UL);

    std::cout << absl::StreamFormat("%lu blocks needs %lu bytes of valid bit storage per bucket.\n", valid_bits_per_bucket, result.bytes_per_bucket);

    addr_t non_leaf_entry_size = sizeof(uint64_t) + result.bytes_per_bucket;

    std::cout << absl::StreamFormat("%lu bytes of valid bit storage plus counter is %lu bytes. \n", result.bytes_per_bucket, non_leaf_entry_size);

    addr_t buckets_per_non_leaf_page = page_size / non_leaf_entry_size;
    result.levels_per_non_leaf_page = num_bits(buckets_per_non_leaf_page + 1) - 1;
    result.page_size = (1UL << result.levels_per_non_leaf_page) * non_leaf_entry_size;

    std::cout << absl::StreamFormat("Each non-leaf page of %lu bytes can hold %lu levels\n", result.page_size, result.levels_per_non_leaf_page);
    

    addr_t buckets_per_leaf_page = page_size / result.bytes_per_bucket;
    result.levels_per_leaf_page = num_bits(buckets_per_leaf_page + 1) - 1;

    std::cout << absl::StreamFormat("Each leaf page of %lu bytes can hold %lu levels\n", page_size, result.levels_per_leaf_page);

    addr_t non_leaf_levels = divide_round_up(levels - result.levels_per_leaf_page, result.levels_per_non_leaf_page);

    std::cout << absl::StreamFormat("%lu levels of non-leaf pages are required\n", non_leaf_levels);

    result.levels_per_leaf_page = levels - result.levels_per_non_leaf_page * non_leaf_levels;
    result.leaf_page_size = (1UL << result.levels_per_leaf_page) * result.bytes_per_bucket;

    std::cout << absl::StreamFormat("Leaf Page has %lu levels and is %lu bytes in size.\n", result.levels_per_leaf_page, result.leaf_page_size);

    result.page_levels = non_leaf_levels + 1;

    addr_t level_start = 0;
    addr_t level_size = 1;
    // compute addresses
    for (addr_t page_level = 0; page_level < result.page_levels; page_level++) {
        // addr_t level_start = level_start_in_pages * (this->parameters.page_size + this->parameters.auth_tag_size);
        addr_t page_size_in_this_page_level = (page_level == result.page_levels - 1 ? result.leaf_page_size : result.page_size) + result.auth_tag_size;
        addr_t levels_per_page_in_this_page_level = (page_level == result.page_levels - 1 ? result.levels_per_leaf_page : result.levels_per_non_leaf_page);
        level_start += level_size * page_size_in_this_page_level;
        level_size *= (1 << levels_per_page_in_this_page_level);
    }

    result.required_memory_size = level_start;

    // result.levels_per_page = num_bits(buckets_per_page + 1) - 1;

    // compute level start offsets

    return result;
    
}

ParentCounterValidBitTreeController::ParentCounterValidBitTreeController(Parameters parameters, CryptoModule *crypto_module, Memory* memory) :
parameters(parameters),
nonce_buffer(crypto_module->nonce_size()),
decrypted_buffer((parameters.page_levels - 1) * parameters.page_size + parameters.leaf_page_size),
path_access_requests(parameters.page_levels),
content_encrypted(false),
root_counter(0),
memory(memory),
crypto_module(crypto_module)
{
    crypto_module->random(nonce_buffer.data(), parameters.random_nonce_bytes);
}

void 
ParentCounterValidBitTreeController::read_path(byte_t *key, addr_t path) {
    if (!this->content_encrypted) {
        throw std::runtime_error("Path read attempted while memory is not encrypted");
    }

    // std::cout << absl::StreamFormat("Reading path %lu\n", path);
    currently_loaded_path = path;
    addr_t level_start = 0;
    addr_t level_size = 1;
    addr_t ignored_bits = this->parameters.levels - 1;
    // compute addresses
    for (addr_t page_level = 0; page_level < this->parameters.page_levels; page_level++) {
        addr_t page_index = path >> ignored_bits;
        // addr_t level_start = level_start_in_pages * (this->parameters.page_size + this->parameters.auth_tag_size);
        addr_t page_size_in_this_page_level = (page_level == this->parameters.page_levels - 1 ? this->parameters.leaf_page_size : this->parameters.page_size) + this->parameters.auth_tag_size;
        addr_t levels_per_page_in_this_page_level = (page_level == this->parameters.page_levels - 1 ? this->parameters.levels_per_leaf_page : this->parameters.levels_per_non_leaf_page);
        addr_t address = level_start + page_index * page_size_in_this_page_level;

        this->path_access_requests[page_level].address = address;
        this->path_access_requests[page_level].type = MemoryRequestType::READ;
        this->path_access_requests[page_level].size = page_size_in_this_page_level;
        this->path_access_requests[page_level].data.resize(page_size_in_this_page_level);

        ignored_bits -= levels_per_page_in_this_page_level;
        level_start += level_size * page_size_in_this_page_level;
        level_size *= (1 << levels_per_page_in_this_page_level);
    }

    this->memory->batch_access(this->path_access_requests);

    // decrypt path
    ignored_bits = this->parameters.levels - 1;
    for (addr_t page_level = 0; page_level < this->parameters.page_levels; page_level++) {
        std::uint64_t counter;
        addr_t page_index = path >> ignored_bits;
        // std::cout << page_index << "\n";
        addr_t page_size_in_this_page_level = (page_level == this->parameters.page_levels - 1 ? this->parameters.leaf_page_size : this->parameters.page_size) + this->parameters.auth_tag_size;
        addr_t levels_per_page_in_this_page_level = (page_level == this->parameters.page_levels - 1 ? this->parameters.levels_per_leaf_page : this->parameters.levels_per_non_leaf_page);
        if (page_level == 0) {
            counter = root_counter;
        } else {
            // read counter from parent
            addr_t counter_offset_in_parent = page_index % (1UL <<this->parameters.levels_per_non_leaf_page);
            counter = this->get_counter_from_page(this->get_decrypted_page(page_level - 1), counter_offset_in_parent);
            // std::memcpy(&counter, this->decrypted_buffer.data() + (page_level - 1) * this->parameters.page_size + counter_offset_in_parent * sizeof(uint64_t), sizeof(uint64_t));
        }

        //set counter and page id in nonce
        std::memcpy(this->nonce_buffer.data() + this->parameters.random_nonce_bytes, &this->path_access_requests[page_level].address, sizeof(addr_t));
        std::memcpy(this->nonce_buffer.data() + this->parameters.random_nonce_bytes + sizeof(addr_t), &counter, sizeof(addr_t));

        bool verification_result = crypto_module->decrypt(
            key, // key
            this->nonce_buffer.data(), // nonce
            this->path_access_requests[page_level].data.data(), //ciphertext
            page_size_in_this_page_level - this->parameters.auth_tag_size, // message length
            this->path_access_requests[page_level].data.data() + page_size_in_this_page_level - this->parameters.auth_tag_size, // auth tag,
            this->decrypted_buffer.data() + page_level * this->parameters.page_size // message
        );

        if (!verification_result) {
            throw std::runtime_error("Auth Tag verification Failed");
        }
        ignored_bits -= levels_per_page_in_this_page_level;
    }
}

void 
ParentCounterValidBitTreeController::write_path(byte_t *key) {
    if (!this->content_encrypted) {
        throw std::runtime_error("Path write attempted while memory is not encrypted");
    }

    if (!currently_loaded_path.has_value()) {
        throw std::runtime_error("Path write attempted without a previous read path");
    }
    addr_t path = currently_loaded_path.value();
    // addr_t level_start = 0;
    // addr_t level_size = 1;
    // addr_t ignored_bits = this->parameters.levels - 1;
    // // compute addresses
    // for (addr_t page_level = 0; page_level < this->parameters.page_levels; page_level++) {
    //     addr_t page_index = path << ignored_bits;
    //     // addr_t level_start = level_start_in_pages * (this->parameters.page_size + this->parameters.auth_tag_size);
    //     addr_t page_size_in_this_page_level = (page_level == this->parameters.page_levels - 1 ? this->parameters.leaf_page_size : this->parameters.page_size) + this->parameters.auth_tag_size;
    //     addr_t levels_per_page_in_this_page_level = (page_level == this->parameters.page_levels - 1 ? this->parameters.levels_per_leaf_page : this->parameters.levels_per_non_leaf_page);
    //     addr_t address = level_start + page_index * page_size_in_this_page_level;

    //     this->path_access_requests[page_level].address = address;
    //     this->path_access_requests[page_level].type = MemoryRequestType::WRITE;

    //     ignored_bits -= levels_per_page_in_this_page_level;
    //     level_start += level_size * page_size_in_this_page_level;
    //     level_size *= (1 << levels_per_page_in_this_page_level);
    // }

    // this->memory->batch_access(this->path_access_requests);

    // encrypt path
    addr_t ignored_bits = this->parameters.levels_per_leaf_page - 1;
    this->root_counter += 1;
    for (addr_t i = 0; i < this->parameters.page_levels; i++) {
        addr_t page_level = this->parameters.page_levels - i - 1;
        std::uint64_t counter;
        addr_t page_index = path >> ignored_bits;
        // std::cout << page_index << "\n";
        addr_t page_size_in_this_page_level = (page_level == this->parameters.page_levels - 1 ? this->parameters.leaf_page_size : this->parameters.page_size) + this->parameters.auth_tag_size;
        if (page_level == 0) {
            counter = root_counter;
        } else {
            // read counter from parent
            addr_t counter_offset_in_parent = page_index % (1 <<  this->parameters.levels_per_non_leaf_page);
            counter = this->get_counter_from_page(this->get_decrypted_page(page_level - 1), counter_offset_in_parent);
            counter += 1;
            this->set_counter_in_page(this->get_decrypted_page(page_level - 1), counter_offset_in_parent, counter);
        }

        this->path_access_requests[page_level].type = MemoryRequestType::WRITE;

        //set counter and page id in nonce
        std::memcpy(this->nonce_buffer.data() + this->parameters.random_nonce_bytes, &this->path_access_requests[page_level].address, sizeof(addr_t));
        std::memcpy(this->nonce_buffer.data() + this->parameters.random_nonce_bytes + sizeof(addr_t), &counter, sizeof(addr_t));

        crypto_module->encrypt(
            key,
            this->nonce_buffer.data(),
            this->get_decrypted_page(page_level),
            page_size_in_this_page_level - this->parameters.auth_tag_size,
            this->path_access_requests[page_level].data.data(),
            this->path_access_requests[page_level].data.data() + page_size_in_this_page_level - this->parameters.auth_tag_size
        );

        ignored_bits += this->parameters.levels_per_non_leaf_page;
    }

    this->memory->batch_access(this->path_access_requests);
}

addr_t 
ParentCounterValidBitTreeController::get_address_of(addr_t level, addr_t bucket_index) const noexcept {
    addr_t page_level = level / this->parameters.levels_per_non_leaf_page;

    if (page_level >= this->parameters.page_levels) {
        // deal with the fact the leaf may be larger than non-leaf pages
        page_level = this->parameters.page_levels - 1;
    }

    addr_t level_start = 0;
    addr_t level_size = 1;
    for (addr_t i = 0; i < page_level; i++) {
        level_start += level_size * (this->parameters.page_size + this->parameters.auth_tag_size);
        level_size *= (1UL << this->parameters.levels_per_non_leaf_page);
    }

    addr_t bucket_level_in_page_level = level - page_level * this->parameters.levels_per_non_leaf_page;
    addr_t page_index = bucket_index / (1 << bucket_level_in_page_level);
    addr_t bucket_index_in_page = bucket_index % (1 << bucket_level_in_page_level);

    addr_t offset_in_page = ((1UL << bucket_level_in_page_level) - 1 + bucket_index_in_page) * this->parameters.bytes_per_bucket;
    addr_t page_size_in_this_page_level = (page_level == this->parameters.page_levels - 1 ? this->parameters.leaf_page_size : this->parameters.page_size) + this->parameters.auth_tag_size;
    
    return level_start + page_index * page_size_in_this_page_level + offset_in_page;
}

void 
ParentCounterValidBitTreeController::encrypt_contents(byte_t *key) {
    MemoryRequest parent_request(MemoryRequestType::READ, 0, this->parameters.page_size);
    MemoryRequest data_request(MemoryRequestType::READ, 0, this->parameters.page_size);
    bytes_t data_buffer(this->parameters.page_size);
    for (std::uint64_t i = 0; i < this->parameters.page_levels; i++) {
        auto level = this->parameters.page_levels - 1 - i;
        if (level != 0) {
            auto page_size_this_level = 
                (level == this->parameters.page_levels - 1? this->parameters.leaf_page_size: this->parameters.page_size) + this->parameters.auth_tag_size;
            auto parent_page_size = this->parameters.page_size + this->parameters.auth_tag_size;
            auto level_start = get_start_address_for_page_level(level);
            auto parent_level_start = get_start_address_for_page_level(level - 1);
            auto level_size = get_page_level_size_in_pages(level);
            std::optional<std::uint64_t> loaded_parent_index;
            for (addr_t j = 0; j < level_size; j++) {
                auto parent_index = j / (1UL << this->parameters.levels_per_non_leaf_page);
                auto child_index_in_parent = j % (1 << this->parameters.levels_per_non_leaf_page);
                if (!loaded_parent_index.has_value() || parent_index != loaded_parent_index.value()) {
                    if (loaded_parent_index.has_value()) {
                        parent_request.type = MemoryRequestType::WRITE;
                        this->memory->access(parent_request);
                    }
                    parent_request.type = MemoryRequestType::READ;
                    parent_request.address = parent_level_start + parent_index * parent_page_size;
                    this->memory->access(parent_request);
                    loaded_parent_index = parent_index;
                }

                data_request.type = MemoryRequestType::READ;
                data_request.address = level_start + j * page_size_this_level;
                data_request.size = page_size_this_level;
                data_request.data.resize(page_size_this_level);

                this->memory->access(data_request);
                
                data_buffer.resize(page_size_this_level);
                // copy contents into the buffer
                std::memcpy(data_buffer.data(), data_request.data.data(), page_size_this_level - this->parameters.auth_tag_size);

                // get and update counter in parent
                auto counter = get_counter_from_page(parent_request.data.data(), child_index_in_parent);
                counter += 1;
                set_counter_in_page(parent_request.data.data(), child_index_in_parent, counter);

                //set counter and page id in nonce
                std::memcpy(this->nonce_buffer.data() + this->parameters.random_nonce_bytes, &data_request.address, sizeof(addr_t));
                std::memcpy(this->nonce_buffer.data() + this->parameters.random_nonce_bytes + sizeof(addr_t), &counter, sizeof(addr_t));

                // encrypt 
                this->crypto_module->encrypt(
                    key, 
                    this->nonce_buffer.data(), 
                    data_buffer.data(), 
                    page_size_this_level - this->parameters.auth_tag_size, 
                    data_request.data.data(), 
                    data_request.data.data() + page_size_this_level - this->parameters.auth_tag_size
                );

                // write back
                data_request.type = MemoryRequestType::WRITE;
                this->memory->access(data_request);

            }
            if (loaded_parent_index.has_value()) {
                parent_request.type = MemoryRequestType::WRITE;
                this->memory->access(parent_request);
            }
        } else {
            // hardcoded exception for the root
            data_request.type = MemoryRequestType::READ;
            data_request.address = 0;
            data_request.size = this->parameters.page_size + this->parameters.auth_tag_size;
            data_request.data.resize(this->parameters.page_size + this->parameters.auth_tag_size);

            this->memory->access(data_request);

            // copy contents into the buffer
            std::memcpy(data_buffer.data(), data_request.data.data(), this->parameters.page_size);
            //set counter and page id in nonce
            this->root_counter += 1;
            std::memcpy(this->nonce_buffer.data() + this->parameters.random_nonce_bytes, &data_request.address, sizeof(addr_t));
            std::memcpy(this->nonce_buffer.data() + this->parameters.random_nonce_bytes + sizeof(addr_t), &(this->root_counter), sizeof(addr_t));

            // encrypt 
            this->crypto_module->encrypt(
                key, 
                this->nonce_buffer.data(), 
                data_buffer.data(), 
                this->parameters.page_size, 
                data_request.data.data(), 
                data_request.data.data() + this->parameters.page_size
            );

            data_request.type = MemoryRequestType::WRITE;
            this->memory->access(data_request);

        }
    }   
    this->content_encrypted = true;
}

void 
ParentCounterValidBitTreeController::decrypt_contents(byte_t *key) {
    this->content_encrypted = false;
    throw std::runtime_error("Not Implemented");
}

toml::table 
ParentCounterValidBitTreeController::to_toml() const noexcept {
    toml::table table;
    table.emplace("levels", size_to_string(this->parameters.levels));
    table.emplace("page_levels", size_to_string(this->parameters.page_levels));
    table.emplace("levels_per_non_leaf_page", size_to_string(this->parameters.levels_per_non_leaf_page));
    table.emplace("levels_per_leaf_page", size_to_string(this->parameters.levels_per_leaf_page));
    table.emplace("page_size", size_to_string(this->parameters.page_size));
    table.emplace("leaf_page_size", size_to_string(this->parameters.leaf_page_size));
    table.emplace("auth_tag_size", size_to_string(this->parameters.auth_tag_size));
    table.emplace("bytes_per_bucket", size_to_string(this->parameters.bytes_per_bucket));
    table.emplace("required_memory_size", size_to_string(this->parameters.required_memory_size));
    table.emplace("random_nonce_bytes", size_to_string(this->parameters.random_nonce_bytes));
    table.emplace("random_nonce", bytes_to_hex_string(this->nonce_buffer.data(), this->parameters.random_nonce_bytes));
    table.emplace("root_counter", size_to_string(root_counter));
    table.emplace("content_encrypted", this->content_encrypted);

    return table;
}

ParentCounterValidBitTreeController::ParentCounterValidBitTreeController(const toml::table &table, CryptoModule *crypto_module, Memory* memory) :
parameters (Parameters{
    .levels = parse_size(table["levels"]),
    .page_levels = parse_size(table["page_levels"]),
    .levels_per_non_leaf_page = parse_size(table["levels_per_non_leaf_page"]),
    .levels_per_leaf_page = parse_size(table["levels_per_leaf_page"]),
    .page_size = parse_size(table["page_size"]),
    .leaf_page_size = parse_size(table["leaf_page_size"]),
    .auth_tag_size = parse_size(table["auth_tag_size"]),
    .bytes_per_bucket = parse_size(table["bytes_per_bucket"]),
    .required_memory_size = parse_size(table["required_memory_size"]),
    .random_nonce_bytes = parse_size(table["random_nonce_bytes"])
}),
nonce_buffer(crypto_module->nonce_size()),
decrypted_buffer((parameters.page_levels - 1) * parameters.page_size + parameters.leaf_page_size),
path_access_requests(parameters.page_levels),
content_encrypted(table["content_encrypted"].value<bool>().value()),
root_counter(parse_size(table["root_counter"])),
memory(memory),
crypto_module(crypto_module)
{   
    if (crypto_module->auth_tag_size() != this->parameters.auth_tag_size) {
        throw std::runtime_error("Mismatched auth tag size");
    }
    this->nonce_buffer.resize(crypto_module->nonce_size());
    hex_string_to_bytes(table["random_nonce"].value<std::string_view>().value(), nonce_buffer.data(), this->parameters.random_nonce_bytes);
}
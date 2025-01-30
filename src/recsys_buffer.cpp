#include <recsys_buffer.hpp>
#include <conditional_memcpy.hpp>
#include <limits>
#include <absl/strings/str_format.h>
#include <iostream>
#include <oram_builders.hpp>
#include <binary_path_oram_2.hpp>
#include <util.hpp>
#include <cmath>
#include <simple_memory.hpp>
#include <memory_interface.hpp>
#include <dummy_memory.hpp>
#include <page_optimized_raw_oram.hpp>

#include <union.hpp>
#include <exponential_dp.hpp>

LinearScanBuffer::LinearScanBuffer(
    unique_memory_t &&memory,
    std::uint64_t max_size,
    bool enable_non_secure_mode
) : 
max_size(max_size),
_entry_size(memory->page_size()),
enable_non_secure_mode(enable_non_secure_mode),
num_entries_in_buffer(0),
num_entries_downloaded(0),
memory(std::move(memory))
{
    this->data_buffer.resize(this->max_size * this->_entry_size);
    this->gradient_buffer.resize(this->max_size * this->_entry_size);
    this->counter_buffer.resize(this->max_size);
    for (std::uint64_t i = 0; i < this->max_size; i++) {
        this->entry_id_buffer.emplace_back(std::numeric_limits<std::uint64_t>::max());
    }
}

std::uint64_t 
LinearScanBuffer::num_entries() const {
    return this->memory->size() / this->memory->page_size();
}
std::uint64_t 
LinearScanBuffer::entry_size() const {
    return this->memory->page_size();
}
    
void 
LinearScanBuffer::download(std::uint64_t entry_id) {
    // bytes_t entry_data(this->_entry_size);
    auto start = std::chrono::steady_clock::now();
    BufferEntry entry(this->_entry_size);
    entry.entry_id = entry_id;
    MemoryRequest request(MemoryRequestType::POP, entry_id * this->_entry_size, this->_entry_size);
    const MemoryRequestType pop_dummy = MemoryRequestType::DUMMY_POP;
    bool found = find_and_remove_block_from_buffer(entry);

    if (!this->enable_non_secure_mode || !found) {
        auto oram_start = std::chrono::steady_clock::now();
        conditional_memcpy(found, &request.type, &pop_dummy, sizeof(MemoryRequestType));
        this->memory->access(request);
        conditional_memcpy(!found, entry.data.data(), request.data.data(), this->_entry_size);
        auto oram_end = std::chrono::steady_clock::now();
        this->oram_time += (oram_end - oram_start);
    }

    // std::cout << absl::StreamFormat("Reading entry %lu\n", entry_id);
    this->num_entries_downloaded += 1;
    this->num_entries_in_buffer += (found ? 0: 1);
    this->place_block_on_buffer(entry);
    auto end = std::chrono::steady_clock::now();
    this->overall_time += end - start;
}

void 
LinearScanBuffer::aggregate(std::uint64_t entry_id) {
    // bytes_t entry_data(this->_entry_size);
    auto start = std::chrono::steady_clock::now();
    bool found = this->update_gradient(entry_id);

    if (!found) {
        throw std::runtime_error("Update entry id not in buffer!");
    }
    // do update?
    // std::cout << absl::StreamFormat("Updating entry %lu\n", entry_id);

    // this->place_block_on_buffer(entry_id, entry_data.data());
    auto end = std::chrono::steady_clock::now();
    this->overall_time += end - start;
}

// bool 
// LinearScanBuffer::find_and_remove_block_from_buffer(std::uint64_t entry_id, byte_t *buffer) {
//     bool found = false;
//     std::uint64_t invalid_entry_id = std::numeric_limits<std::uint64_t>::max();
//     for (std::uint64_t index = 0; index < this->max_size; index++) {
//         bool is_target = (this->entry_id_buffer[index] == entry_id);
//         conditional_memcpy(is_target, buffer, this->data_buffer.data() + this->_entry_size * index, _entry_size);
//         conditional_memcpy(is_target, &this->entry_id_buffer[index], &invalid_entry_id, sizeof(std::uint64_t));
//         found |= is_target;
//     }
//     return found;
// }

// void 
// LinearScanBuffer::place_block_on_buffer(std::uint64_t entry_id, byte_t *buffer) {
//     bool completed = false;
//     for (std::uint64_t index = 0; index < this->max_size; index++) {
//         bool is_free = (this->entry_id_buffer[index] == std::numeric_limits<std::uint64_t>::max());
//         bool do_place = is_free && !completed;
//         conditional_memcpy(do_place, this->data_buffer.data() + this->_entry_size * index, buffer, _entry_size);
//         conditional_memcpy(do_place, &this->entry_id_buffer[index], &entry_id, sizeof(std::uint64_t));
//         completed |= do_place;
//     }

//     if (!completed) {
//         throw std::runtime_error("Buffer is full!");
//     }
// }

void 
LinearScanBuffer::update_flush_buffer() {
    auto start = std::chrono::steady_clock::now();
    MemoryRequest request(MemoryRequestType::PUSH, 0, this->_entry_size);
    const std::uint64_t zero = 0;
    const MemoryRequestType dummy_push = MemoryRequestType::DUMMY_PUSH;
    for (std::uint64_t index = 0; index < this->max_size; index++) {
        bool is_free = (this->entry_id_buffer[index] == std::numeric_limits<std::uint64_t>::max());
        if (!is_free || !this->enable_non_secure_mode) {
            // push blocks back into the ORAM
            std::memcpy(request.data.data(), this->data_buffer.data() + this->_entry_size * index, this->_entry_size);
            request.address = this->entry_id_buffer[index] * this->_entry_size;
            request.type = MemoryRequestType::PUSH;

            // update base on gradient
            bool valid = (this->gradient_buffer[this->_entry_size * index] == (this->counter_buffer[index] & 0xFF));

            if (!(is_free || valid)) {
                throw std::runtime_error("validation failed");
            }

            conditional_memcpy(is_free, &request.address, &zero, sizeof(std::uint64_t));
            conditional_memcpy(is_free, &request.type, &dummy_push, sizeof(MemoryRequestType));

            // if (!is_free) {
            //     std::cout << absl::StreamFormat("Flushing entry %lu back to oram\n", this->entry_id_buffer[index]);
            // }

            auto oram_start = std::chrono::steady_clock::now();
            this->memory->access(request);
            auto oram_end = std::chrono::steady_clock::now();
            this->oram_time += (oram_end - oram_start);
        }

        this->entry_id_buffer[index] = std::numeric_limits<std::uint64_t>::max();

        this->num_entries_downloaded = 0;
        this->num_entries_in_buffer = 0;
    }
    auto end = std::chrono::steady_clock::now();
    this->overall_time += end - start;
}

// std::chrono::nanoseconds 
// LinearScanBuffer::get_overall_time() {
//     return this->overall_time;
// }

// std::chrono::nanoseconds 
// LinearScanBuffer::get_oram_time() {
//     return this->oram_time;
// } 
OramBuffer::OramBuffer(
    unique_memory_t &&memory,
    std::uint64_t max_size,
    bool enable_non_secure_mode,
    UpdateMode update_mode,
    unique_memory_t &&buffer_oram
) : 
max_size(max_size),
enable_non_secure_mode(enable_non_secure_mode),
_entry_size(memory->page_size()),
buffer_entry_size(this->_entry_size * 2 + 2),
update_mode(update_mode),
entry_id_size(
    divide_round_up(num_bits(memory->size() / memory->page_size() - 1), 8UL)
),
memory(std::move(memory)),
buffer(std::move(buffer_oram)),
num_downloads(0),
num_blocks_in_buffer(0),
main_oram_request(MemoryRequestType::POP, 0, this->_entry_size),
buffer_request(MemoryRequestType::POP, 0, this->buffer_entry_size)
{
    if (this->update_mode == UpdateMode::POP_N_PUSH) {
        this->entry_id_buffer.resize(this->entry_id_size * this->max_size);
    }

    if (this->buffer == nullptr) {
        double load_factor = 0.90;
        double phony_load_factor = static_cast<double>(this->num_entries()) / (static_cast<double>(this->max_size) / load_factor);

        std::cout << "No buffer given\n";
        std::cout << "Generating Buffer ORAM, this may take a while\n";
        this->buffer = createBinaryPathOram2(
            buffer_entry_size * this->num_entries(), buffer_entry_size, buffer_entry_size * 5, false, 4096, true, 0, phony_load_factor, false, 1, "AEGIS256"
        );
        this->buffer->init();
    }
}

void 
OramBuffer::download(std::uint64_t entry_id) {
    auto overall_time_start = std::chrono::steady_clock::now();
    this->num_downloads ++;

    if (this->update_mode == UpdateMode::POP_N_PUSH) {
        std::memcpy(this->entry_id_buffer.data() + this->entry_id_size * (this->num_downloads - 1), &entry_id, this->entry_id_size);
    }

    // put entry_id into buffer
    this->buffer_request.type = POP;
    this->buffer_request.address = entry_id * this->buffer_entry_size;

    std::memset(this->buffer_request.data.data(), 0, this->buffer_entry_size);

    this->buffer->access(buffer_request);

    bool found = (buffer_request.address == entry_id * this->buffer_entry_size);

    this->num_blocks_in_buffer += (found ? 0: 1);

    if (!found || !this->enable_non_secure_mode) {
        this->main_oram_request.type = POP;
        this->main_oram_request.address = entry_id * this->_entry_size;
        
        const MemoryRequestType dp = MemoryRequestType::DUMMY_POP;

        conditional_memcpy(found, &(this->main_oram_request.type), &dp, sizeof(MemoryRequestType));

        auto oram_start = std::chrono::steady_clock::now();
        this->memory->access(main_oram_request);
        auto oram_end = std::chrono::steady_clock::now();
        this->oram_time += (oram_end - oram_start);

        // copy entry data into buffer
        conditional_memcpy(!found, this->buffer_request.data.data(), this->main_oram_request.data.data(), this->_entry_size);
    }

    this->buffer_request.type = PUSH;
    this->buffer_request.address = entry_id * this->buffer_entry_size;

    this->buffer->access(buffer_request);

    auto overall_time_end = std::chrono::steady_clock::now();
    this->overall_time += (overall_time_end - overall_time_start);
}

void 
OramBuffer::aggregation_increment(byte_t *entry, const byte_t * _dummy) {
    uint16_t counter = 0;
    std::memcpy(&counter, entry + 2 * this->_entry_size, sizeof(std::uint16_t));
    counter += 1;
    std::memcpy(entry + 2 * this->_entry_size, &counter, sizeof(std::uint16_t));

    entry[this->_entry_size] += 1;
}

void 
OramBuffer::aggregate(std::uint64_t entry_id) {
    auto overall_time_start = std::chrono::steady_clock::now();
    this->buffer_request.type = UPDATE;
    this->buffer_request.address = entry_id * this->buffer_entry_size;
    this->buffer_request.update_function = [this](byte_t * entry, const byte_t * _dummy) {
        this->aggregation_increment(entry, _dummy);
    };

    std::memset(this->buffer_request.data.data(), 0, this->buffer_entry_size);

    this->buffer->access(buffer_request);

    // bool found = (buffer_request.address == entry_id * this->buffer_entry_size);

    // if (!found) {
    //     throw std::runtime_error("Requested Block not found in disk!");
    // }

    // uint16_t counter = 0;
    // std::memcpy(&counter, buffer_request.data.data() + 2 * this->_entry_size, sizeof(std::uint16_t));
    // counter += 1;
    // std::memcpy(buffer_request.data.data() + 2 * this->_entry_size, &counter, sizeof(std::uint16_t));

    // buffer_request.data[this->_entry_size] += 1;

    // this->buffer_request.type = PUSH;
    // this->buffer_request.address = entry_id * this->buffer_entry_size;

    // this->buffer->access(buffer_request);
    auto overall_time_end = std::chrono::steady_clock::now();
    this->overall_time += (overall_time_end - overall_time_start);
}

void 
OramBuffer::update_flush_buffer() {
    auto overall_time_start = std::chrono::steady_clock::now();
    if (this->update_mode == UpdateMode::POP_N_PUSH) {
        for (std::uint64_t i = 0; i < this->num_downloads; i++) {
            std::uint64_t entry_id = 0;

            std::memcpy(&entry_id, this->entry_id_buffer.data() + i * this->entry_id_size, this->entry_id_size);

            this->buffer_request.type = POP;
            this->buffer_request.address = entry_id * this->buffer_entry_size;

            std::memset(this->buffer_request.data.data(), 0, this->buffer_entry_size);

            this->buffer->access(buffer_request);

            bool valid = (buffer_request.address == entry_id * this->buffer_entry_size);

            // if (valid) {
            //     std::cout << absl::StreamFormat("Removing block %lu from buffer\n", entry_id);
            // }

            this->main_oram_request.type = PUSH;
            this->main_oram_request.address = entry_id * this->_entry_size;
        
            const MemoryRequestType dp = MemoryRequestType::DUMMY_PUSH;

            conditional_memcpy(!valid, &(this->main_oram_request.type), &dp, sizeof(MemoryRequestType));

            std::memcpy(this->main_oram_request.data.data(), this->buffer_request.data.data(), this->_entry_size);

            auto oram_start = std::chrono::steady_clock::now();
            this->memory->access(main_oram_request);
            auto oram_end = std::chrono::steady_clock::now();
            this->oram_time += (oram_end - oram_start);
        }
    } else {
        dynamic_cast<BinaryPathOram2 *>(this->buffer.get())->empty_oram(
            [this](const BlockMetadata *metadata, const byte_t *data) {
                this->flush_block_callback(metadata, data);
            }
        );
    }

    this->num_blocks_in_buffer = 0;
    this->num_downloads = 0;

    auto overall_time_end = std::chrono::steady_clock::now();
    this->overall_time += (overall_time_end - overall_time_start);
}

void 
OramBuffer::flush_block_callback(const BlockMetadata *metadata, const byte_t * data) {
    bool is_valid = metadata->is_valid();

    if (is_valid || !this->enable_non_secure_mode) {
        this->main_oram_request.type = PUSH;
        this->main_oram_request.address = metadata->get_block_index() * this->_entry_size;

        const MemoryRequestType dp = MemoryRequestType::DUMMY_POP;

        conditional_memcpy(!is_valid, &(this->main_oram_request.type), &dp, sizeof(MemoryRequestType));

        uint16_t counter = 0;
        std::memcpy(&counter, data + 2 * this->_entry_size, sizeof(std::uint16_t));

        bool counter_valid = (data[this->_entry_size] == (counter & 0xFF));

        if (!(!is_valid || counter_valid)) {
            throw std::runtime_error("validation failed");
        }

        std::memcpy(this->main_oram_request.data.data(), data, this->_entry_size);

        auto oram_start = std::chrono::steady_clock::now();
        this->memory->access(main_oram_request);
        auto oram_end = std::chrono::steady_clock::now();
        this->oram_time += (oram_end - oram_start);
    }
}

OramBuffer3::OramBuffer3(
    unique_memory_t &&memory,
    std::uint64_t max_size,
    bool enable_non_secure_mode,
    BufferORAMType buffer_oram_type
) :
max_size(max_size),
enable_non_secure_mode(enable_non_secure_mode),
_entry_size(memory->page_size()),
buffer_entry_size(this->_entry_size * 2 + 2),
entry_id_size(
    divide_round_up(num_bits(memory->size() / memory->page_size() - 1), 8UL)
),
memory(std::move(memory)),
num_downloads(0),
num_blocks_in_buffer(0),
main_oram_block_buffer(_entry_size),
buffer_oram_block_buffer(buffer_entry_size)
{
    this->entry_id_buffer.resize(this->entry_id_size * this->max_size);
    this->is_entry_duplicate.resize(this->max_size);

    double load_factor = 0.75;
    std::unique_ptr<CryptoModule> crypto_module = std::make_unique<AEGIS256Module>();

    if (buffer_oram_type == BufferORAMType::PathORAM) {
        
        // double phony_load_factor = static_cast<double>(this->num_entries()) / (static_cast<double>(this->max_size) / load_factor);

        std::cout << "No buffer given\n";
        std::cout << "Generating Buffer ORAM, this may take a while\n";

        std::uint64_t num_slots_needed = static_cast<std::uint64_t>(ceil(static_cast<double>(this->max_size) / load_factor));

        std::cout << absl::StreamFormat("To maintain an max load factor of %lf when holding %lu entries the buffer oram need at least %lu slots\n", load_factor, this->max_size, num_slots_needed);

        auto parameters = BinaryPathOram2::compute_parameters_known_oram_size(this->buffer_entry_size, buffer_entry_size * 5, 1, this->num_entries(), crypto_module.get(), num_slots_needed, false);

        uint64_t untrusted_memory_size = ((1UL << parameters.levels) - 1) * parameters.bucket_size;
        auto untrusted_memory = BackedMemory::create("buffer_untrusted_memory", untrusted_memory_size, parameters.bucket_size);

        std::uint64_t position_map_page_size = 64;
        std::uint64_t num_position_map_entires_per_page = position_map_page_size / parameters.path_index_size;
        position_map_page_size = num_position_map_entires_per_page * parameters.path_index_size;
        std::cout << absl::StreamFormat("Each position map page of %lu bytes can hold %lu position map entry of %lu bytes\n", position_map_page_size, num_position_map_entires_per_page, parameters.path_index_size);
        std::uint64_t num_position_map_pages = divide_round_up(parameters.num_blocks, num_position_map_entires_per_page);
        uint64_t position_map_size = num_position_map_pages * position_map_page_size;
        std::cout << absl::StreamFormat("Position map needs %lu pages totaling %lu bytes to hold %lu entries\n", num_position_map_pages, position_map_size, parameters.num_blocks);

        unique_memory_t dummy_position_map = DummyMemory::create("buffer_dummy_position_map", position_map_size, position_map_page_size);

        this->buffer = BinaryPathOram2::create(
            "buffer_oram",
            std::move(dummy_position_map), std::move(untrusted_memory), 
            std::move(crypto_module),
            parameters,
            parameters.levels,
            false
        );
    } else {
        const uint64_t target_z = 8;
        const uint64_t a = 8;
        load_factor = 0.375;
        double phony_load_factor = static_cast<double>(this->num_entries()) / (static_cast<double>(this->max_size) / load_factor);
        auto parameters = PageOptimizedRAWOram::compute_parameters(this->buffer_entry_size * (target_z + 1), this->buffer_entry_size, this->num_entries(), 2, crypto_module.get(), phony_load_factor, false);
        auto valid_bit_tree_parameters = ParentCounterValidBitTreeController::compute_parameters(
            crypto_module.get(), 
            parameters.levels, 
            512, 
            parameters.blocks_per_bucket
        );
        unique_memory_t valid_bit_tree_memory = BackedMemory::create("Valid bit tree memory", valid_bit_tree_parameters.required_memory_size);
        std::unique_ptr<ValidBitTreeController> valid_bit_tree_controller = std::make_unique<ParentCounterValidBitTreeController>(valid_bit_tree_parameters, crypto_module.get(), valid_bit_tree_memory.get());
        // uint64_t untrusted_memory_size = (1UL << levels) * blocks_per_bucket * (block_size + sizeof(BlockMetadata));
        std::uint64_t position_map_page_size = 64;
        std::uint64_t num_position_map_entires_per_page = position_map_page_size / parameters.path_index_size;
        position_map_page_size = num_position_map_entires_per_page * parameters.path_index_size;
        std::cout << absl::StreamFormat("Each position map page of %lu bytes can hold %lu position map entry of %lu bytes\n", position_map_page_size, num_position_map_entires_per_page, parameters.path_index_size);
        std::uint64_t num_position_map_pages = divide_round_up(this->max_size, num_position_map_entires_per_page);
        uint64_t position_map_size = num_position_map_pages * position_map_page_size;
        std::cout << absl::StreamFormat("Position map needs %lu pages totaling %lu bytes to hold %lu entries\n", num_position_map_pages, position_map_size, this->max_size);
        // uint64_t bitfield_size = divide_round_up((1UL << levels) * blocks_per_bucket, 64UL * 8UL) * 64UL;

        // std::shared_ptr<std::ostream> untrusted_memory_log = std::shared_ptr<std::ostream>(new std::ofstream(absl::StrFormat("level-%lu_untrusted_memory.log\n", recursive_level)));

        unique_memory_t untrusted_memory = BackedMemory::create("buffer_untrusted_memory", parameters.untrusted_memory_size, parameters.untrusted_memory_page_size);

        unique_memory_t position_map = DummyMemory::create("Dummy ORAM", position_map_size, position_map_page_size);

        this->buffer = PageOptimizedRAWOram::create(
            "buffer_oram",
            std::move(position_map), std::move(untrusted_memory), 
            std::move(valid_bit_tree_controller), std::move(valid_bit_tree_memory),
            std::move(crypto_module),
            this->buffer_entry_size, this->num_entries(), a, 2,
            a * 4,
            phony_load_factor
        );
    }

    this->buffer->init();
    this->ll_memory = dynamic_cast<LLPathOramInterface *>(this->memory.get());
    this->ll_buffer = dynamic_cast<LLPathOramInterface *>(this->buffer.get());

    this->in_buffer_mask = 1UL << num_bits(std::max(this->ll_memory->num_paths(), this->ll_buffer->num_paths()) - 1);
    auto memory_path_index_size = divide_round_up(num_bits(this->ll_memory->num_paths() - 1) + 1, 8UL);
    auto buffer_path_index_size = divide_round_up(num_bits(this->ll_buffer->num_paths() - 1) + 1, 8UL);

    if (buffer_path_index_size > memory_path_index_size) {
        throw std::runtime_error(absl::StrFormat("Buffer path index size %lu is greater than memory path index size %lu", buffer_path_index_size, memory_path_index_size));
    }
}

void 
OramBuffer3::download(std::uint64_t entry_id) {
    auto overall_time_start = std::chrono::steady_clock::now();
    this->num_downloads ++;

    std::memcpy(this->entry_id_buffer.data() + this->entry_id_size * (this->num_downloads - 1), &entry_id, this->entry_id_size);

    // generate_new_path
    
    std::uint64_t new_path = absl::Uniform(this->bit_gen, 0UL, this->ll_buffer->num_paths());
    

    // get old path and write new path
    auto oram_start = std::chrono::steady_clock::now(); 
    std::uint64_t old_path = this->ll_memory->read_and_update_position_map(entry_id, new_path | this->in_buffer_mask);
    auto oram_end = std::chrono::steady_clock::now();
    this->oram_time += (oram_end - oram_start);

    bool is_in_buffer = ((old_path & this->in_buffer_mask) != 0);
    old_path &= (~this->in_buffer_mask);

    this->is_entry_duplicate[this->num_downloads - 1] = is_in_buffer;

    auto random_buffer_path = absl::Uniform(this->bit_gen, 0UL, this->ll_buffer->num_paths());
    auto random_memory_path = absl::Uniform(this->bit_gen, 0UL, this->ll_memory->num_paths());

    auto buffer_search_path = old_path;
    auto memory_search_path = old_path;

    conditional_memcpy(!is_in_buffer, &buffer_search_path, &random_buffer_path, sizeof(buffer_search_path));
    conditional_memcpy(is_in_buffer, &memory_search_path, &random_memory_path, sizeof(memory_search_path));

    // seach for the block in memory
    if (is_in_buffer || !this->enable_non_secure_mode) {
        this->buffer_oram_block_buffer.metadata = BlockMetadata(entry_id, buffer_search_path, false); // invalidate
        std::memset(this->buffer_oram_block_buffer.block.data(), 0, this->buffer_entry_size);

        this->ll_buffer->find_and_remove_block_from_path(this->buffer_oram_block_buffer);

        bool block_found_in_buffer = buffer_oram_block_buffer.metadata.is_valid();

        if (!(!is_in_buffer || block_found_in_buffer)) {
            throw std::runtime_error(absl::StrFormat("Failed to find entry %lu!", entry_id));
        }
    }

    this->num_blocks_in_buffer += (is_in_buffer ? 0: 1);

    if (!is_in_buffer || !this->enable_non_secure_mode) {
        // read main oram
        this->main_oram_block_buffer.metadata = BlockMetadata(entry_id, memory_search_path, false);

        auto oram_start = std::chrono::steady_clock::now();
        this->ll_memory->find_and_remove_block_from_path(this->main_oram_block_buffer);
        auto oram_end = std::chrono::steady_clock::now();
        this->oram_time += (oram_end - oram_start);

        bool block_found_in_main_oram = main_oram_block_buffer.metadata.is_valid();

        if (!(is_in_buffer || block_found_in_main_oram)) {
            throw std::runtime_error(absl::StrFormat("Failed to find entry %lu!", entry_id));
        }

        // copy entry from main oram to buffer oram
        conditional_memcpy(!is_in_buffer, buffer_oram_block_buffer.block.data(), main_oram_block_buffer.block.data(), this->_entry_size);
    }

    // std::uint64_t block_value = 0;
    // std::memcpy(&block_value, this->buffer_oram_block_buffer.block.data(), sizeof(std::uint64_t));

    // if (block_value != entry_id) {
    //     throw std::runtime_error("a");
    // }

    // place the block back onto the buffer
    buffer_oram_block_buffer.metadata = BlockMetadata(entry_id, new_path, true);
    this->ll_buffer->place_block_on_path(buffer_oram_block_buffer);

    auto overall_time_end = std::chrono::steady_clock::now();
    this->overall_time += (overall_time_end - overall_time_start);
}

void 
OramBuffer3::aggregate(std::uint64_t entry_id) {
    auto overall_time_start = std::chrono::steady_clock::now();
    // generate_new_path
    std::uint64_t new_path = absl::Uniform(this->bit_gen, 0UL, this->ll_buffer->num_paths());
    // get old path and write new path
    std::uint64_t old_path = this->ll_memory->read_and_update_position_map(entry_id, new_path | this->in_buffer_mask);
    old_path &= (~this->in_buffer_mask);

    // get block from buffer
    this->buffer_oram_block_buffer.metadata = BlockMetadata(entry_id, old_path, false);

    this->ll_buffer->find_and_remove_block_from_path(buffer_oram_block_buffer);

    bool block_found = this->buffer_oram_block_buffer.metadata.is_valid();

    if (!block_found) {
        throw std::runtime_error(absl::StrFormat("Can't find block %lu in buffer for aggregation.", entry_id));
    }


    // do "aggregation"
    uint16_t counter = 0;
    std::memcpy(&counter, this->buffer_oram_block_buffer.block.data() + 2 * this->_entry_size, sizeof(std::uint16_t));
    counter += 1;
    std::memcpy(this->buffer_oram_block_buffer.block.data() + 2 * this->_entry_size, &counter, sizeof(std::uint16_t));

    this->buffer_oram_block_buffer.block[this->_entry_size] += 1;

    // place block back into the buffer
    this->buffer_oram_block_buffer.metadata.set_path(new_path);
    this->ll_buffer->place_block_on_path(this->buffer_oram_block_buffer);

    auto overall_time_end = std::chrono::steady_clock::now();
    this->overall_time += (overall_time_end - overall_time_start);
}

void 
OramBuffer3::update_flush_buffer() {
    auto overall_time_start = std::chrono::steady_clock::now();
    for (std::uint64_t i = 0; i < this->num_downloads; i++) {
        // get entry id
        std::uint64_t entry_id = 0;
        std::memcpy(&entry_id, this->entry_id_buffer.data() + i * this->entry_id_size, this->entry_id_size);

        bool is_duplicate = this->is_entry_duplicate[i];

        if (!is_duplicate || !this->enable_non_secure_mode) {

            // generate_new_path
            std::uint64_t new_path = absl::Uniform(this->bit_gen, 0UL, this->ll_memory->num_paths());
            std::uint64_t random_path = absl::Uniform(this->bit_gen, 0UL, this->ll_buffer->num_paths());

            // do a fake update if is duplicate
            // auto position_map_start:
            std::uint64_t old_path = this->ll_memory->read_and_update_position_map(entry_id, new_path, is_duplicate);
            old_path &= (~this->in_buffer_mask);

            // use the random path if duplicate
            conditional_memcpy(is_duplicate, &old_path, &random_path, sizeof(std::uint64_t));

            this->buffer_oram_block_buffer.metadata = BlockMetadata(entry_id, old_path, false);

            this->ll_buffer->find_and_remove_block_from_path(buffer_oram_block_buffer);

            bool block_found = this->buffer_oram_block_buffer.metadata.is_valid();

            if (block_found == is_duplicate) {
                if (block_found) {
                    throw std::runtime_error(absl::StrFormat("Duplicate block some how found in buffer, id %lu", entry_id));
                } else {
                    throw std::runtime_error(absl::StrFormat("Can't find block in buffer, id %lu", entry_id));
                }
            }

            uint16_t counter = 0;
            std::memcpy(&counter, this->buffer_oram_block_buffer.block.data() + 2 * this->_entry_size, sizeof(std::uint16_t));

            bool counter_valid = (this->buffer_oram_block_buffer.block[this->_entry_size] == (counter & 0xFF));

            // std::uint64_t block_value = 0;
            // std::memcpy(&block_value, this->buffer_oram_block_buffer.block.data(), sizeof(std::uint64_t));

            // counter_valid = counter_valid && (block_value == entry_id);

            if (!is_duplicate && !counter_valid) {
                throw std::runtime_error("validation failed");
            }

            // setup main oram access
            this->main_oram_block_buffer.metadata = BlockMetadata(entry_id, new_path, !is_duplicate);
            std::memcpy(this->main_oram_block_buffer.block.data(), this->buffer_oram_block_buffer.block.data(), this->_entry_size);

            auto oram_start = std::chrono::steady_clock::now();
            this->ll_memory->place_block_on_path(this->main_oram_block_buffer);
            auto oram_end = std::chrono::steady_clock::now();
            this->oram_time += (oram_end - oram_start);
        }
    }

            

    this->num_blocks_in_buffer = 0;
    this->num_downloads = 0;

    auto overall_time_end = std::chrono::steady_clock::now();
    this->overall_time += (overall_time_end - overall_time_start);
}


OramBufferDP::OramBufferDP(
    unique_memory_t &&memory,
    std::uint64_t max_size,
    std::uint64_t chunk_size,
    float eps,
    BufferORAMType buffer_oram_type
) :
max_size(max_size),
_entry_size(memory->page_size()),
buffer_entry_size(this->_entry_size * 2 + 2),
chunk_size(chunk_size),
eps(eps),
memory(std::move(memory)),
main_oram_block_buffer(_entry_size),
buffer_oram_block_buffer(buffer_entry_size)
{

    double load_factor = 0.75;
    std::unique_ptr<CryptoModule> crypto_module = std::make_unique<AEGIS256Module>();

    if (buffer_oram_type == BufferORAMType::PathORAM) {
        
        // double phony_load_factor = static_cast<double>(this->num_entries()) / (static_cast<double>(this->max_size) / load_factor);

        std::cout << "No buffer given\n";
        std::cout << "Generating Buffer ORAM, this may take a while\n";

        std::uint64_t num_slots_needed = static_cast<std::uint64_t>(ceil(static_cast<double>(this->max_size) / load_factor));

        std::cout << absl::StreamFormat("To maintain an max load factor of %lf when holding %lu entries the buffer oram need at least %lu slots\n", load_factor, this->max_size, num_slots_needed);

        auto parameters = BinaryPathOram2::compute_parameters_known_oram_size(this->buffer_entry_size, buffer_entry_size * 5, 1, this->num_entries(), crypto_module.get(), num_slots_needed, false);

        uint64_t untrusted_memory_size = ((1UL << parameters.levels) - 1) * parameters.bucket_size;
        auto untrusted_memory = BackedMemory::create("buffer_untrusted_memory", untrusted_memory_size, parameters.bucket_size);

        std::uint64_t position_map_page_size = 64;
        std::uint64_t num_position_map_entires_per_page = position_map_page_size / parameters.path_index_size;
        position_map_page_size = num_position_map_entires_per_page * parameters.path_index_size;
        std::cout << absl::StreamFormat("Each position map page of %lu bytes can hold %lu position map entry of %lu bytes\n", position_map_page_size, num_position_map_entires_per_page, parameters.path_index_size);
        std::uint64_t num_position_map_pages = divide_round_up(parameters.num_blocks, num_position_map_entires_per_page);
        uint64_t position_map_size = num_position_map_pages * position_map_page_size;
        std::cout << absl::StreamFormat("Position map needs %lu pages totaling %lu bytes to hold %lu entries\n", num_position_map_pages, position_map_size, parameters.num_blocks);

        unique_memory_t dummy_position_map = DummyMemory::create("buffer_dummy_position_map", position_map_size, position_map_page_size);

        this->buffer = BinaryPathOram2::create(
            "buffer_oram",
            std::move(dummy_position_map), std::move(untrusted_memory), 
            std::move(crypto_module),
            parameters,
            parameters.levels,
            false
        );
    } else {
        const uint64_t target_z = 8;
        const uint64_t a = 8;
        load_factor = 0.375;
        double phony_load_factor = static_cast<double>(this->num_entries()) / (static_cast<double>(this->max_size) / load_factor);
        auto parameters = PageOptimizedRAWOram::compute_parameters(this->buffer_entry_size * (target_z + 1), this->buffer_entry_size, this->num_entries(), 2, crypto_module.get(), phony_load_factor, false);
        auto valid_bit_tree_parameters = ParentCounterValidBitTreeController::compute_parameters(
            crypto_module.get(), 
            parameters.levels, 
            512, 
            parameters.blocks_per_bucket
        );
        unique_memory_t valid_bit_tree_memory = BackedMemory::create("Valid bit tree memory", valid_bit_tree_parameters.required_memory_size);
        std::unique_ptr<ValidBitTreeController> valid_bit_tree_controller = std::make_unique<ParentCounterValidBitTreeController>(valid_bit_tree_parameters, crypto_module.get(), valid_bit_tree_memory.get());
        // uint64_t untrusted_memory_size = (1UL << levels) * blocks_per_bucket * (block_size + sizeof(BlockMetadata));
        std::uint64_t position_map_page_size = 64;
        std::uint64_t num_position_map_entires_per_page = position_map_page_size / parameters.path_index_size;
        position_map_page_size = num_position_map_entires_per_page * parameters.path_index_size;
        std::cout << absl::StreamFormat("Each position map page of %lu bytes can hold %lu position map entry of %lu bytes\n", position_map_page_size, num_position_map_entires_per_page, parameters.path_index_size);
        std::uint64_t num_position_map_pages = divide_round_up(this->max_size, num_position_map_entires_per_page);
        uint64_t position_map_size = num_position_map_pages * position_map_page_size;
        std::cout << absl::StreamFormat("Position map needs %lu pages totaling %lu bytes to hold %lu entries\n", num_position_map_pages, position_map_size, this->max_size);
        // uint64_t bitfield_size = divide_round_up((1UL << levels) * blocks_per_bucket, 64UL * 8UL) * 64UL;

        // std::shared_ptr<std::ostream> untrusted_memory_log = std::shared_ptr<std::ostream>(new std::ofstream(absl::StrFormat("level-%lu_untrusted_memory.log\n", recursive_level)));

        unique_memory_t untrusted_memory = BackedMemory::create("buffer_untrusted_memory", parameters.untrusted_memory_size, parameters.untrusted_memory_page_size);

        unique_memory_t position_map = DummyMemory::create("Dummy ORAM", position_map_size, position_map_page_size);

        this->buffer = PageOptimizedRAWOram::create(
            "buffer_oram",
            std::move(position_map), std::move(untrusted_memory), 
            std::move(valid_bit_tree_controller), std::move(valid_bit_tree_memory),
            std::move(crypto_module),
            this->buffer_entry_size, this->num_entries(), a, 2,
            a * 4,
            phony_load_factor
        );
    }

    this->buffer->init();
    this->ll_memory = dynamic_cast<LLPathOramInterface *>(this->memory.get());
    this->ll_buffer = dynamic_cast<LLPathOramInterface *>(this->buffer.get());

    this->in_buffer_mask = 1UL << num_bits(std::max(this->ll_memory->num_paths(), this->ll_buffer->num_paths()) - 1);
    auto memory_path_index_size = divide_round_up(num_bits(this->ll_memory->num_paths() - 1) + 1, 8UL);
    auto buffer_path_index_size = divide_round_up(num_bits(this->ll_buffer->num_paths() - 1) + 1, 8UL);

    if (buffer_path_index_size > memory_path_index_size) {
        throw std::runtime_error(absl::StrFormat("Buffer path index size %lu is greater than memory path index size %lu", buffer_path_index_size, memory_path_index_size));
    }
}

void OramBufferDP::reserve(std::uint64_t entry_id) {
    this->request_id_buffer.emplace_back(entry_id);
}

void OramBufferDP::load_entries() {
    auto overall_time_start = std::chrono::steady_clock::now();
    size_t total_clients = this->request_id_buffer.size();

    total_requests += total_clients;

    std::cout << absl::StreamFormat("Total clients: %lu\n", total_clients);

    size_t num_chunks = divide_round_up(total_clients, this->chunk_size);

    std::cout << absl::StreamFormat("Clients divided into %lu chunks of size %lu\n", num_chunks, chunk_size);

    this->union_buffer.resize(total_clients);
    std::fill(this->union_buffer.begin(), this->union_buffer.end(), std::numeric_limits<std::uint64_t>::max());
    this->k_union_array.resize(num_chunks);

    for (size_t chunk_index = 0; chunk_index < num_chunks; chunk_index++) {
        size_t chunk_start_offset = chunk_index * chunk_size;
        size_t this_chunk_size = (chunk_start_offset + chunk_size > total_clients) ? total_clients - chunk_start_offset : chunk_size;
        
        std::cout << absl::StreamFormat("Processing chunk %lu of %lu: %lu requests\n", chunk_index + 1, num_chunks, this_chunk_size);

        uint64_t k_union = union_linear_scanning(this->request_id_buffer.data() + chunk_start_offset, this->union_buffer.data() + chunk_start_offset, this_chunk_size);

        std::cout << absl::StreamFormat("k_union: %lu\n", k_union);
        this->k_union_sum += k_union;

        uint64_t k = compute_and_sample_exp_dp<float>(this_chunk_size, k_union, this->eps, this->bit_gen);
        this->k_sum += k;

        if (k < k_union) {
            this->num_dropped_entries += (k_union - k);
        }

        std::cout << absl::StreamFormat("k: %lu\n", k);

        this->k_union_array[chunk_index] = k;
    }

    // start loading phase
    std::cout << "Moving entries from main ORAM to buffer ORAM\n";
    for (size_t chunk_id = 0; chunk_id < this->k_union_array.size(); chunk_id++) {
        size_t chunk_start_offset = chunk_id * this->chunk_size;
        // size_t this_chunk_size = (chunk_start_offset + chunk_size > total_clients) ? total_clients - chunk_start_offset : chunk_size;
        std::cout << absl::StreamFormat("Moving entries for chunk %lu\n", chunk_id + 1);
        for (size_t i = 0; i < this->k_union_array[chunk_id]; i++) {
            this->move_entry_to_buffer_oram(this->union_buffer[chunk_start_offset + i]);
        }
    }

    auto overall_time_end = std::chrono::steady_clock::now();
    this->overall_time += (overall_time_end - overall_time_start);
}

void 
OramBufferDP::move_entry_to_buffer_oram(std::uint64_t entry_id) {
    // generate_new_path
    
    std::uint64_t new_path = absl::Uniform(this->bit_gen, 0UL, this->ll_buffer->num_paths());
    
    bool is_dummy = (entry_id == std::numeric_limits<std::uint64_t>::max());

    // if (is_dummy) {
    //     std::cout << "Moving Dummy\n";
    // }

    // get old path and write new path
    auto oram_start = std::chrono::steady_clock::now(); 
    std::uint64_t old_path = this->ll_memory->read_and_update_position_map(entry_id, new_path | this->in_buffer_mask, is_dummy);
    auto oram_end = std::chrono::steady_clock::now();
    this->oram_time += (oram_end - oram_start);

    bool is_in_buffer = ((old_path & this->in_buffer_mask) != 0);
    old_path &= (~this->in_buffer_mask);

    auto random_buffer_path = absl::Uniform(this->bit_gen, 0UL, this->ll_buffer->num_paths());
    auto random_memory_path = absl::Uniform(this->bit_gen, 0UL, this->ll_memory->num_paths());

    auto buffer_search_path = old_path;
    auto memory_search_path = old_path;

    conditional_memcpy(!is_in_buffer || is_dummy, &buffer_search_path, &random_buffer_path, sizeof(buffer_search_path));
    conditional_memcpy(is_in_buffer || is_dummy, &memory_search_path, &random_memory_path, sizeof(memory_search_path));

    // seach for the block in memory
    this->buffer_oram_block_buffer.metadata = BlockMetadata(entry_id, buffer_search_path, false); // invalidate
    std::memset(this->buffer_oram_block_buffer.block.data(), 0, this->buffer_entry_size);

    this->ll_buffer->find_and_remove_block_from_path(this->buffer_oram_block_buffer);

    bool block_found_in_buffer = buffer_oram_block_buffer.metadata.is_valid();

    if ((!(!is_in_buffer || block_found_in_buffer)) && !is_dummy) {
        throw std::runtime_error(absl::StrFormat("Failed to find entry %lu!", entry_id));
    }

    // this->num_blocks_in_buffer += (is_in_buffer ? 0: 1);

    // read main oram
    this->main_oram_block_buffer.metadata = BlockMetadata(entry_id, memory_search_path, false);

    oram_start = std::chrono::steady_clock::now();
    this->ll_memory->find_and_remove_block_from_path(this->main_oram_block_buffer);
    oram_end = std::chrono::steady_clock::now();
    this->oram_time += (oram_end - oram_start);

    bool block_found_in_main_oram = main_oram_block_buffer.metadata.is_valid();

    if ((!(is_in_buffer || block_found_in_main_oram)) && !is_dummy) {
        throw std::runtime_error(absl::StrFormat("Failed to find entry %lu!", entry_id));
    }

    // copy entry from main oram to buffer oram
    conditional_memcpy(!is_in_buffer, buffer_oram_block_buffer.block.data(), main_oram_block_buffer.block.data(), this->_entry_size);

    // place the block back onto the buffer
    buffer_oram_block_buffer.metadata = BlockMetadata(entry_id, new_path, !is_dummy);
    this->ll_buffer->place_block_on_path(buffer_oram_block_buffer);
}

void
OramBufferDP::download(std::uint64_t entry_id) {
    // generate_new_path
    std::uint64_t new_path = absl::Uniform(this->bit_gen, 0UL, this->ll_buffer->num_paths());
    // get old path and write new path
    std::uint64_t old_path = this->ll_memory->read_and_update_position_map_function(entry_id, [=, this] (std::uint64_t old_path) {
        bool is_present = old_path & this->in_buffer_mask;
        uint64_t new_path_marked = new_path | this->in_buffer_mask;
        uint64_t path = old_path;
        conditional_memcpy(is_present, &path, &new_path_marked, sizeof(uint64_t));
        return path;
    });

    bool present = old_path & this->in_buffer_mask;
    old_path &= (~this->in_buffer_mask);

    auto random_buffer_path = absl::Uniform(this->bit_gen, 0UL, this->ll_buffer->num_paths());
    conditional_memcpy(!present, &old_path, &random_buffer_path, sizeof(uint64_t));

    // get block from buffer
    this->buffer_oram_block_buffer.metadata = BlockMetadata(entry_id, old_path, false);

    this->ll_buffer->find_and_remove_block_from_path(buffer_oram_block_buffer);

    bool block_found = this->buffer_oram_block_buffer.metadata.is_valid();

    if (!block_found) {
        this->num_dropped_requests++;
        // throw std::runtime_error(absl::StrFormat("Can't find block %lu in buffer for aggregation.", entry_id));
    }

    // place block back into the buffer
    this->buffer_oram_block_buffer.metadata.set_path(new_path);
    this->ll_buffer->place_block_on_path(this->buffer_oram_block_buffer);
}

void 
OramBufferDP::aggregate(std::uint64_t entry_id) {
    auto overall_time_start = std::chrono::steady_clock::now();
    // generate_new_path
    std::uint64_t new_path = absl::Uniform(this->bit_gen, 0UL, this->ll_buffer->num_paths());
    // get old path and write new path
    std::uint64_t old_path = this->ll_memory->read_and_update_position_map_function(entry_id, [=, this] (std::uint64_t old_path) {
        bool is_present = old_path & this->in_buffer_mask;
        uint64_t new_path_marked = new_path | this->in_buffer_mask;
        uint64_t path = old_path;
        conditional_memcpy(is_present, &path, &new_path_marked, sizeof(uint64_t));
        return path;
    });
    bool present = old_path & this->in_buffer_mask;
    old_path &= (~this->in_buffer_mask);

    auto random_buffer_path = absl::Uniform(this->bit_gen, 0UL, this->ll_buffer->num_paths());
    conditional_memcpy(!present, &old_path, &random_buffer_path, sizeof(uint64_t));

    // get block from buffer
    this->buffer_oram_block_buffer.metadata = BlockMetadata(entry_id, old_path, false);

    this->ll_buffer->find_and_remove_block_from_path(buffer_oram_block_buffer);

    // bool block_found = this->buffer_oram_block_buffer.metadata.is_valid();

    // if (!block_found) {
    //     throw std::runtime_error(absl::StrFormat("Can't find block %lu in buffer for aggregation.", entry_id));
    // }


    // do "aggregation"
    uint16_t counter = 0;
    std::memcpy(&counter, this->buffer_oram_block_buffer.block.data() + 2 * this->_entry_size, sizeof(std::uint16_t));
    counter += 1;
    std::memcpy(this->buffer_oram_block_buffer.block.data() + 2 * this->_entry_size, &counter, sizeof(std::uint16_t));

    this->buffer_oram_block_buffer.block[this->_entry_size] += 1;

    // place block back into the buffer
    this->buffer_oram_block_buffer.metadata.set_path(new_path);
    this->ll_buffer->place_block_on_path(this->buffer_oram_block_buffer);

    auto overall_time_end = std::chrono::steady_clock::now();
    this->overall_time += (overall_time_end - overall_time_start);
}

void 
OramBufferDP::move_entry_to_main_oram(std::uint64_t entry_id) {
    bool is_dummy = (entry_id == std::numeric_limits<std::uint64_t>::max());

    // std::uint64_t zero = 0;
    // conditional_memcpy(is_dummy, &entry_id, &zero, sizeof(std::uint64_t));

    // access positionmap
    std::uint64_t new_path = absl::Uniform(this->bit_gen, 0UL, this->ll_memory->num_paths());

    std::uint64_t old_path = this->ll_memory->read_and_update_position_map_function(entry_id, [=, this] (std::uint64_t old_path) {
        bool present_in_buffer = (old_path & this->in_buffer_mask);
        std::uint64_t output_path = old_path;
        conditional_memcpy(present_in_buffer, &output_path, &new_path, sizeof(std::uint64_t));
        return output_path;
    }, is_dummy);

    is_dummy = is_dummy || !(old_path & this->in_buffer_mask);
    old_path &= (~this->in_buffer_mask);

    // use a random buffer path if is duplicate
    std::uint64_t random_buffer_path = absl::Uniform(this->bit_gen, 0UL, this->ll_buffer->num_paths());
    conditional_memcpy(is_dummy, &old_path, &random_buffer_path, sizeof(std::uint64_t));

    // std::cout << old_path << "\n";

    this->buffer_oram_block_buffer.metadata = BlockMetadata(entry_id, old_path, false);

    // std::cout << this->buffer_oram_block_buffer.metadata.get_path() << "," << this->buffer_oram_block_buffer.metadata.is_valid() << "\n";

    this->ll_buffer->find_and_remove_block_from_path(buffer_oram_block_buffer);

    // std::cout << this->buffer_oram_block_buffer.metadata.get_path() << "," << this->buffer_oram_block_buffer.metadata.is_valid() << "\n";

    bool block_found = this->buffer_oram_block_buffer.metadata.is_valid();

    // std::cout << block_found << "\n";

    if (block_found == is_dummy) {
        if (block_found) {
            throw std::runtime_error(absl::StrFormat("Duplicate block some how found in buffer, id %lu", entry_id));
        } else {
            throw std::runtime_error(absl::StrFormat("Can't find block in buffer, id %lu", entry_id));
        }
    }

    uint16_t counter = 0;
    std::memcpy(&counter, this->buffer_oram_block_buffer.block.data() + 2 * this->_entry_size, sizeof(std::uint16_t));

    bool counter_valid = (this->buffer_oram_block_buffer.block[this->_entry_size] == (counter & 0xFF));

    if (!is_dummy && !counter_valid) {
        throw std::runtime_error("validation failed");
    }

    // setup main oram access
    this->main_oram_block_buffer.metadata = BlockMetadata(entry_id, new_path, !is_dummy);
    std::memcpy(this->main_oram_block_buffer.block.data(), this->buffer_oram_block_buffer.block.data(), this->_entry_size);

    auto oram_start = std::chrono::steady_clock::now();
    this->ll_memory->place_block_on_path(this->main_oram_block_buffer);
    auto oram_end = std::chrono::steady_clock::now();
    this->oram_time += (oram_end - oram_start);
}

void 
OramBufferDP::update_flush_buffer() {
    auto overall_time_start = std::chrono::steady_clock::now();

    std::cout << "Moving entries from buffer ORAM to main ORAM\n";
    for (size_t chunk_id = 0; chunk_id < this->k_union_array.size(); chunk_id++) {
        size_t chunk_start_offset = chunk_id * this->chunk_size;
        std::cout << absl::StreamFormat("Moving entries for chunk %lu\n", chunk_id + 1);
        for (size_t i = 0; i < this->k_union_array[chunk_id]; i++) {
            this->move_entry_to_main_oram(this->union_buffer[chunk_start_offset + i]);
        }
    }

    // this->num_blocks_in_buffer = 0;
    // this->num_downloads = 0;

    this->request_id_buffer.clear();

    auto overall_time_end = std::chrono::steady_clock::now();
    this->overall_time += (overall_time_end - overall_time_start);
}

OramBufferDPLinearScan::OramBufferDPLinearScan(
    unique_memory_t &&memory,
    std::uint64_t max_size,
    std::uint64_t chunk_size,
    float eps,
    BufferORAMType buffer_oram_type
) :
max_size(max_size),
_entry_size(memory->page_size()),
buffer_entry_size(this->_entry_size * 2 + 2),
chunk_size(chunk_size),
eps(eps),
memory(std::move(memory)),
main_oram_block_buffer(_entry_size),
buffer_oram_block_buffer(buffer_entry_size),
buffer_posmap(max_size),
memory_path_buffer(max_size),
buffer_posmap_scanning_limit(0),
num_entries_in_buffer_oram(0)
{

    double load_factor = 0.75;
    std::unique_ptr<CryptoModule> crypto_module = std::make_unique<AEGIS256Module>();

    if (buffer_oram_type == BufferORAMType::PathORAM) {
        
        // double phony_load_factor = static_cast<double>(this->num_entries()) / (static_cast<double>(this->max_size) / load_factor);

        std::cout << "No buffer given\n";
        std::cout << "Generating Buffer ORAM, this may take a while\n";

        std::uint64_t num_slots_needed = static_cast<std::uint64_t>(ceil(static_cast<double>(this->max_size) / load_factor));

        std::cout << absl::StreamFormat("To maintain an max load factor of %lf when holding %lu entries the buffer oram need at least %lu slots\n", load_factor, this->max_size, num_slots_needed);

        auto parameters = BinaryPathOram2::compute_parameters_known_oram_size(this->buffer_entry_size, buffer_entry_size * 5, 1, this->num_entries(), crypto_module.get(), num_slots_needed, false);

        uint64_t untrusted_memory_size = ((1UL << parameters.levels) - 1) * parameters.bucket_size;
        auto untrusted_memory = BackedMemory::create("buffer_untrusted_memory", untrusted_memory_size, parameters.bucket_size);

        std::uint64_t position_map_page_size = 64;
        std::uint64_t num_position_map_entires_per_page = position_map_page_size / parameters.path_index_size;
        position_map_page_size = num_position_map_entires_per_page * parameters.path_index_size;
        std::cout << absl::StreamFormat("Each position map page of %lu bytes can hold %lu position map entry of %lu bytes\n", position_map_page_size, num_position_map_entires_per_page, parameters.path_index_size);
        std::uint64_t num_position_map_pages = divide_round_up(parameters.num_blocks, num_position_map_entires_per_page);
        uint64_t position_map_size = num_position_map_pages * position_map_page_size;
        std::cout << absl::StreamFormat("Position map needs %lu pages totaling %lu bytes to hold %lu entries\n", num_position_map_pages, position_map_size, parameters.num_blocks);

        unique_memory_t dummy_position_map = DummyMemory::create("buffer_dummy_position_map", position_map_size, position_map_page_size);

        this->buffer = BinaryPathOram2::create(
            "buffer_oram",
            std::move(dummy_position_map), std::move(untrusted_memory), 
            std::move(crypto_module),
            parameters,
            parameters.levels,
            false
        );
    } else {
        const uint64_t target_z = 8;
        const uint64_t a = 8;
        load_factor = 0.375;
        double phony_load_factor = static_cast<double>(this->num_entries()) / (static_cast<double>(this->max_size) / load_factor);
        auto parameters = PageOptimizedRAWOram::compute_parameters(this->buffer_entry_size * (target_z + 1), this->buffer_entry_size, this->num_entries(), 2, crypto_module.get(), phony_load_factor, false);
        auto valid_bit_tree_parameters = ParentCounterValidBitTreeController::compute_parameters(
            crypto_module.get(), 
            parameters.levels, 
            512, 
            parameters.blocks_per_bucket
        );
        unique_memory_t valid_bit_tree_memory = BackedMemory::create("Valid bit tree memory", valid_bit_tree_parameters.required_memory_size);
        std::unique_ptr<ValidBitTreeController> valid_bit_tree_controller = std::make_unique<ParentCounterValidBitTreeController>(valid_bit_tree_parameters, crypto_module.get(), valid_bit_tree_memory.get());
        // uint64_t untrusted_memory_size = (1UL << levels) * blocks_per_bucket * (block_size + sizeof(BlockMetadata));
        std::uint64_t position_map_page_size = 64;
        std::uint64_t num_position_map_entires_per_page = position_map_page_size / parameters.path_index_size;
        position_map_page_size = num_position_map_entires_per_page * parameters.path_index_size;
        std::cout << absl::StreamFormat("Each position map page of %lu bytes can hold %lu position map entry of %lu bytes\n", position_map_page_size, num_position_map_entires_per_page, parameters.path_index_size);
        std::uint64_t num_position_map_pages = divide_round_up(this->max_size, num_position_map_entires_per_page);
        uint64_t position_map_size = num_position_map_pages * position_map_page_size;
        std::cout << absl::StreamFormat("Position map needs %lu pages totaling %lu bytes to hold %lu entries\n", num_position_map_pages, position_map_size, this->max_size);
        // uint64_t bitfield_size = divide_round_up((1UL << levels) * blocks_per_bucket, 64UL * 8UL) * 64UL;

        // std::shared_ptr<std::ostream> untrusted_memory_log = std::shared_ptr<std::ostream>(new std::ofstream(absl::StrFormat("level-%lu_untrusted_memory.log\n", recursive_level)));

        unique_memory_t untrusted_memory = BackedMemory::create("buffer_untrusted_memory", parameters.untrusted_memory_size, parameters.untrusted_memory_page_size);

        unique_memory_t position_map = DummyMemory::create("Dummy ORAM", position_map_size, position_map_page_size);

        this->buffer = PageOptimizedRAWOram::create(
            "buffer_oram",
            std::move(position_map), std::move(untrusted_memory), 
            std::move(valid_bit_tree_controller), std::move(valid_bit_tree_memory),
            std::move(crypto_module),
            this->buffer_entry_size, this->num_entries(), a, 2,
            a * 4,
            phony_load_factor
        );
    }

    this->buffer->init();
    this->ll_memory = dynamic_cast<LLPathOramInterface *>(this->memory.get());
    this->ll_buffer = dynamic_cast<LLPathOramInterface *>(this->buffer.get());

    this->in_buffer_mask = 1UL << num_bits(std::max(this->ll_memory->num_paths(), this->ll_buffer->num_paths()) - 1);
    auto memory_path_index_size = divide_round_up(num_bits(this->ll_memory->num_paths() - 1) + 1, 8UL);
    auto buffer_path_index_size = divide_round_up(num_bits(this->ll_buffer->num_paths() - 1) + 1, 8UL);

    if (buffer_path_index_size > memory_path_index_size) {
        throw std::runtime_error(absl::StrFormat("Buffer path index size %lu is greater than memory path index size %lu", buffer_path_index_size, memory_path_index_size));
    }

    // // set all posmap entries to be invalid.
    // for (size_t i = 0; i < this->max_size; i++) {
    //     this->buffer_posmap[i].block_id = std::numeric_limits<uint32_t>::max();
    // }
}

void OramBufferDPLinearScan::reserve(std::uint64_t entry_id) {
    this->request_id_buffer.emplace_back(entry_id);
}

void OramBufferDPLinearScan::load_entries() {
    auto overall_time_start = std::chrono::steady_clock::now();
    size_t total_clients = this->request_id_buffer.size();
    this->union_buffer.resize(total_clients);

    total_requests += total_clients;

    std::cout << absl::StreamFormat("Total clients: %lu\n", total_clients);
    std::cout << "Performing Union";

    uint64_t k_union = union_linear_scanning(this->request_id_buffer.data(), this->union_buffer.data(), total_clients);

    std::cout << absl::StreamFormat("k_union: %lu\n", k_union);
    this->k_union_sum += k_union;

    uint64_t k = compute_and_sample_exp_dp<float>(total_clients, k_union, this->eps, this->bit_gen);
    this->k_sum += k;

    if (k < k_union) {
        this->num_dropped_entries += (k_union - k);
    }

    std::cout << absl::StreamFormat("k: %lu\n", k);

    this->buffer_posmap_scanning_limit = k;
    this->num_entries_in_buffer_oram = std::min(k_union, k);

    // start loading phase
    std::cout << "Moving entries from main ORAM to buffer ORAM\n";

    for (size_t i = 0; i < this->buffer_posmap_scanning_limit; i++) {
        bool is_dummy = (i >= this->num_entries_in_buffer_oram);
        uint64_t entry_id = is_dummy ? std::numeric_limits<uint32_t>::max(): this->union_buffer[i];
        std::uint64_t new_memory_path = absl::Uniform(this->bit_gen, 0UL, this->ll_memory->num_paths());
        std::uint64_t new_buffer_path = absl::Uniform(this->bit_gen, 0UL, this->ll_buffer->num_paths());
        this->memory_path_buffer[i] = new_memory_path;

        // get old path and write new path to memory
        auto oram_start = std::chrono::steady_clock::now(); 
        std::uint64_t old_memory_path = this->ll_memory->read_and_update_position_map(entry_id, new_memory_path, is_dummy);
        auto oram_end = std::chrono::steady_clock::now();
        this->oram_time += (oram_end - oram_start);

        // write new buffer path to buffer posmap
        this->buffer_posmap[i].block_id = entry_id;
        this->buffer_posmap[i].path_id = new_buffer_path;

        auto random_memory_path = absl::Uniform(this->bit_gen, 0UL, this->ll_memory->num_paths());

        auto memory_search_path = old_memory_path;

        conditional_memcpy(is_dummy, &memory_search_path, &random_memory_path, sizeof(memory_search_path));

        this->main_oram_block_buffer.metadata = BlockMetadata(entry_id, memory_search_path, false);

        oram_start = std::chrono::steady_clock::now();
        this->ll_memory->find_and_remove_block_from_path(this->main_oram_block_buffer);
        oram_end = std::chrono::steady_clock::now();
        this->oram_time += (oram_end - oram_start);

        bool block_found_in_main_oram = main_oram_block_buffer.metadata.is_valid();

        if ((!(block_found_in_main_oram)) && !is_dummy) {
            throw std::runtime_error(absl::StrFormat("Failed to find entry %lu!", entry_id));
        }

        // copy entry from main oram to buffer oram
        std::memcpy(buffer_oram_block_buffer.block.data(), main_oram_block_buffer.block.data(), this->_entry_size);

        // place the block back onto the buffer
        buffer_oram_block_buffer.metadata = BlockMetadata(entry_id, new_buffer_path, !(is_dummy));
        this->ll_buffer->place_block_on_path(buffer_oram_block_buffer);
    }

    auto overall_time_end = std::chrono::steady_clock::now();
    this->overall_time += (overall_time_end - overall_time_start);
}

// bool 
// OramBufferDPLinearScan::buffer_posmap_insert_if_not_present(uint64_t block_id, uint64_t path_id, bool is_dummy) {
//     this->buffer_posmap_scanning_limit++;
//     bool is_duplicte = is_dummy;

//     PosMapEntry new_entry = {
//         .block_id = static_cast<uint32_t>(block_id),
//         .path_id = static_cast<uint32_t>(path_id)
//     };

//     for (size_t i = 0; i < buffer_posmap_scanning_limit; i++) {
//         is_duplicte |= (this->buffer_posmap[i].block_id == block_id);
//         bool do_insert = (i == this->num_entries_in_buffer_oram);
//         do_insert = do_insert && (!is_duplicte);

//         conditional_memcpy(do_insert, &(buffer_posmap[i]), &new_entry, sizeof(PosMapEntry));
//     }

//     // increment entry count if it has actually been inserted
//     this->num_entries_in_buffer_oram += (is_duplicte ? 0: 1);
//     return !is_duplicte;
// }

// void 
// OramBufferDPLinearScan::move_entry_to_buffer_oram(std::uint64_t entry_id) {
//     // generate_new_path
    
    
// }

uint64_t 
OramBufferDPLinearScan::bufer_posmap_read_and_update(uint64_t block_id, uint64_t new_path_id) {
    bool found = false;
    PosMapEntry new_entry = {
        .block_id = static_cast<uint32_t>(block_id),
        .path_id = static_cast<uint32_t>(new_path_id)
    };
    uint64_t old_path_id = 0;

    for (size_t i = 0; i < this->buffer_posmap_scanning_limit; i++) {
        bool match = this->buffer_posmap[i].block_id == block_id;
        found = found | match;

        conditional_memcpy(match, &old_path_id, &(this->buffer_posmap[i].path_id), sizeof(uint32_t));
        conditional_memcpy(match, &(this->buffer_posmap[i]), &new_entry, sizeof(PosMapEntry));
    }

    old_path_id = (found ? old_path_id : std::numeric_limits<uint64_t>::max());

    return old_path_id;
}

void
OramBufferDPLinearScan::download(std::uint64_t entry_id) {
    // generate_new_path
    std::uint64_t new_path = absl::Uniform(this->bit_gen, 0UL, this->ll_buffer->num_paths());
    // get old path and write new path
    // std::uint64_t old_path = this->ll_memory->read_and_update_position_map_function(entry_id, [=] (std::uint64_t old_path) {
    //     bool is_present = old_path & this->in_buffer_mask;
    //     uint64_t new_path_marked = new_path | this->in_buffer_mask;
    //     uint64_t path = old_path;
    //     conditional_memcpy(is_present, &path, &new_path_marked, sizeof(uint64_t));
    //     return path;
    // });

    std::uint64_t old_path = this->bufer_posmap_read_and_update(entry_id, new_path);

    bool present = (old_path != std::numeric_limits<uint64_t>::max());
    // old_path &= (~this->in_buffer_mask);

    auto random_buffer_path = absl::Uniform(this->bit_gen, 0UL, this->ll_buffer->num_paths());
    conditional_memcpy(!present, &old_path, &random_buffer_path, sizeof(uint64_t));

    // get block from buffer
    this->buffer_oram_block_buffer.metadata = BlockMetadata(entry_id, old_path, false);

    this->ll_buffer->find_and_remove_block_from_path(buffer_oram_block_buffer);

    bool block_found = this->buffer_oram_block_buffer.metadata.is_valid();

    if (!block_found) {
        this->num_dropped_requests++;
        // throw std::runtime_error(absl::StrFormat("Can't find block %lu in buffer for aggregation.", entry_id));
    }

    // place block back into the buffer
    this->buffer_oram_block_buffer.metadata.set_path(new_path);
    this->ll_buffer->place_block_on_path(this->buffer_oram_block_buffer);
}

void 
OramBufferDPLinearScan::aggregate(std::uint64_t entry_id) {
    auto overall_time_start = std::chrono::steady_clock::now();
    // generate_new_path
    std::uint64_t new_path = absl::Uniform(this->bit_gen, 0UL, this->ll_buffer->num_paths());
    // get old path and write new path
    // std::uint64_t old_path = this->ll_memory->read_and_update_position_map_function(entry_id, [=] (std::uint64_t old_path) {
    //     bool is_present = old_path & this->in_buffer_mask;
    //     uint64_t new_path_marked = new_path | this->in_buffer_mask;
    //     uint64_t path = old_path;
    //     conditional_memcpy(is_present, &path, &new_path_marked, sizeof(uint64_t));
    //     return path;
    // });

    std::uint64_t old_path = this->bufer_posmap_read_and_update(entry_id, new_path);
    bool present = (old_path != std::numeric_limits<uint64_t>::max());
    // old_path &= (~this->in_buffer_mask);

    auto random_buffer_path = absl::Uniform(this->bit_gen, 0UL, this->ll_buffer->num_paths());
    conditional_memcpy(!present, &old_path, &random_buffer_path, sizeof(uint64_t));

    // get block from buffer
    this->buffer_oram_block_buffer.metadata = BlockMetadata(entry_id, old_path, false);

    this->ll_buffer->find_and_remove_block_from_path(buffer_oram_block_buffer);

    // bool block_found = this->buffer_oram_block_buffer.metadata.is_valid();

    // if (!block_found) {
    //     throw std::runtime_error(absl::StrFormat("Can't find block %lu in buffer for aggregation.", entry_id));
    // }


    // do "aggregation"
    uint16_t counter = 0;
    std::memcpy(&counter, this->buffer_oram_block_buffer.block.data() + 2 * this->_entry_size, sizeof(std::uint16_t));
    counter += 1;
    std::memcpy(this->buffer_oram_block_buffer.block.data() + 2 * this->_entry_size, &counter, sizeof(std::uint16_t));

    this->buffer_oram_block_buffer.block[this->_entry_size] += 1;

    // place block back into the buffer
    this->buffer_oram_block_buffer.metadata.set_path(new_path);
    this->ll_buffer->place_block_on_path(this->buffer_oram_block_buffer);

    auto overall_time_end = std::chrono::steady_clock::now();
    this->overall_time += (overall_time_end - overall_time_start);
}

// uint64_t 
// OramBuffer::buffer_posmap_pop(uint64_t block_id) {

// }

void 
OramBufferDPLinearScan::move_entry_to_main_oram(std::uint64_t entry_id) {
    bool is_dummy = (entry_id == std::numeric_limits<std::uint64_t>::max());

    // std::uint64_t zero = 0;
    // conditional_memcpy(is_dummy, &entry_id, &zero, sizeof(std::uint64_t));

    // access positionmap
    std::uint64_t new_path = absl::Uniform(this->bit_gen, 0UL, this->ll_memory->num_paths());

    std::uint64_t old_path = this->ll_memory->read_and_update_position_map_function(entry_id, [=, this] (std::uint64_t old_path) {
        bool present_in_buffer = (old_path & this->in_buffer_mask);
        std::uint64_t output_path = old_path;
        conditional_memcpy(present_in_buffer, &output_path, &new_path, sizeof(std::uint64_t));
        return output_path;
    }, is_dummy);

    is_dummy = is_dummy || !(old_path & this->in_buffer_mask);
    old_path &= (~this->in_buffer_mask);

    // use a random buffer path if is duplicate
    std::uint64_t random_buffer_path = absl::Uniform(this->bit_gen, 0UL, this->ll_buffer->num_paths());
    conditional_memcpy(is_dummy, &old_path, &random_buffer_path, sizeof(std::uint64_t));

    // std::cout << old_path << "\n";

    this->buffer_oram_block_buffer.metadata = BlockMetadata(entry_id, old_path, false);

    // std::cout << this->buffer_oram_block_buffer.metadata.get_path() << "," << this->buffer_oram_block_buffer.metadata.is_valid() << "\n";

    this->ll_buffer->find_and_remove_block_from_path(buffer_oram_block_buffer);

    // std::cout << this->buffer_oram_block_buffer.metadata.get_path() << "," << this->buffer_oram_block_buffer.metadata.is_valid() << "\n";

    bool block_found = this->buffer_oram_block_buffer.metadata.is_valid();

    // std::cout << block_found << "\n";

    if (block_found == is_dummy) {
        if (block_found) {
            throw std::runtime_error(absl::StrFormat("Duplicate block some how found in buffer, id %lu", entry_id));
        } else {
            throw std::runtime_error(absl::StrFormat("Can't find block in buffer, id %lu", entry_id));
        }
    }

    uint16_t counter = 0;
    std::memcpy(&counter, this->buffer_oram_block_buffer.block.data() + 2 * this->_entry_size, sizeof(std::uint16_t));

    bool counter_valid = (this->buffer_oram_block_buffer.block[this->_entry_size] == (counter & 0xFF));

    if (!is_dummy && !counter_valid) {
        throw std::runtime_error("validation failed");
    }

    // setup main oram access
    this->main_oram_block_buffer.metadata = BlockMetadata(entry_id, new_path, !is_dummy);
    std::memcpy(this->main_oram_block_buffer.block.data(), this->buffer_oram_block_buffer.block.data(), this->_entry_size);

    auto oram_start = std::chrono::steady_clock::now();
    this->ll_memory->place_block_on_path(this->main_oram_block_buffer);
    auto oram_end = std::chrono::steady_clock::now();
    this->oram_time += (oram_end - oram_start);
}

void 
OramBufferDPLinearScan::update_flush_buffer() {
    auto overall_time_start = std::chrono::steady_clock::now();

    std::cout << "Moving entries from buffer ORAM to main ORAM\n";
    // for (size_t chunk_id = 0; chunk_id < this->k_union_array.size(); chunk_id++) {
    //     size_t chunk_start_offset = chunk_id * this->chunk_size;
    //     std::cout << absl::StreamFormat("Moving entries for chunk %lu\n", chunk_id + 1);
    //     for (size_t i = 0; i < this->k_union_array[chunk_id]; i++) {
    //         this->move_entry_to_main_oram(this->union_buffer[chunk_start_offset + i]);
    //     }
    // }

    for (size_t i = 0; i < this->buffer_posmap_scanning_limit; i++) {
        bool is_dummy = (i >= this->num_entries_in_buffer_oram);
        uint64_t entry_id = is_dummy ? std::numeric_limits<uint32_t>::max(): this->buffer_posmap[i].block_id;

        std::uint64_t buffer_path = this->buffer_posmap[i].path_id;

        // use a random buffer path if is duplicate
        std::uint64_t random_buffer_path = absl::Uniform(this->bit_gen, 0UL, this->ll_buffer->num_paths());
        conditional_memcpy(is_dummy, &buffer_path, &random_buffer_path, sizeof(std::uint64_t));

        // std::cout << old_path << "\n";

        this->buffer_oram_block_buffer.metadata = BlockMetadata(entry_id, buffer_path, false);

        // std::cout << this->buffer_oram_block_buffer.metadata.get_path() << "," << this->buffer_oram_block_buffer.metadata.is_valid() << "\n";

        this->ll_buffer->find_and_remove_block_from_path(buffer_oram_block_buffer);

        // std::cout << this->buffer_oram_block_buffer.metadata.get_path() << "," << this->buffer_oram_block_buffer.metadata.is_valid() << "\n";

        bool block_found = this->buffer_oram_block_buffer.metadata.is_valid();

        // std::cout << block_found << "\n";

        if (block_found == is_dummy) {
            if (block_found) {
                throw std::runtime_error(absl::StrFormat("Duplicate block some how found in buffer, id %lu", entry_id));
            } else {
                throw std::runtime_error(absl::StrFormat("Can't find block in buffer, id %lu", entry_id));
            }
        }

        uint16_t counter = 0;
        std::memcpy(&counter, this->buffer_oram_block_buffer.block.data() + 2 * this->_entry_size, sizeof(std::uint16_t));

        bool counter_valid = (this->buffer_oram_block_buffer.block[this->_entry_size] == (counter & 0xFF));

        if (!is_dummy && !counter_valid) {
            throw std::runtime_error("validation failed");
        }

        // setup main oram access
        this->main_oram_block_buffer.metadata = BlockMetadata(entry_id, this->memory_path_buffer[i], !is_dummy);
        std::memcpy(this->main_oram_block_buffer.block.data(), this->buffer_oram_block_buffer.block.data(), this->_entry_size);

        auto oram_start = std::chrono::steady_clock::now();
        this->ll_memory->place_block_on_path(this->main_oram_block_buffer);
        auto oram_end = std::chrono::steady_clock::now();
        this->oram_time += (oram_end - oram_start);
    }

    // this->num_blocks_in_buffer = 0;
    // this->num_downloads = 0;

    this->request_id_buffer.clear();

    auto overall_time_end = std::chrono::steady_clock::now();
    this->overall_time += (overall_time_end - overall_time_start);
}



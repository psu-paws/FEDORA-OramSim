#pragma once

#include <cstdint>
#include <memory_defs.hpp>
#include <vector>
#include <stash.hpp>
#include <memory_interface.hpp>
#include <conditional_memcpy.hpp>
#include <chrono>
#include <absl/random/random.h>
#include <low_level_path_oram_interface.hpp>

class RecSysBuffer {
    public:
    RecSysBuffer() :
    oram_time(std::chrono::nanoseconds::zero()),
    overall_time(std::chrono::nanoseconds::zero())
    {}
    virtual std::uint64_t num_entries() const = 0;
    virtual std::uint64_t entry_size() const = 0;

    virtual Memory *underlying_memory() = 0;
    
    virtual void reserve(std::uint64_t entry_id) {};
    virtual void load_entries() {};
    virtual void download(std::uint64_t entry_id) = 0;
    virtual void aggregate(std::uint64_t entry_id) = 0;
    virtual void update_flush_buffer() = 0;

    virtual void save_buffer_stats() {};

    virtual std::chrono::nanoseconds get_overall_time() {
        return this->overall_time;
    };
    virtual std::chrono::nanoseconds get_oram_time() {
        return this->oram_time;
    };

    virtual std::size_t get_total_requests() {
        return this->total_requests;
    }

    virtual std::size_t get_k_union_sum() {
        return this->k_union_sum;
    }

    virtual std::size_t get_k_sum() {
        return this->k_sum;
    }


    virtual std::size_t get_num_dropped_entries() {
        return this->num_dropped_entries;
    }


    virtual std::size_t get_num_dropped_requests() {
        return this->num_dropped_requests;
    }


    virtual ~RecSysBuffer() = default;

    protected:
    std::chrono::nanoseconds oram_time;
    std::chrono::nanoseconds overall_time;

    std::size_t total_requests = 0;
    std::size_t k_union_sum = 0;
    std::size_t k_sum = 0;
    std::size_t num_dropped_entries = 0;
    std::size_t num_dropped_requests = 0;
};


class LinearScanBuffer : public RecSysBuffer{
    public:
    LinearScanBuffer(
        unique_memory_t &&memory,
        std::uint64_t max_size,
        bool enable_non_secure_mode
    );

    virtual std::uint64_t num_entries() const;
    virtual std::uint64_t entry_size() const;

    virtual Memory *underlying_memory() override {
        return this->memory.get();
    }
    
    virtual void download(std::uint64_t entry_id);
    virtual void aggregate(std::uint64_t entry_id);
    virtual void update_flush_buffer();

    // virtual std::chrono::nanoseconds get_overall_time();
    // virtual std::chrono::nanoseconds get_oram_time();

    virtual ~LinearScanBuffer() = default;

    protected:

    // inline uint64_t find_block_from_buffer(std::uint64_t entry_id, byte_t *buffer, byte_t *gradient) {
    //     std::uint64_t result = std::numeric_limits<std::uint64_t>::max();
    //     std::uint64_t invalid_entry_id = std::numeric_limits<std::uint64_t>::max();
    //     for (std::uint64_t index = 0; index < this->max_size; index++) {
    //         bool is_target = (this->entry_id_buffer[index] == entry_id);
    //         conditional_memcpy(is_target, buffer, this->data_buffer.data() + this->_entry_size * index, _entry_size);
    //         conditional_memcpy(is_target, &result, &this->entry_id_buffer[index], sizeof(std::uint64_t));
    //     }
    //     return result;
    // }

    struct BufferEntry {
        std::uint64_t entry_id;
        bytes_t data;
        bytes_t gradient;
        std::uint16_t counter;

        BufferEntry(uint64_t entry_size) :
        // buffer_index(std::numeric_limits<std::uint64_t>::max()),
        entry_id(std::numeric_limits<std::uint64_t>::max()),
        data(entry_size),
        gradient(entry_size),
        counter(0)
        {}
    };

    inline bool find_and_remove_block_from_buffer(BufferEntry &entry) {
        bool found = false;
        std::uint64_t invalid_entry_id = std::numeric_limits<std::uint64_t>::max();
        std::uint64_t scan_end = this->enable_non_secure_mode ? this->num_entries_in_buffer : this->num_entries_downloaded;
        for (std::uint64_t index = 0; index < scan_end; index++) {
            bool is_target = (this->entry_id_buffer[index] == entry.entry_id);
            conditional_memcpy(is_target, entry.data.data(), this->data_buffer.data() + this->_entry_size * index, _entry_size);
            conditional_memcpy(is_target, entry.gradient.data(), this->gradient_buffer.data() + this->_entry_size * index, _entry_size);
            conditional_memcpy(is_target, &entry.counter, &(this->counter_buffer[index]), sizeof(std::uint16_t));
            conditional_memcpy(is_target, &entry.entry_id, &(this->entry_id_buffer[index]), sizeof(std::uint64_t));
            conditional_memcpy(is_target, &this->entry_id_buffer[index], &invalid_entry_id, sizeof(std::uint64_t));
            found |= is_target;
        }
        return found;
    }

    inline bool update_gradient(uint64_t entry_id) {
        bytes_t update_result_buffer(this->_entry_size);
        bool found = false;
        std::uint64_t scan_end = this->enable_non_secure_mode ? this->num_entries_in_buffer : this->num_entries_downloaded;
        for (std::uint64_t index = 0; index < scan_end; index++) {
            bool is_target = (this->entry_id_buffer[index] == entry_id);
            std::uint16_t new_count = this->counter_buffer[index] + 1;
            std::memcpy(update_result_buffer.data(), this->gradient_buffer.data() + this->_entry_size * index, this->_entry_size);

            // updating gradient
            update_result_buffer[0] ++;

            conditional_memcpy(is_target, this->gradient_buffer.data() + this->_entry_size * index, update_result_buffer.data(), _entry_size);
            conditional_memcpy(is_target, &this->counter_buffer[index], &new_count, sizeof(std::uint16_t));

            found |= is_target;
        }

        return found;
    }

    inline void place_block_on_buffer(const BufferEntry &entry) {
        bool completed = false;
        std::uint64_t scan_end = this->enable_non_secure_mode ? this->num_entries_in_buffer : this->num_entries_downloaded;
        for (std::uint64_t index = 0; index < scan_end; index++) {
            bool is_free = (this->entry_id_buffer[index] == std::numeric_limits<std::uint64_t>::max());
            bool do_place = is_free && !completed;
            conditional_memcpy(do_place, this->data_buffer.data() + this->_entry_size * index, entry.data.data(), _entry_size);
            conditional_memcpy(do_place, this->gradient_buffer.data() + this->_entry_size * index, entry.gradient.data(), this->_entry_size);
            conditional_memcpy(do_place, &this->counter_buffer[index], &entry.counter, sizeof(std::uint16_t));
            conditional_memcpy(do_place, &this->entry_id_buffer[index], &entry.entry_id, sizeof(std::uint64_t));
            completed |= do_place;
        }

        if (!completed) {
            throw std::runtime_error("Buffer is full!");
        }
    }

    const std::uint64_t max_size;
    const std::uint64_t _entry_size;
    const bool enable_non_secure_mode;
    std::uint64_t num_entries_in_buffer;
    std::uint64_t num_entries_downloaded;
    unique_memory_t memory;
    bytes_t data_buffer;
    bytes_t gradient_buffer;
    std::vector<std::uint16_t> counter_buffer;
    std::vector<std::uint64_t> entry_id_buffer;
};

class NoBuffer : public RecSysBuffer{
    public:
    NoBuffer(
        unique_memory_t &&memory
    ) : 
    _entry_size(memory->page_size()),
    request(MemoryRequestType::READ, 0, this->_entry_size),
    memory(std::move(memory))
    {};

    virtual std::uint64_t num_entries() const {
        return this->memory->size() / this->memory->page_size();
    };
    virtual std::uint64_t entry_size() const {
        return this->memory->page_size();
    }

    virtual Memory *underlying_memory() override {
        return this->memory.get();
    }
    
    virtual void download(std::uint64_t entry_id) {
        auto start = std::chrono::steady_clock::now();
        this->request.address = entry_id * this->_entry_size;
        this->request.type = MemoryRequestType::READ;

        auto oram_start = std::chrono::steady_clock::now();
        this->memory->access(this->request);
        auto oram_end = std::chrono::steady_clock::now();
        this->oram_time += (oram_end - oram_start);

        auto end = std::chrono::steady_clock::now();
        this->overall_time += (end - start);
    }

    virtual void aggregate(std::uint64_t entry_id) {
        auto start = std::chrono::steady_clock::now();
        this->request.address = entry_id * this->_entry_size;
        this->request.type = MemoryRequestType::WRITE;

        auto oram_start = std::chrono::steady_clock::now();
        this->memory->access(this->request);
        auto oram_end = std::chrono::steady_clock::now();
        this->oram_time += (oram_end - oram_start);

        auto end = std::chrono::steady_clock::now();
        this->overall_time += (end - start);
    }

    virtual void update_flush_buffer() {}

    virtual ~NoBuffer() = default;

    protected:

    const std::uint64_t _entry_size;
    MemoryRequest request;
    unique_memory_t memory;
};


class OramBuffer: public RecSysBuffer {
    public:
    enum UpdateMode {
        EMPTY_ORAM,
        POP_N_PUSH
    };

    OramBuffer(
        unique_memory_t &&memory,
        std::uint64_t max_size,
        bool enable_non_secure_mode,
        UpdateMode update_mode = EMPTY_ORAM,
        unique_memory_t &&buffer_oram = nullptr
    );

    virtual std::uint64_t num_entries() const {
        return this->memory->size() / this->memory->page_size();
    }
    virtual std::uint64_t entry_size() const {
        return this->_entry_size;
    }

    virtual Memory *underlying_memory() override {
        return this->memory.get();
    }
    
    virtual void download(std::uint64_t entry_id);
    virtual void aggregate(std::uint64_t entry_id);
    virtual void update_flush_buffer();

    virtual void save_buffer_stats() override {
        this->buffer->save_statistics();
    } 

    // virtual std::chrono::nanoseconds get_overall_time();
    // virtual std::chrono::nanoseconds get_oram_time();

    virtual ~OramBuffer() = default;

    private:
    void flush_block_callback(const BlockMetadata *metadata, const byte_t * data);
    void aggregation_increment(byte_t *entry, const byte_t * _dummy);

    const std::uint64_t max_size;
    const bool enable_non_secure_mode;
    const std::uint64_t _entry_size;
    const std::uint64_t buffer_entry_size;
    const UpdateMode update_mode;
    const std::uint64_t entry_id_size;
    unique_memory_t memory;
    unique_memory_t buffer;
    bytes_t entry_id_buffer;

    std::uint64_t num_downloads;
    std::uint64_t num_blocks_in_buffer;

    MemoryRequest main_oram_request;
    MemoryRequest buffer_request;
};

class OramBuffer3: public RecSysBuffer {
    public:
    enum BufferORAMType {
        PathORAM,
        PageOptimizedRAWORAM
    };

    OramBuffer3(
        unique_memory_t &&memory,
        std::uint64_t max_size,
        bool enable_non_secure_mode,
        BufferORAMType buffer_oram_type = BufferORAMType::PathORAM
    );

    virtual std::uint64_t num_entries() const {
        return this->memory->size() / this->memory->page_size();
    }
    virtual std::uint64_t entry_size() const {
        return this->_entry_size;
    }

    virtual Memory *underlying_memory() override {
        return this->memory.get();
    }
    
    virtual void download(std::uint64_t entry_id);
    virtual void aggregate(std::uint64_t entry_id);
    virtual void update_flush_buffer();

    virtual void save_buffer_stats() override {
        this->buffer->save_statistics();
    } 

    // virtual std::chrono::nanoseconds get_overall_time();
    // virtual std::chrono::nanoseconds get_oram_time();

    virtual ~OramBuffer3() = default;

    private:
    // void aggregation_increment(byte_t *entry, const byte_t * _dummy);

    const std::uint64_t max_size;
    const bool enable_non_secure_mode;
    const std::uint64_t _entry_size;
    const std::uint64_t buffer_entry_size;
    const std::uint64_t entry_id_size;
    std::uint64_t in_buffer_mask;
    unique_memory_t memory;
    unique_memory_t buffer;
    LLPathOramInterface *ll_memory;
    LLPathOramInterface *ll_buffer;
    bytes_t entry_id_buffer;
    std::vector<bool> is_entry_duplicate;

    std::uint64_t num_downloads;
    std::uint64_t num_blocks_in_buffer;

    StashEntry main_oram_block_buffer;
    StashEntry buffer_oram_block_buffer;

    absl::BitGen bit_gen;
};

class OramBufferDP: public RecSysBuffer {
    public:
    enum BufferORAMType {
        PathORAM,
        PageOptimizedRAWORAM
    };

    OramBufferDP(
        unique_memory_t &&memory,
        std::uint64_t max_size,
        std::uint64_t chunk_size,
        float eps,
        BufferORAMType buffer_oram_type = BufferORAMType::PathORAM
    );

    virtual std::uint64_t num_entries() const {
        return this->memory->size() / this->memory->page_size();
    }
    virtual std::uint64_t entry_size() const {
        return this->_entry_size;
    }

    virtual Memory *underlying_memory() override {
        return this->memory.get();
    }
    
    virtual void reserve(std::uint64_t entry_id) override;
    virtual void load_entries() override;
    virtual void download(std::uint64_t entry_id) override;
    virtual void aggregate(std::uint64_t entry_id) override;
    virtual void update_flush_buffer();

    virtual void save_buffer_stats() override {
        this->buffer->save_statistics();
    } 

    // virtual std::chrono::nanoseconds get_overall_time();
    // virtual std::chrono::nanoseconds get_oram_time();

    virtual ~OramBufferDP() = default;

    private:
    // void aggregation_increment(byte_t *entry, const byte_t * _dummy);
    void move_entry_to_buffer_oram(std::uint64_t entry_id);
    void move_entry_to_main_oram(std::uint64_t entry_id);

    const std::uint64_t max_size;
    const std::uint64_t _entry_size;
    const std::uint64_t buffer_entry_size;
    const std::uint64_t chunk_size;
    const float eps;
    std::uint64_t in_buffer_mask;
    unique_memory_t memory;
    unique_memory_t buffer;
    LLPathOramInterface *ll_memory;
    LLPathOramInterface *ll_buffer;
    bytes_t entry_id_buffer;

    std::vector<uint64_t> request_id_buffer;
    std::vector<uint64_t> union_buffer;
    std::vector<size_t> k_union_array;

    StashEntry main_oram_block_buffer;
    StashEntry buffer_oram_block_buffer;

    absl::BitGen bit_gen;
};


class OramBufferDPLinearScan: public RecSysBuffer {
    public:
    enum BufferORAMType {
        PathORAM,
        PageOptimizedRAWORAM
    };

    // a block id of all 1's indicate invalid block
    struct PosMapEntry {
        uint32_t block_id;
        uint32_t path_id;
    };

    OramBufferDPLinearScan(
        unique_memory_t &&memory,
        std::uint64_t max_size,
        std::uint64_t chunk_size,
        float eps,
        BufferORAMType buffer_oram_type = BufferORAMType::PathORAM
    );

    virtual std::uint64_t num_entries() const {
        return this->memory->size() / this->memory->page_size();
    }
    virtual std::uint64_t entry_size() const {
        return this->_entry_size;
    }

    virtual Memory *underlying_memory() override {
        return this->memory.get();
    }
    
    virtual void reserve(std::uint64_t entry_id) override;
    virtual void load_entries() override;
    virtual void download(std::uint64_t entry_id) override;
    virtual void aggregate(std::uint64_t entry_id) override;
    virtual void update_flush_buffer();

    virtual void save_buffer_stats() override {
        this->buffer->save_statistics();
    } 

    // virtual std::chrono::nanoseconds get_overall_time();
    // virtual std::chrono::nanoseconds get_oram_time();

    virtual ~OramBufferDPLinearScan() = default;

    private:
    // void aggregation_increment(byte_t *entry, const byte_t * _dummy);
    void move_entry_to_buffer_oram(std::uint64_t entry_id);
    void move_entry_to_main_oram(std::uint64_t entry_id);

    // bool buffer_posmap_insert_if_not_present(uint64_t block_id, uint64_t path_id, bool is_dummy=false);
    uint64_t bufer_posmap_read_and_update(uint64_t block_id, uint64_t new_path_id);
    // uint64_t buffer_posmap_pop(uint64_t block_id);

    const std::uint64_t max_size;
    const std::uint64_t _entry_size;
    const std::uint64_t buffer_entry_size;
    const std::uint64_t chunk_size;
    const float eps;
    std::uint64_t in_buffer_mask;
    unique_memory_t memory;
    unique_memory_t buffer;
    LLPathOramInterface *ll_memory;
    LLPathOramInterface *ll_buffer;
    bytes_t entry_id_buffer;

    std::vector<uint64_t> request_id_buffer;
    std::vector<uint64_t> union_buffer;
    std::vector<size_t> k_union_array;

    StashEntry main_oram_block_buffer;
    StashEntry buffer_oram_block_buffer;

    std::vector<PosMapEntry> buffer_posmap;
    std::vector<uint32_t> memory_path_buffer;
    size_t buffer_posmap_scanning_limit;
    size_t num_entries_in_buffer_oram;

    absl::BitGen bit_gen;
};
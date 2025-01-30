#pragma once

#include "memory_interface.hpp"

#include <vector>
#include <memory>
#include <stdint.h>
#include <string>
#include <optional>
#include <absl/random/random.h>
#include <absl/strings/str_format.h>
#include <binary_tree_layout.hpp>
#include <oram_defs.hpp>
#include <binary_tree_controller.hpp>
#include <stash.hpp>
#include <chrono>

class BinaryPathOramStatistics : public MemoryStatistics {
    protected:
    int64_t max_stash_size = 0;
    int64_t path_reads = 0;
    int64_t path_writes = 0;
    #ifdef PROFILE_STASH_LOAD
    std::vector<int64_t> stash_load;
    #endif
    #ifdef PROFILE_TREE_LOAD
    std::vector<std::vector<int64_t>> tree_load;
    #endif

    std::chrono::nanoseconds overall_time;
    std::chrono::nanoseconds path_read_time;
    std::chrono::nanoseconds path_write_time;
    std::chrono::nanoseconds valid_bit_tree_time;
    std::chrono::nanoseconds position_map_access_time;
    std::chrono::nanoseconds stash_access_time;
    std::chrono::nanoseconds crypto_time;
    std::chrono::nanoseconds path_scan_time;

    public:
    virtual void clear() override;
    virtual toml::table to_toml() const override;
    virtual void from_toml(const toml::table &table) override;
    void log_stash_size(uint64_t stash_size);
    void log_tree_load(std::vector<int64_t> &&tree_load);
    void increment_path_read();
    void increment_path_write();
    inline void add_overall_time(std::chrono::nanoseconds overall_time) {
        this->overall_time += overall_time;
    }
    inline void add_path_read_time(std::chrono::nanoseconds path_read_time) {
        this->path_read_time += path_read_time;
    }
    inline void add_path_write_time(std::chrono::nanoseconds path_write_time) {
        this->path_write_time += path_write_time;
    }
    inline void add_position_map_access_time(std::chrono::nanoseconds position_map_access_time) {
        this->position_map_access_time += position_map_access_time;
    }
    inline void add_stash_access_time(std::chrono::nanoseconds stash_access_time) {
        this->stash_access_time += stash_access_time;
    }

    inline void add_path_scan_time(std::chrono::nanoseconds path_scan_time) {
        this->path_scan_time += path_scan_time;
    }

    inline void add_crypto_time(std::chrono::nanoseconds crypto_time) {
        this->crypto_time += crypto_time;
    }

    inline void add_valid_bit_tree_time(std::chrono::nanoseconds valid_bit_tree_time) {
        this->valid_bit_tree_time += valid_bit_tree_time;
    }

    virtual ~BinaryPathOramStatistics() = default;

};

class BinaryPathOram: public Memory {
    protected:
    unique_memory_t position_map;
    unique_memory_t untrusted_memory;
    
    const uint64_t block_size;
    const uint64_t block_size_bits;
    const uint64_t blocks_per_bucket;
    const uint64_t levels;
    const uint64_t num_blocks;

    const uint64_t total_data_size;
    const uint64_t total_meta_data_size;
    bool bypass_path_read_on_stash_hit;

    // const unique_tree_layout_t data_address_gen;
    // const unique_tree_layout_t metadata_address_gen;

    Stash stash;
    UntrustedMemoryController untrusted_memory_controller;
    

    BinaryPathOramStatistics *oram_statistics;

    // the stash
    // std::vector<StashEntry> stash;

    // std::vector<MemoryRequest> path_buffer;
    // std::vector<MemoryRequest>

    public:
    static unique_memory_t create(
        std::string_view name,
        unique_memory_t &&position_map, unique_memory_t &&untrusted_memory, 
        uint64_t block_size, uint64_t levels, uint64_t blocks_per_bucket, 
        uint64_t num_blocks,
        bool bypass_path_read_on_stash_hit = false
    );
    static unique_memory_t create(
        std::string_view name,
        unique_memory_t &&position_map, unique_memory_t &&untrusted_memory, 
        uint64_t block_size, uint64_t levels, uint64_t blocks_per_bucket, 
        uint64_t num_blocks,
        unique_tree_layout_t &&data_address_gen,
        unique_tree_layout_t &&metadata_address_gen,
        bool bypass_path_read_on_stash_hit = false
    );
    virtual ~BinaryPathOram() = default;
    protected:
    BinaryPathOram(
        std::string_view type, std::string_view name,
        unique_memory_t &&position_map, unique_memory_t &&untrusted_memory,
        unique_tree_layout_t data_address_gen,
        unique_tree_layout_t metadata_address_gen,
        uint64_t block_size, uint64_t levels, uint64_t blocks_per_bucket, 
        uint64_t num_blocks, bool bypass_path_read_on_stash_hit,
        BinaryPathOramStatistics *statistics
        );
    BinaryPathOram(
        std::string_view type, const toml::table &table, 
        unique_memory_t &&position_map, unique_memory_t &&untrusted_memory,
        BinaryPathOramStatistics *statistics
    );
    public:
    virtual void init() override;
    virtual uint64_t size() const override;
    virtual uint64_t page_size() const override;
    virtual bool isBacked() const override;
    virtual void access(MemoryRequest &request);
    virtual bool is_request_type_supported(MemoryRequestType type) const override;
    virtual void start_logging(bool append = false) override;
    virtual void stop_logging() override;

    virtual toml::table to_toml() const override;
    virtual void save_to_disk(const std::filesystem::path &location) const override;

    static unique_memory_t load_from_disk(const std::filesystem::path &location);
    static unique_memory_t load_from_disk(const std::filesystem::path &location, const toml::table &table);

    virtual void reset_statistics(bool from_file = false) override;
    virtual void save_statistics() override;

    virtual void barrier() override;

    protected:
    // virtual uint64_t read_position_map(uint64_t logical_block_address);
    // virtual void write_position_map(uint64_t logical_block_address, uint64_t new_path);
    virtual uint64_t read_and_update_position_map(uint64_t logical_block_address, uint64_t new_path);
    virtual toml::table to_toml_self() const;

    protected:
    virtual void access_block(MemoryRequestType access_type, uint64_t block_address, unsigned char *buffer, uint64_t offset = 0, uint64_t length = UINT64_MAX);
    // virtual void read_path(uint64_t path);
    // virtual void write_path(uint64_t path);

    protected:
    absl::BitGen bit_gen;
    std::vector<BlockMetadata> eviction_metadata_buffer;
    bytes_t eviction_data_block_buffer;
    // std::vector<MemoryRequest> path_access;

};

// class RAWOram: public BinaryPathOram {
//     public:
//     static unique_memory_t create(
//         std::string_view name,
//         unique_memory_t &&position_map, unique_memory_t &&untrusted_memory, 
//         unique_memory_t &&block_valid_bitfield,
//         uint64_t block_size, uint64_t levels, uint64_t blocks_per_bucket, 
//         uint64_t num_blocks, uint64_t num_accesses_per_eviction
//     );
//     static unique_memory_t create(
//         std::string_view name,
//         unique_memory_t &&position_map, unique_memory_t &&untrusted_memory, 
//         unique_memory_t &&block_valid_bitfield,
//         uint64_t block_size, uint64_t levels, uint64_t blocks_per_bucket, 
//         uint64_t num_blocks, uint64_t num_accesses_per_eviction,
//         std::string_view data_layout_type,
//         std::string_view metadata_layout_type,
//         std::string_view bitfield_layout_type,
//         bool bypass_path_read_on_stash_hit = false
//     );
//     static unique_memory_t create(
//         std::string_view name,
//         unique_memory_t &&position_map, unique_memory_t &&untrusted_memory, 
//         unique_memory_t &&block_valid_bitfield,
//         unique_tree_layout_t &&data_layout,
//         unique_tree_layout_t &&metadata_layout,
//         unique_tree_layout_t &&bitfield_layout,
//         uint64_t block_size, uint64_t levels, uint64_t blocks_per_bucket, 
//         uint64_t num_blocks, uint64_t num_accesses_per_eviction,
//         bool bypass_path_read_on_stash_hit = false
//     );
//     protected:
//     RAWOram(
//         std::string_view type, std::string_view name,
//         unique_memory_t &&position_map, unique_memory_t &&untrusted_memory,
//         unique_memory_t &&block_valid_bitfield_memory,
//         unique_tree_layout_t &&data_address_gen,
//         unique_tree_layout_t &&metadata_address_gen,
//         unique_tree_layout_t &&bitfield_address_gen,
//         uint64_t block_size, uint64_t levels, uint64_t blocks_per_bucket, 
//         uint64_t num_blocks, uint64_t num_accesses_per_eviction,
//         bool bypass_path_read_on_stash_hit,
//         BinaryPathOramStatistics *statistics
//     );
//     RAWOram(
//         std::string_view type, const toml::table &table, 
//         unique_memory_t &&position_map, unique_memory_t &&untrusted_memory,
//         unique_memory_t &&block_valid_bitfield,
//         BinaryPathOramStatistics *statistics
//     );

//     public:
//     virtual void access_block(MemoryRequestType request_type, uint64_t logical_block_address, unsigned char *buffer, uint64_t offset = 0, uint64_t length = UINT64_MAX) override;
//     virtual void read_path(uint64_t path, bool to_stash = false);
//     virtual void eviction_access();
//     virtual void write_path(uint64_t path) override;
//     virtual void start_logging(bool append = false) override;
//     virtual void stop_logging() override;
//     virtual toml::table to_toml() const override;
//     virtual void save_to_disk(const std::filesystem::path &location) const override;
//     static unique_memory_t load_from_disk(const std::filesystem::path &location);
//     static unique_memory_t load_from_disk(const std::filesystem::path &location, const toml::table &table);
//     virtual void reset_statistics(bool from_file = false) override;
//     virtual void save_statistics() override;

//     // virtual uint64_t get_path_buffer_index(uint64_t block_index);
//     protected:
//     virtual toml::table to_toml_self() const override;
//     virtual bool is_block_on_path_valid(uint64_t level, uint64_t block_index) const;
//     protected:
//     const std::function<bool(uint64_t, uint64_t)> is_block_on_path_valid_bond = std::bind(&RAWOram::is_block_on_path_valid, this, std::placeholders::_1, std::placeholders::_2);
//     unique_memory_t block_valid_bitfield;
//     unique_tree_layout_t bitfield_layout;
//     const uint64_t num_accesses_per_eviction;
//     // std::vector<MemoryRequest> path_requests; // the first $levels requests are for metadata, the following $level requests are for the data blocks
//     std::vector<MemoryRequest> bitfield_requests;
//     uint64_t eviction_counter;
//     uint64_t access_counter;

// };
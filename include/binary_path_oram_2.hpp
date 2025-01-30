#pragma once

#include <memory_defs.hpp>
#include <memory_interface.hpp>
#include <stash.hpp>
#include <crypto_module.hpp>
#include <memory>
#include <oram.hpp>
#include <absl/random/random.h>
#include <block_callback.hpp>
#include <low_level_path_oram_interface.hpp>

class BinaryPathOram2: public Memory, public LLPathOramInterface {
    public:
    class MetadataLayout {
        public:
        const std::size_t block_index_size;
        const std::size_t path_index_size;

        public:
        inline MetadataLayout(std::size_t path_index_size, std::size_t block_index_size) :
        block_index_size(block_index_size), path_index_size(path_index_size)
        {}

        [[nodiscard]] inline std::size_t metadata_size() const noexcept {
            return path_index_size + block_index_size + 1;
        }

        [[nodiscard]] inline addr_t get_path_index(const byte_t * metadata) const noexcept{
            addr_t path_index = 0;
            std::memcpy(&path_index, metadata + this->block_index_size, sizeof(path_index));
            path_index &= ((1UL << (this->path_index_size * 8)) - 1);
            return path_index;
        }

         inline void set_path_index(byte_t * metadata, addr_t path_index) const noexcept {
            std::memcpy(metadata + this->block_index_size, &path_index, this->path_index_size);
        }

        [[nodiscard]] inline addr_t get_block_index(const byte_t * metadata) const noexcept{
            addr_t block_index = 0;
            std::memcpy(&block_index, metadata, sizeof(block_index));
            block_index &= ((1UL << (this->block_index_size * 8)) - 1);
            return block_index;
        }

        inline void set_block_index(byte_t * metadata, addr_t block_index) const noexcept {
            std::memcpy(metadata, &block_index, this->block_index_size);
        }

        [[nodiscard]] inline bool get_valid(const byte_t * metadata) const noexcept{
            byte_t flags = 0;
            std::memcpy(&flags, metadata + this->block_index_size + this->path_index_size, 1);
            return (flags & 0x01) != 0;
        }

        inline void set_valid(byte_t * metadata, bool valid) const noexcept {
            byte_t flags = 0;
            std::memcpy(&flags, metadata + this->block_index_size + this->path_index_size, 1);
            if (valid) {
                flags |= 0x01;
            } else {
                flags &= ~(0x01);
            }
            std::memcpy(metadata + this->block_index_size + this->path_index_size, &flags, 1);
        }
    

        [[nodiscard]] BlockMetadata inline to_block_metadata(const byte_t * metadata) const noexcept {
            return BlockMetadata(this->get_block_index(metadata), this->get_path_index(metadata), this->get_valid(metadata));
        }

        void from_block_metadata(byte_t * metadata, const BlockMetadata &source) const noexcept {
            this->set_block_index(metadata, source.get_block_index());
            this->set_path_index(metadata, source.get_path());
            this->set_valid(metadata, source.is_valid());
        }
    };
    public:
    struct Parameters {
        uint64_t block_size;
        uint64_t page_size;
        uint64_t bucket_size;
        uint64_t blocks_per_bucket;
        uint64_t levels_per_page;
        uint64_t levels;
        uint64_t page_levels;
        uint64_t num_blocks;
        uint64_t auth_tag_size;
        uint64_t random_nonce_bytes;
        uint64_t nonce_size;
        uint64_t block_index_size;
        uint64_t path_index_size;
        uint64_t untrusted_memory_size;
    };

    protected:
    unique_memory_t position_map;
    unique_memory_t untrusted_memory;
    std::unique_ptr<CryptoModule> crypto_module;
    
    const Parameters parameters;
    // const uint64_t block_size;
    // const uint64_t bucket_size;
    // const uint64_t blocks_per_bucket;
    // const uint64_t levels;
    // const uint64_t num_blocks;
    // const uint64_t auth_tag_size;

    // const uint64_t total_data_size;
    // const uint64_t total_meta_data_size;
    const MetadataLayout metadata_layout;
    bool bypass_path_read_on_stash_hit;

    Stash stash;
    
    

    public:

    static Parameters compute_parameters(
        uint64_t block_size, uint64_t page_size, uint64_t levels_per_page, 
        uint64_t num_blocks, CryptoModule * crypto_module,
        double max_load_factor = 0.75, bool strict_bucket_size = false
    );

    static Parameters compute_parameters_known_oram_size(
        uint64_t block_size, uint64_t page_size, uint64_t levels_per_page,
        uint64_t num_blocks, CryptoModule * crypto_module,
        uint64_t min_num_slots, bool strict_bucket_size = false
    ); 
    static unique_memory_t create(
        std::string_view name,
        unique_memory_t &&position_map, unique_memory_t &&untrusted_memory,
        std::unique_ptr<CryptoModule> &&crypto_module,
        Parameters Parameters,
        uint64_t max_stash_size,
        bool bypass_path_read_on_stash_hit = false
    );
    // static unique_memory_t create(
    //     std::string_view name,
    //     unique_memory_t &&position_map, unique_memory_t &&untrusted_memory, 
    //     uint64_t block_size, uint64_t levels, uint64_t blocks_per_bucket, 
    //     uint64_t num_blocks,
    //     unique_tree_layout_t &&data_address_gen,
    //     unique_tree_layout_t &&metadata_address_gen,
    //     bool bypass_path_read_on_stash_hit = false
    // );
    virtual ~BinaryPathOram2() = default;
    protected:
    BinaryPathOram2(
        std::string_view name,
        unique_memory_t &&position_map, unique_memory_t &&untrusted_memory,
        std::unique_ptr<CryptoModule> &&crypto_module,
        BinaryPathOramStatistics *statistics,
        Parameters Parameters,
        uint64_t max_stash_size,
        bool bypass_path_read_on_stash_hit = false
    );
    BinaryPathOram2(
        std::string_view type, const toml::table &table, 
        unique_memory_t &&position_map, unique_memory_t &&untrusted_memory,
        BinaryPathOramStatistics *statistics
    );
    public:
    virtual void init() override;
    virtual void fast_init();
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

    virtual void empty_oram(block_call_back callback);

    virtual void barrier() override;

    // LLPathOramInterface
    virtual std::uint64_t read_and_update_position_map(uint64_t logical_block_address, uint64_t new_path, bool is_dummy = false) override;
    virtual void find_and_remove_block_from_path(BlockMetadata *metadata, byte_t * data) override;
    virtual void place_block_on_path(const BlockMetadata *metadata, const byte_t * data) override;
    virtual std::uint64_t num_paths() const noexcept override{
        return 1UL << (this->parameters.levels - 1);
    }

    protected:
    // virtual uint64_t read_position_map(uint64_t logical_block_address);
    // virtual void write_position_map(uint64_t logical_block_address, uint64_t new_path);
    // virtual uint64_t read_and_update_position_map(uint64_t logical_block_address, uint64_t new_path, bool is_dummy);
    virtual toml::table to_toml_self() const;

    inline byte_t *get_bucket(std::uint64_t level) {
        if (this->parameters.levels_per_page == 1) {
            return this->decrypted_path.data() + level * this->parameters.page_size;
        } else {
            std::uint64_t page_level = level / this->parameters.levels_per_page;
            std::uint64_t subtree_level = level % this->parameters.levels_per_page;
            std::uint64_t subtree_offset = (this->currently_loaded_path.value() >> (this->parameters.levels - 1 - page_level * this->parameters.levels_per_page - subtree_level)) % (1UL << subtree_level);
            // std::uint64_t subtree_offset = subtree_path % (1UL << subtree_level);

            return this->decrypted_path.data() + page_level * this->parameters.page_size + ((1UL << subtree_level) - 1 + subtree_offset) * this->parameters.bucket_size;
        }
    }

    inline byte_t *get_metadata(std::uint64_t level, std::uint64_t slot) {
        return this->get_bucket(level) + this->parameters.blocks_per_bucket * this->parameters.block_size + slot * this->metadata_layout.metadata_size();
    }

    inline byte_t *get_data_block(std::uint64_t level, std::uint64_t slot) {
        return this->get_bucket(level) + slot * this->parameters.block_size;
    }

    inline std::uint64_t get_counter(std::uint64_t page_level, std::uint64_t child_index) {
        std::uint64_t counter = 0;
        std::memcpy(
            &counter,
            this->decrypted_path.data() + (page_level + 1) * this->parameters.page_size - this->parameters.auth_tag_size - ((1UL << this->parameters.levels_per_page) - child_index) * sizeof(std::uint64_t),
            sizeof(std::uint64_t)
        );
        return counter;
    }

    inline void set_counter(std::uint64_t page_level, std::uint64_t child_index, std::uint64_t counter) {
        std::memcpy(
            this->decrypted_path.data() + (page_level + 1) * this->parameters.page_size - this->parameters.auth_tag_size - ((1UL << this->parameters.levels_per_page) - child_index) * sizeof(std::uint64_t),
            &counter,
            sizeof(std::uint64_t)
        );
    }

    protected:
    bool access_block(MemoryRequestType access_type, uint64_t block_address, MemoryRequest &request, uint64_t offset = 0, uint64_t length = UINT64_MAX);
    void read_path(uint64_t path);
    void evict_and_write_path();

    bool find_and_remove_block_on_path_buffer(addr_t logical_block_address, BlockMetadata* metadata_buffer, byte_t *block_buffer);
    std::size_t try_evict_block_from_path_buffer(std::size_t max_count, uint64_t ignored_bits, uint64_t path, BlockMetadata *metadatas, byte_t *data_blocks, uint64_t level_limit = std::numeric_limits<uint64_t>::max());

    void empty_oram_recursive(std::uint64_t counter, uint64_t level, uint64_t level_offset, MemoryRequest &request, byte_t * buffer, block_call_back callback);

    protected:
    BinaryPathOramStatistics* oram_statistics;
    absl::BitGen bit_gen;
    std::vector<BlockMetadata> eviction_metadata_buffer;
    bytes_t eviction_data_block_buffer;
    // std::vector<MemoryRequest> path_access;
    std::uint64_t root_counter;
    // std::uint64_t access_counter;
    bytes_t nonce_buffer;
    bytes_t key;

    std::vector<MemoryRequest> path_access;
    bytes_t decrypted_path;

    std::optional<std::uint64_t> currently_loaded_path;
};

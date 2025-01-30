#pragma once
#include <oram_defs.hpp>
#include <memory_interface.hpp>
#include <oram.hpp>
#include <ratio>
#include <fstream>
#include <eviction_path_generator.hpp>
#include <conditional_memcpy.hpp>
#include <valid_bit_tree.hpp>
#include <crypto_module.hpp>
#include <low_level_path_oram_interface.hpp>

class PageOptimizedRAWOram: public Memory, public LLPathOramInterface {

    public:
    static unique_memory_t create(
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
        double max_load_factor = 1.0,
        bool bypass_path_read_on_stash_hit = false,
        bool unsecure_eviction_buffer = false
    );

    struct ComputedParameters {
        addr_t levels;
        addr_t top_level_order;
        addr_t blocks_per_bucket;
        addr_t num_paths;
        addr_t valid_bits_per_bucket;
        addr_t untrusted_memory_size;
        addr_t block_index_size;
        addr_t path_index_size;
        addr_t untrusted_memory_page_size;
    };
    static ComputedParameters compute_parameters(
        addr_t untrusted_memory_page_size,
        addr_t block_size,
        addr_t num_blocks,
        addr_t tree_order,
        CryptoModule *crypto_module,
        double max_load_factor = 0.75,
        bool is_page_size_strict = true
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
    virtual ~PageOptimizedRAWOram() = default;
    protected:
    class MetadataLayout {
        public:
        const std::size_t path_index_size;
        const std::size_t block_index_size;

        public:
        inline MetadataLayout(std::size_t path_index_size, std::size_t block_index_size) :
        path_index_size(path_index_size), block_index_size(block_index_size)
        {}

        [[nodiscard]] inline std::size_t metadata_size() const noexcept {
            return path_index_size + block_index_size;
        }

        [[nodiscard]] inline addr_t get_path_index(const byte_t * metadata) const noexcept{
            addr_t path_index = 0;
            std::memcpy(&path_index, metadata + this->block_index_size, this->path_index_size);
            return path_index;
        }

         inline void set_path_index(byte_t * metadata, addr_t path_index) const noexcept {
            std::memcpy(metadata + this->block_index_size, &path_index, this->path_index_size);
        }

        [[nodiscard]] inline addr_t get_block_index(const byte_t * metadata) const noexcept{
            addr_t block_index = 0;
            std::memcpy(&block_index, metadata, this->block_index_size);
            return block_index;
        }

        inline void set_block_index(byte_t * metadata, addr_t block_index) const noexcept {
            std::memcpy(metadata, &block_index, this->block_index_size);
        }

        [[nodiscard]] BlockMetadata inline to_block_metadata(const byte_t * metadata, bool valid=true) const noexcept {
            return BlockMetadata(this->get_block_index(metadata), this->get_path_index(metadata), valid);
        }

        void from_block_metadata(byte_t * metadata, const BlockMetadata &source) const noexcept {
            this->set_block_index(metadata, source.get_block_index());
            this->set_path_index(metadata, source.get_path());
        }
    };

    PageOptimizedRAWOram(
        std::string_view type, std::string_view name,
        unique_memory_t &&position_map, unique_memory_t &&untrusted_memory,
        std::unique_ptr<ValidBitTreeController> &&valid_bit_tree_controller,
        unique_memory_t &&valid_bit_tree_memory,
        std::unique_ptr<CryptoModule> &&crypto_module,
        const ComputedParameters &computed_parameters,
        uint64_t block_size,
        uint64_t num_blocks,
        uint64_t tree_order,
        uint64_t num_accesses_per_eviction,
        bool bypass_path_read_on_stash_hit,
        bool unsecure_eviction_buffer,
        uint64_t stash_capacity,
        BinaryPathOramStatistics *statistics
    );
    PageOptimizedRAWOram(
        std::string_view type, const toml::table &table, 
        unique_memory_t &&position_map, unique_memory_t &&untrusted_memory,
        unique_memory_t &&valid_bitfield,
        BinaryPathOramStatistics *statistics
    );
    public:
    virtual void init() override;
    virtual void fast_init();
    virtual uint64_t size() const override;
    virtual bool isBacked() const override;
    virtual void access(MemoryRequest &request);
    virtual bool is_request_type_supported(MemoryRequestType type) const override;
    virtual void start_logging(bool append = false) override;
    virtual void stop_logging() override;

    virtual uint64_t page_size() const override;

    virtual toml::table to_toml() const override;
    virtual void save_to_disk(const std::filesystem::path &location) const override;

    static unique_memory_t load_from_disk(const std::filesystem::path &location);
    static unique_memory_t load_from_disk(const std::filesystem::path &location, const toml::table &table);

    virtual void reset_statistics(bool from_file = false) override;
    virtual void save_statistics() override;

    virtual void barrier() override;

    // LLPathOramInterface
    virtual std::uint64_t read_and_update_position_map(uint64_t logical_block_address, uint64_t new_path, bool is_dummy = false) override;
    virtual std::uint64_t read_and_update_position_map_function(uint64_t logical_block_address, positionmap_updater updater, bool is_dummy = false) override;
    virtual void find_and_remove_block_from_path(BlockMetadata *metadata, byte_t * data) override;
    virtual void place_block_on_path(const BlockMetadata *metadata, const byte_t * data) override;
    virtual std::uint64_t num_paths() const noexcept override;


    protected:
    // virtual uint64_t read_position_map(uint64_t logical_block_address);
    // virtual void write_position_map(uint64_t logical_block_address, uint64_t new_path);
    // virtual uint64_t read_and_update_position_map(uint64_t logical_block_address, uint64_t new_path, bool dummy = false);
    virtual toml::table to_toml_self() const;

    protected:
    virtual void access_block(MemoryRequestType access_type, uint64_t block_address, unsigned char *buffer, uint64_t offset = 0, uint64_t length = UINT64_MAX);
    void read_path(uint64_t path);
    void write_path();
    void eviction_access();
    // StashEntry find_block_on_path(addr_t logical_block_address);
    bool find_and_remove_block_on_path_buffer(addr_t logical_block_address, BlockMetadata* metadata_buffer, byte_t *block_buffer);
    std::size_t try_evict_block_from_path_buffer(std::size_t max_count, uint64_t ignored_bits, uint64_t path, BlockMetadata *metadatas, byte_t *data_blocks, uint64_t level_limit = std::numeric_limits<uint64_t>::max());

    inline byte_t *get_metadata(addr_t level, addr_t block_index) {
        return (this->decrypted_path.data() + this->untrusted_memory_page_size * level + block_size * blocks_per_bucket + this->metadata_layout.metadata_size() * block_index);
    }

    inline byte_t *get_metadata(const PathLocation &location) {
        return this->get_metadata(location.level, location.block_index);
    }

    inline byte_t *get_data_block(addr_t level, addr_t block_index) {
        return this->decrypted_path.data() + this->untrusted_memory_page_size * level + block_size * block_index;
    }

    inline byte_t *get_data_block(const PathLocation &location) {
        return this->decrypted_path.data() + this->untrusted_memory_page_size * location.level + block_size * location.block_index;
    }

    inline std::uint64_t get_position_map_address(std::uint64_t logical_block_address) {
        std::uint64_t position_map_page = get_position_map_page(logical_block_address);
        std::uint64_t offset = get_position_map_offset_in_page(logical_block_address);
        return position_map_page * this->position_map_page_size + offset;
    }

    inline std::uint64_t get_position_map_page(std::uint64_t logical_block_address) {
        return logical_block_address / this->num_position_map_entries_per_page;
    }

    inline std::uint64_t get_position_map_offset_in_page(std::uint64_t logical_block_address) {
        return (logical_block_address % this->num_position_map_entries_per_page) * this->metadata_layout.path_index_size;
    }

    protected:
    // crypto stuff
    std::unique_ptr<CryptoModule> crypto_module;
    bytes_t key;

    // memories
    unique_memory_t position_map;
    unique_memory_t untrusted_memory;
    std::unique_ptr<ValidBitTreeController> valid_bit_tree_controller;
    unique_memory_t valid_bit_tree_memory;
    
    const uint64_t block_size;
    const uint64_t block_size_bits;
    const uint64_t num_blocks;
    const uint64_t tree_bits;

    const addr_t levels;
    const addr_t top_level_order;
    const addr_t blocks_per_bucket;
    const addr_t _num_paths;
    const addr_t valid_bits_per_bucket;

    const bool bypass_path_read_on_stash_hit;
    const bool unsecure_eviction_buffer;

    const uint64_t num_accesses_per_eviction;

    const uint64_t random_nonce_bytes;
    const uint64_t auth_tag_bytes;
    const uint64_t untrusted_memory_page_size;
    const uint64_t position_map_page_size;
    const uint64_t num_position_map_entries_per_page;

    const MetadataLayout metadata_layout;

    BinaryPathOramStatistics *oram_statistics;

    EvictionPathGenerator eviction_path_gen;
    uint64_t root_counter;
    uint64_t access_counter;

    // the stash
    Stash stash;
    absl::BitGen bit_gen;
    // path buffers
    std::vector<MemoryRequest> path_access;
    std::optional<addr_t> currently_loaded_path;
    bytes_t decrypted_path;
    bytes_t nonce_buffer;
    // std::vector<MemoryRequest> valid_bitfield_access;
    std::vector<BlockMetadata> eviction_metadata_buffer;
    bytes_t eviction_data_block_buffer;

    LLPathOramInterface *ll_posmap;
    StashEntry posmap_block_buffer;

    #ifdef PROFILE_TREE_LOAD_EXTENDED
    uint64_t extended_tree_load_log_counter;
    void log_extended_tree_load();
    std::ofstream extended_tree_load_out;
    #endif

};
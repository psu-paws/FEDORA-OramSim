#pragma once
#include <memory_defs.hpp>
#include <crypto_module.hpp>
#include <memory_interface.hpp>
#include <conditional_memcpy.hpp>
#include <cstdint>
#include <toml++/toml.h>

class ValidBitTreeController {
public:
    virtual void read_path(byte_t *key, addr_t path) = 0;
    virtual void write_path(byte_t *key) = 0;

    virtual addr_t get_address_of(addr_t level, addr_t bucket_index) const noexcept= 0;
    virtual void encrypt_contents(byte_t *key) = 0;
    virtual void decrypt_contents(byte_t *key) = 0;

    virtual ~ValidBitTreeController() = default;

    virtual toml::table to_toml() const noexcept = 0;

    virtual const byte_t *get_bitfield_for_bucket(addr_t level) const noexcept = 0;
    inline byte_t * get_bitfield_for_bucket(addr_t level) noexcept {
        return const_cast<byte_t *>(const_cast<const ValidBitTreeController*>(this)->get_bitfield_for_bucket(level));
    }

    inline bool is_valid(addr_t level, addr_t slot_index) const noexcept {
        auto byte_offset = slot_index / 8UL;
        auto bit_index = slot_index % 8UL;

        byte_t mask = 1 << bit_index;
        return (this->get_bitfield_for_bucket(level)[byte_offset] & mask) != 0;
    }

    inline void set_valid(addr_t level, addr_t slot_index, bool valid) noexcept {
        auto byte_offset = slot_index / 8UL;
        auto bit_index = slot_index % 8UL;

        if (valid) {
            byte_t mask = 1 << bit_index;
            this->get_bitfield_for_bucket(level)[byte_offset] |= mask;
        } else {
            byte_t mask = ~(1 << bit_index);
            this->get_bitfield_for_bucket(level)[byte_offset] &= mask;
        }
    }

    inline void conditional_set_valid(addr_t level, addr_t slot_index, bool valid, bool condition) noexcept {
        auto byte_offset = slot_index / 8UL;
        auto bit_index = slot_index % 8UL;

        // compute what the byte should be if the modification is made
        byte_t modified_byte = this->get_bitfield_for_bucket(level)[byte_offset];

        if (valid) {
            byte_t mask = 1 << bit_index;
            modified_byte |= mask;
        } else {
            byte_t mask = ~(1 << bit_index);
            modified_byte &= mask;
        }

        // conditionally apply the modification
        conditional_memcpy(condition, this->get_bitfield_for_bucket(level) + byte_offset, &modified_byte, 1);
    }
};

// class InternalCounterValidBitTreeController : public ValidBitTreeController {

//     public:

//     struct Parameters {
//         addr_t levels;
//         addr_t page_levels;
//         addr_t levels_per_page;
//         addr_t root_page_levels;
//         addr_t page_size;
//         addr_t auth_tag_size;
//         addr_t bytes_per_bucket;
//         addr_t required_memory_size;
//         addr_t random_nonce_bytes;
//     };

//     static Parameters compute_parameters(CryptoModule *crypto, addr_t levels, addr_t page_size, addr_t valid_bits_per_bucket);

//     virtual void read_path(byte_t *key, addr_t path) override;
//     virtual void write_path(byte_t *key, addr_t path) override;
//     virtual addr_t get_address_of(addr_t level, addr_t bucket_index) const noexcept override;
    
//     private:

//     const Parameters parameters;

//     std::unique_ptr<byte_t[]> nonce_buffer;

//     std::unique_ptr<byte_t[]> decrypted_buffer;
//     std::vector<MemoryRequest> path_access_requests;

//     std::vector<uint64_t> counters;

// };

class ParentCounterValidBitTreeController : public ValidBitTreeController {


    public:
    struct Parameters {
        addr_t levels;
        addr_t page_levels;
        addr_t levels_per_non_leaf_page;
        addr_t levels_per_leaf_page;
        addr_t page_size;
        addr_t leaf_page_size;
        addr_t auth_tag_size;
        addr_t bytes_per_bucket;
        // addr_t non_leaf_entry_size;
        addr_t required_memory_size;
        addr_t random_nonce_bytes;
    };

    static Parameters compute_parameters(CryptoModule *crypto, addr_t levels, addr_t page_size, addr_t valid_bits_per_bucket);

    ParentCounterValidBitTreeController(Parameters parameters, CryptoModule *crypto_module, Memory* memory);

    ParentCounterValidBitTreeController(const toml::table &table, CryptoModule *crypto_module, Memory* memory);

    public:

    virtual void read_path(byte_t *key, addr_t path) override;
    virtual void write_path(byte_t *key) override;

    virtual void encrypt_contents(byte_t *key) override;
    virtual void decrypt_contents(byte_t *key) override;
    virtual addr_t get_address_of(addr_t level, addr_t bucket_index) const noexcept override;
    virtual toml::table to_toml() const noexcept override;

    virtual ~ParentCounterValidBitTreeController() = default;

    private:
    inline const byte_t* get_decrypted_page(std::uint64_t level) const {
        return this->decrypted_buffer.data() + level * this->parameters.page_size;
    }

    inline byte_t* get_decrypted_page(std::uint64_t level) {
        return const_cast<byte_t*>(const_cast<const ParentCounterValidBitTreeController*>(this)->get_decrypted_page(level));
    }

    virtual const byte_t *get_bitfield_for_bucket(addr_t level) const noexcept {
        uint64_t page_level = level / this->parameters.levels_per_non_leaf_page;
        if (page_level >= this->parameters.page_levels) {
            // deal with the fact the leaf may be larger than non-leaf pages
            page_level = this->parameters.page_levels - 1;
        }
        uint64_t intra_page_level = level - page_level * this->parameters.levels_per_non_leaf_page;
        uint64_t bucket_level_index = this->currently_loaded_path.value() >> (this->parameters.levels - 1 - level);
        uint64_t intra_page_bucket_index = bucket_level_index % (1UL << intra_page_level);
        return get_bucket_from_page(get_decrypted_page(page_level), intra_page_level, intra_page_bucket_index);
    }
    
    inline std::uint64_t get_counter_from_page(const byte_t* page, std::uint64_t child_index) const {
        // compute offset 
        auto offset = ((1UL << this->parameters.levels_per_non_leaf_page) - 1) * this->parameters.bytes_per_bucket;
        offset += sizeof(std::uint64_t) * child_index;
        uint64_t counter;
        std::memcpy(&counter, page + offset, sizeof(std::uint64_t));

        return counter;
    }

    inline const byte_t *get_bucket_from_page(const byte_t* page, std::uint64_t intra_page_level, std::uint64_t intra_page_index) const {
        auto offset = (((1UL << intra_page_level) - 1) + intra_page_index) * this->parameters.bytes_per_bucket;
        return page + offset;
    }

    inline void set_counter_in_page(byte_t* page, std::uint64_t child_index, std::uint64_t counter) const {
        // compute offset 
        auto offset = ((1UL << this->parameters.levels_per_non_leaf_page) - 1) * this->parameters.bytes_per_bucket;
        offset += sizeof(std::uint64_t) * child_index;
        std::memcpy(page + offset, &counter, sizeof(std::uint64_t));
    }

    inline std::uint64_t get_start_address_for_page_level(std::uint64_t page_level) {
        addr_t level_start = 0;
        addr_t level_size = 1;
        // compute addresses
        for (addr_t i = 0; i < page_level; i++) {
            // addr_t level_start = level_start_in_pages * (this->parameters.page_size + this->parameters.auth_tag_size);
            addr_t page_size_in_this_page_level = (i == this->parameters.page_levels - 1 ? this->parameters.leaf_page_size : this->parameters.page_size) + this->parameters.auth_tag_size;
            addr_t levels_per_page_in_this_page_level = (i == this->parameters.page_levels - 1 ? this->parameters.levels_per_leaf_page : this->parameters.levels_per_non_leaf_page);

            level_start += level_size * page_size_in_this_page_level;
            level_size *= (1 << levels_per_page_in_this_page_level);
        }

        return level_start;
    }

    inline std::uint64_t get_page_level_size_in_pages(std::uint64_t page_level) {
        addr_t level_start = 0;
        addr_t level_size = 1;
        // compute addresses
        for (addr_t i = 0; i < page_level; i++) {
            // addr_t level_start = level_start_in_pages * (this->parameters.page_size + this->parameters.auth_tag_size);
            addr_t page_size_in_this_page_level = (i == this->parameters.page_levels - 1 ? this->parameters.leaf_page_size : this->parameters.page_size) + this->parameters.auth_tag_size;
            addr_t levels_per_page_in_this_page_level = (i == this->parameters.page_levels - 1 ? this->parameters.levels_per_leaf_page : this->parameters.levels_per_non_leaf_page);

            level_start += level_size * page_size_in_this_page_level;
            level_size *= (1 << levels_per_page_in_this_page_level);
        }

        return level_size;
    }

    const Parameters parameters;

    bytes_t nonce_buffer;

    bytes_t decrypted_buffer;
    std::vector<MemoryRequest> path_access_requests;
    bool content_encrypted;

    uint64_t root_counter;

    Memory * memory;
    CryptoModule * crypto_module;

    std::optional<addr_t> currently_loaded_path;
};
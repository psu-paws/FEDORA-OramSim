#pragma once

#include <exceptions.hpp>
#include <memory_interface.hpp>
#include <filesystem>
#include <stdint.h>
#include <toml++/toml.h>

// address range checking
bool check_address(uint64_t address, uint64_t valid_range_start, uint64_t valid_range_end) noexcept;
bool check_access_range(const MemoryRequest &request, uint64_t valid_range_start, uint64_t valid_range_end) noexcept;

// heap utils
constexpr uint64_t heap_get_index(uint64_t level, uint64_t index) {
    return (((uint64_t)1UL) << level) - 1 + index;
}

constexpr uint64_t num_bits(uint64_t value) {
    uint64_t bit_count = 0;
    while(value != 0) {
        bit_count ++;
        value >>= 1;
    }
    return bit_count;
}

template <typename T>
T divide_round_up(T dividend, T divisor) {
    return dividend / divisor + (dividend % divisor != 0 ? 1 : 0);
}

inline std::size_t num_bytes(std::size_t value) {
    return divide_round_up(num_bits(value), 8UL);
}

/**
 * @brief Removes everything after the first #, including the #
 *        Returns a copy of the givin string if it does not contain a #
 * 
 * @param line 
 * @return std::string 
 */
std::string remove_comment(const std::string &line);

/**
 * @brief Parses string representation of size.
 * Supports SI prefix such as KB and binary prefix such as KiB.
 * 
 * @param input 
 * @return uint64_t 
 */
uint64_t parse_size(const std::string_view input);

uint64_t parse_size(const toml::node &input);
uint64_t parse_size(const toml::node *node);
uint64_t parse_size(const toml::node_view<const toml::node> &node);
uint64_t parse_size_or(const toml::node_view<const toml::node> &node, uint64_t default_value);

/**
 * @brief Converts the size into a readable string
 * Shortens 
 * 
 * @return string representation of the size
 */
std::string size_to_string(uint64_t size);


char generate_random_character();

constexpr uint64_t reverse_bits(uint64_t value, uint64_t total_bits) {
    if (total_bits == 0) {
        return 0;
    }
    
    value = (value & 0xFFFFFFFF00000000UL) >> 32 | (value & 0x00000000FFFFFFFFUL) << 32; 
    value = (value & 0xFFFF0000FFFF0000UL) >> 16 | (value & 0x0000FFFF0000FFFFUL) << 16; 
    value = (value & 0xFF00FF00FF00FF00UL) >> 8 | (value & 0x00FF00FF00FF00FFUL) << 8; 
    value = (value & 0xF0F0F0F0F0F0F0F0UL) >> 4 | (value & 0x0F0F0F0F0F0F0F0FUL) << 4;  
    value = (value & 0xCCCCCCCCCCCCCCCCUL) >> 2 | (value & 0x3333333333333333UL) << 2;
    value = (value & 0xAAAAAAAAAAAAAAAAUL) >> 1 | (value & 0x5555555555555555UL) << 1;

    return value >> (64 - total_bits);
}

template <typename T>
std::vector<T> vector_from_toml_array(const toml::array *array) {
    std::vector<T> result;
    array->for_each([&result](const toml::value<T> &n) {
        result.emplace_back(n.get());
    });
    return result;
}

// std::vector<int64_t> vector_from_toml_array(const toml::array *array) {
//     std::vector<int64_t> result;
//     array->for_each([&result](const toml::value<int64_t> &n) {
//         result.emplace_back(n.get());
//     });
//     return result;
// }

template <typename T>
toml::array toml_array_from_vector(const std::vector<T> vector) {
    toml::array array;
    for (const auto& element: vector) {
        array.emplace_back(element);
    }

    return array;
}

inline char hex_char_from_nibble(byte_t nibble) {
    nibble = nibble & 0x0F;
    if (nibble < 10) {
        return '0' + nibble;
    } else {
        return 'A' + (nibble - 10);
    }
}

inline byte_t nibble_from_hex_char(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    } else if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    } else if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    
    return 255;
}

inline std::string bytes_to_hex_string(const byte_t* buf, std::size_t length) {
    std::string result;
    for (const byte_t *ptr = buf; ptr < buf + length; ptr++) {
        byte_t byte = *ptr;
        result.push_back(hex_char_from_nibble(byte >> 4));
        result.push_back(hex_char_from_nibble(byte & 0x0F));
    }

    return result;
}

inline void hex_string_to_bytes(const std::string_view hex_string, byte_t *buf, std::size_t length) {
    for (std::size_t i = 0; i < hex_string.length() && i < length * 2; i++) {
        byte_t nibble = nibble_from_hex_char(hex_string[i]);
        if (i % 2 == 0) {
            buf[i / 2] = nibble << 4;
        } else {
            buf[i / 2] |= nibble;
        }
    }
}

double half_normal_cdf(double y);
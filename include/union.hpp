#pragma once

#include <stdint.h>
#include <conditional_memcpy.hpp>

template <class T>
uint64_t union_linear_scanning(const T* input, T*output, uint64_t input_count) {
    uint64_t output_index = 0;
    for (uint64_t input_index = 0; input_index < input_count; input_index++) {
        bool match = false;
        for (uint64_t scanning_index = 0; scanning_index <= input_index; scanning_index++) {
            bool write_enable = scanning_index == output_index;

            conditional_memcpy(write_enable && (!match), output + scanning_index, input + input_index, sizeof(T));

            bool scanning_valid_data = scanning_index < output_index;
            bool data_match = input[input_index] == output[scanning_index];
            match |= (scanning_valid_data && data_match);
        }

        output_index = match ? output_index : output_index + 1;
    }

    return output_index;
}
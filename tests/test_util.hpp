#pragma once

#include <memory_interface.hpp>
#include <iterator>
#include <cstddef>

class PermutedSequence {
    public:
        static PermutedSequence create(uint64_t count, uint64_t reorder_mask = 0);

        class Iterator {
            using iterator_category = std::forward_iterator_tag;
            using difference_type   = std::ptrdiff_t;
            using value_type        = uint64_t;
            using pointer           = const uint64_t*;  // or also value_type*
            using reference         = const uint64_t&;  // or also value_type&

            private:
                Iterator(const PermutedSequence *parent, uint64_t index, uint64_t value);
            
            public:
                reference operator*() const { 
                    return this->value;
                }
                pointer operator->() const { 
                    return &(this->value); 
                }
                Iterator& operator++();

                // Postfix increment
                Iterator operator++(int);

                bool operator== (const Iterator& other);
                bool operator!= (const Iterator& other);
            private:
                uint64_t index;
                uint64_t value;
                const PermutedSequence * parent;
            
            friend PermutedSequence;
        };

        Iterator begin() const;
        Iterator end() const;
    private:
        PermutedSequence(uint64_t start_count, uint64_t end_count, uint64_t reorder_mask, uint64_t count);
        uint64_t get_reordered_value(uint64_t index) const;
        const uint64_t count;
        const uint64_t start_index;
        const uint64_t end_index;
        const uint64_t reorder_mask;
};

void basic_backed_memory_test(Memory *memory);
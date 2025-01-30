#include "test_util.hpp"
#include <absl/random/random.h>
#include <util.hpp>
#include <iostream>
#include <gtest/gtest.h>

PermutedSequence 
PermutedSequence::create(uint64_t count, uint64_t reorder_mask) {

    if (reorder_mask == 0) {
        // generate a new reorder mask
        absl::BitGen bit_gen;
        reorder_mask = absl::Uniform<uint64_t>(absl::IntervalClosed, bit_gen, 0, ULONG_LONG_MAX);
    }

    uint64_t end_count = 1UL << (num_bits(count - 1));

    uint64_t start_count;

    for (start_count = 0; start_count < end_count; start_count++) {
        if ((start_count ^ reorder_mask) % end_count < count) {
            break;
        }
    }

    return PermutedSequence(start_count, end_count, reorder_mask, count);

}

PermutedSequence::PermutedSequence(uint64_t start_count, uint64_t end_count, uint64_t reorder_mask, uint64_t count) :
start_index(start_count), end_index(end_count), reorder_mask(reorder_mask), count(count)
{}

PermutedSequence::Iterator::Iterator(const PermutedSequence *parent, uint64_t index, uint64_t value) :
index(index), parent(parent), value(value)
{}

PermutedSequence::Iterator&
PermutedSequence::Iterator::operator++() {
    if (this->index <= this->parent->end_index) {
        do{
            this->index++;
        } while (this->index < this->parent->end_index && this->parent->get_reordered_value(index) >= this->parent->count);

        this->value = this->parent->get_reordered_value(index);
    } else {
        this->value = 0;
    }

    return (*this);
}

// Postfix increment
PermutedSequence::Iterator
PermutedSequence::Iterator::operator++(int) {
    Iterator tmp = (*this);
    ++(*this);
    return tmp;
}

bool 
PermutedSequence::Iterator::operator== (const Iterator& other) {
    return this->index == other.index;
}

bool 
PermutedSequence::Iterator::operator!= (const Iterator& other) {
    return !(*this == other);
}

PermutedSequence::Iterator 
PermutedSequence::begin() const {
    return Iterator(this, start_index, this->get_reordered_value(this->start_index));
}

PermutedSequence::Iterator
PermutedSequence::end() const {
    return Iterator(this, end_index, 0);
}

uint64_t 
PermutedSequence::get_reordered_value(uint64_t index) const {
    return (index ^ this->reorder_mask) % this->end_index;
}

void basic_backed_memory_test(Memory *memory) {
    constexpr uint64_t write_mask = 0xCAFEBABEDEADBEEFUL;
    constexpr uint64_t read_write_mask = 0x3141592653589793UL;
    constexpr uint64_t read_mask = 0x2718281828459045UL;
    constexpr uint64_t data_mask_1 = 0xDEADBEEFCAFEBABEUL;
    constexpr uint64_t data_mask_2 = 0xFFCCEEAA08922313UL;
    uint64_t block_size = memory->page_size();
    uint64_t memory_size = memory->size();
    uint64_t num_blocks = memory_size / block_size;
    PermutedSequence p = PermutedSequence::create(num_blocks, 0xDEADBEEF);
    uint64_t read_data_mask = data_mask_1;
    for (auto &block_index : p) {
        MemoryRequest req(MemoryRequestType::WRITE, block_index * block_size, block_size);
        uint64_t data = block_index ^ data_mask_1;
        if (block_size >= 8) {
            *((uint64_t *)req.data.data()) = data;
        } else if (block_size >= 4) {
            *((uint32_t *)req.data.data()) = (uint32_t)(data & 0xFFFFFFFFUL);
        } else if (block_size >= 2) {
            *((uint16_t *)req.data.data()) = (uint16_t)(data & 0xFFFFUL);
        } else {
            *((uint8_t *)req.data.data()) = (uint8_t)(data & 0xFFUL);
        }
        memory->access(req);
    }

    if (memory->is_request_type_supported(MemoryRequestType::READ_WRITE)) {
        read_data_mask = data_mask_2;
        for (auto &block_index : p) {
            MemoryRequest req(MemoryRequestType::READ_WRITE, block_index * block_size, block_size);
            uint64_t write_data = block_index ^ data_mask_2;
            if (block_size >= 8) {
                *((uint64_t *)req.data.data()) = write_data;
            } else if (block_size >= 4) {
                *((uint32_t *)req.data.data()) = (uint32_t)(write_data & 0xFFFFFFFFUL);
            } else if (block_size >= 2) {
                *((uint16_t *)req.data.data()) = (uint16_t)(write_data & 0xFFFFUL);
            } else {
                *((uint8_t *)req.data.data()) = (uint8_t)(write_data & 0xFFUL);
            }
            memory->access(req);
            uint64_t read_data = block_index ^ data_mask_1;
            if (block_size >= 8) {
                ASSERT_EQ(*((uint64_t *)req.data.data()), read_data);
            } else if (block_size >= 4) {
                ASSERT_EQ(*((uint32_t *)req.data.data()), (uint32_t)(read_data & 0xFFFFFFFFUL));
            } else if (block_size >= 2) {
                ASSERT_EQ(*((uint16_t *)req.data.data()), (uint16_t)(read_data & 0xFFFFUL));
            } else {
                ASSERT_EQ(*((uint8_t *)req.data.data()), (uint8_t)(read_data & 0xFFUL));
            }
        }
    }

    for (auto &block_index : p) {
        MemoryRequest req(MemoryRequestType::READ, block_index * block_size, block_size);
        memory->access(req);
        uint64_t read_data = block_index ^ read_data_mask;
        if (block_size >= 8) {
            ASSERT_EQ(*((uint64_t *)req.data.data()), read_data);
        } else if (block_size >= 4) {
            ASSERT_EQ(*((uint32_t *)req.data.data()), (uint32_t)(read_data & 0xFFFFFFFFUL));
        } else if (block_size >= 2) {
            ASSERT_EQ(*((uint16_t *)req.data.data()), (uint16_t)(read_data & 0xFFFFUL));
        } else {
            ASSERT_EQ(*((uint8_t *)req.data.data()), (uint8_t)(read_data & 0xFFUL));
        }
    }
}
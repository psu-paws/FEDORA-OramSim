#include <gtest/gtest.h>
#include <memory_interface.hpp>
#include <simple_memory.hpp>
#include <test_util.hpp>

TEST(SimpleMemoryTest, TestBackedMemory) {
    unique_memory_t test_mem = BackedMemory::create("test_memory", 4096, 64);
    basic_backed_memory_test(test_mem.get());
}
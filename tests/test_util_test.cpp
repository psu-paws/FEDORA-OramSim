#include <gtest/gtest.h>
#include <test_util.hpp>
#include <stdint.h>
#include <unordered_set>
#include <absl/strings/str_format.h>
#include <iostream>

TEST(TestUtilTest, TestPermutation) {
    constexpr uint64_t test_size = 100;
    PermutedSequence ps = PermutedSequence::create(test_size, 0xDEADBEEF);
    std::unordered_set<uint64_t> values;
    uint64_t count = 0;
    for (auto v : ps) {
        auto result = values.emplace(v);
        ASSERT_TRUE(result.second) << absl::StrFormat("Duplicated element detected at index %lu of value %lu", count, *result.first);
        count ++;
    }

    ASSERT_EQ(count, test_size);

    for (uint64_t i = 0; i < test_size; i++) {
        ASSERT_EQ(values.count(i), 1) << absl::StrFormat("Missing value %lu", i);
    }
}
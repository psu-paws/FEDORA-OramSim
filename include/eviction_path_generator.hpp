#pragma once
#include <vector>

#include <memory_defs.hpp>
#include <toml++/toml.h>

class EvictionPathGenerator final{
    public:
        EvictionPathGenerator(const std::vector<int64_t> &level_sizes);
        EvictionPathGenerator(std::vector<int64_t> &&level_sizes);
        EvictionPathGenerator(const toml::table *table);
        int64_t next_path();

        toml::table to_toml() const;
    private:
        const std::vector<int64_t> level_sizes;
        std::vector<int64_t> level_indices;
};
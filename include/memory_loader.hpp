#pragma once

#include <unordered_map>
#include <filesystem>
#include <memory_interface.hpp>

namespace MemoryLoader{
    extern std::unordered_map<std::string, unique_memory_t (*)(const std::filesystem::path &, const toml::table &)> memory_loader_map;

    unique_memory_t load(const std::filesystem::path &location);
}
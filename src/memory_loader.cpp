#include <memory_loader.hpp>
#include <fstream>
#include <algorithm>

#include "util.hpp"
#include "simple_memory.hpp"
#include "oram.hpp"
#include "disk_memory.hpp"
#include "memory_adapters.hpp"
#include "cache.hpp"
#include <page_optimized_raw_oram.hpp>
#include <binary_path_oram_2.hpp>

#include <absl/strings/str_format.h>

std::unordered_map<std::string, unique_memory_t (*)(const std::filesystem::path &, const toml::table &)> MemoryLoader::memory_loader_map = {
    {"BackedMemory", BackedMemory::load_from_disk},
    // {"AccessLogger", AccessLogger::load_from_disk},
    {"BinaryPathOram", BinaryPathOram::load_from_disk},
    {"DiskMemory", DiskMemory::load_from_disk},
    {"BitfieldAdapter", BitfieldAdapter::load_from_disk},
    // {"RAWOram", RAWOram::load_from_disk},
    {"BlockDiskMemory", BlockDiskMemory::load_from_disk},
    {"SplitMemory", SplitMemory::load_from_disk},
    {"Cache", Cache::load_from_disk},
    {"PageOptimizedRAWOram", PageOptimizedRAWOram::load_from_disk},
    {"MultiSplitMemory", MultiSplitMemory::load_from_disk},
    {"BlockDiskMemoryLibAIO", BlockDiskMemoryLibAIO::load_from_disk},
    {"BinaryPathOram2", BinaryPathOram2::load_from_disk},
    {"LinearScannedMemory", LinearScannedMemory::load_from_disk},
    {"BlockDiskMemoryLibAIOCached", BlockDiskMemoryLibAIOCached::load_from_disk}
};

unique_memory_t MemoryLoader::load(const std::filesystem::path &location) {
    std::filesystem::path config_file_path = location / "config.toml";

    // check that the config file exists
    if (!std::filesystem::is_regular_file(config_file_path)) {
        throw std::runtime_error(absl::StrFormat("Unable to find config file at %s!", config_file_path));
    }

    auto table = toml::parse_file(config_file_path.string());

    std::string type = table["type"].value<std::string>().value();

    // try finding the correct loader in memory loader map
    auto loader_iter = memory_loader_map.find(type);

    if (loader_iter == memory_loader_map.end()) {
        // loader was not found
        throw std::runtime_error(absl::StrFormat("Attempted to load unknown memory type %s!", type));
    }

    // use the loader to load the memory
    auto loader = loader_iter->second;
    return loader(location, table);
}
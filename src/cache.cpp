#include <cache.hpp>
#include <util.hpp>
#include <memory_loader.hpp>
#include <absl/strings/str_format.h>
#include <iostream>

void 
CacheStatistics::clear() {
    this->MemoryStatistics::clear();
    this->hits = 0;
}

toml::table 
CacheStatistics::to_toml() const{
    auto table = this->MemoryStatistics::to_toml();
    table.emplace("hits", this->hits);
    table.emplace("misses", this->read_requests + this->write_requests - this->hits);
    table.emplace("miss_rate", 1.0 - ((double)this->hits / (double)(this->read_requests + this->write_requests)));
    return table;
}

void 
CacheStatistics::from_toml(const toml::table &table) {
    this->MemoryStatistics::from_toml(table);
    this->hits = table["hits"].value<int64_t>().value();
}

Cache::Cache(std::string_view type, std::string_view name, uint64_t num_blocks, unique_memory_t &&memory, CacheStatistics *statistics) :
Memory(type, name, memory->size(), statistics),
cache_statistics(statistics),
memory(std::move(memory)),
block_size(this->memory->page_size())
{
    for (uint64_t i = 0; i < num_blocks; i++) {
        this->cache_blocks.emplace(this->cache_blocks.end(), MemoryRequestType::READ, 0, this->block_size);
    }

    for (uint64_t i = 0; i < num_blocks; i++) {
        this->cache_metadata.emplace_back(CacheMeta{this->cache_blocks[i], false, false});
    }
}

Cache::Cache(std::string_view type, const toml::table &table, unique_memory_t &&memory, CacheStatistics *statistics) : 
Cache(table["type"].value<std::string_view>().value(), table["name"].value<std::string_view>().value(), parse_size(*(table["num_blocks"].node())), std::move(memory), statistics)
{
}

bool 
Cache::isBacked() const {
    return this->memory->isBacked();
}

uint64_t 
Cache::size() const {
    return this->memory->size();
}

uint64_t 
Cache::page_size() const {
    return this->block_size;
}

void 
Cache::access(MemoryRequest &request) {
    if (request.address % this->block_size != 0 || request.size != this->block_size) {
        throw std::runtime_error("Cache does not support non-aligned accesses");
    }

    // check if block is already in cache;
    auto cache_map_iter = this->cache_map.find(request.address);
    if (cache_map_iter == this->cache_map.end()) {
        // Block not in cache;
        // get the first (least recently used block in cache)
        CacheMeta cache_meta = this->cache_metadata.front();
        this->cache_metadata.erase(this->cache_metadata.begin());

        std::cout << absl::StrFormat("Cache Miss on block %lu, evicting block %lu\n", request.address / this->block_size, cache_meta.block.address / this->block_size);

        // erase cache map entry
        if(cache_meta.valid) {
            auto evicted_map_iter = this->cache_map.find(cache_meta.block.address);
            if (evicted_map_iter != this->cache_map.end()) {
                this->cache_map.erase(evicted_map_iter);
            }

            // write back if required
            if (cache_meta.valid && cache_meta.dirty) {
                cache_meta.block.type = MemoryRequestType::WRITE;
                this->memory->access(cache_meta.block);
                this->statistics->add_read_write(0UL, this->block_size);
            }
        }

        // fetch new block
        cache_meta.block.type = MemoryRequestType::READ;
        cache_meta.block.address = request.address;
        this->memory->access(cache_meta.block);
        this->statistics->add_read_write(this->block_size, 0UL);

        // set flags
        cache_meta.valid = true;
        cache_meta.dirty = false;

        // put into the back of the list
        auto cache_meta_iter = this->cache_metadata.emplace(this->cache_metadata.end(), std::move(cache_meta));

        // put into cache map
        auto emplace_result = this->cache_map.emplace(std::make_pair(request.address, cache_meta_iter));
        assert(emplace_result.second);
        cache_map_iter = emplace_result.first;
    } else {
        std::cout << absl::StrFormat("Cache Hit on block %lu\n", request.address / this->block_size);
        this->cache_statistics->hits++;
        // move the metadata block to the end of the list to mark it as recently accessed
        CacheMeta cache_meta = *(cache_map_iter->second);
        this->cache_metadata.erase(cache_map_iter->second);
        cache_map_iter->second = this->cache_metadata.emplace(this->cache_metadata.end(), std::move(cache_meta));
    }

    // do the access
    switch (request.type) {
        case MemoryRequestType::READ:
        std::memcpy(request.data.data(), cache_map_iter->second->block.data.data(), request.size); // copy data over
        break;

        case MemoryRequestType::WRITE:
        std::memcpy(cache_map_iter->second->block.data.data(), request.data.data(), request.size);
        break;

        default:
        throw std::invalid_argument("unkown memory request type");
    }

    this->log_request(request);
}

void 
Cache::start_logging(bool append) {
    this->Memory::start_logging(append);
    this->memory->start_logging(append);

}

void 
Cache::stop_logging() {
    this->Memory::stop_logging();
    this->memory->stop_logging();
}

bool 
Cache::is_request_type_supported(MemoryRequestType type) const {
    switch (type)
    {
    case MemoryRequestType::READ:
    case MemoryRequestType::WRITE:
        return true;
        break;
    
    default:
        return false;
        break;
    }
}

toml::table 
Cache::to_toml() const {
    auto table = this->to_toml_self();
    table.emplace("memory", this->memory->to_toml());
    return table;
}

void 
Cache::save_to_disk(const std::filesystem::path &location) const {
    // write config file
    std::ofstream config_file(location / "config.toml");
    config_file << this->to_toml_self() << "\n";

    // write out upper and lower
    std::filesystem::path memory_directory = location / "memory";
    std::filesystem::create_directory(memory_directory);
    this->memory->save_to_disk(memory_directory);
}

void 
Cache::reset_statistics(bool from_file) {
    this->Memory::reset_statistics(from_file);
    this->memory->reset_statistics(from_file);

}

void 
Cache::save_statistics() {
    this->Memory::save_statistics();
    this->memory->save_statistics();
}

toml::table 
Cache::to_toml_self() const {
    auto table = this->Memory::to_toml();
    table.emplace("num_blocks", size_to_string(this->cache_blocks.size()));
    return table;
}

unique_memory_t 
Cache::load_from_disk(const std::filesystem::path &location) {
    auto table = toml::parse_file((location / "config.txt").string());
    return Cache::load_from_disk(location, table);
}

unique_memory_t 
Cache::load_from_disk(const std::filesystem::path &location, const toml::table &table) {
    unique_memory_t memory = MemoryLoader::load(location / "memory");

    return unique_memory_t(
        new Cache("Cache", table, std::move(memory), new CacheStatistics())
    );
}
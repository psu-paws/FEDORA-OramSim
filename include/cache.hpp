#pragma once

#include <memory_interface.hpp>
#include <vector>
#include <list>
#include <unordered_map>

class CacheStatistics : public MemoryStatistics {
    public:
    int64_t hits;
    virtual void clear() override;
    virtual toml::table to_toml() const override;
    virtual void from_toml(const toml::table &table) override;
    virtual ~CacheStatistics() = default;
};

class Cache : public Memory {
    protected:
    Cache(std::string_view type, std::string_view name, uint64_t num_blocks, unique_memory_t &&memory, CacheStatistics *statistics);
    Cache(std::string_view type, const toml::table &table, unique_memory_t &&memory, CacheStatistics *statistics);

    public:
    virtual bool isBacked() const override;
    virtual uint64_t size() const override;
    virtual uint64_t page_size() const override;
    virtual void access(MemoryRequest &request) override;
    // virtual void batch_access(std::vector<MemoryRequest> &requests) override;
    virtual void start_logging(bool append = false) override;
    virtual void stop_logging() override;
    virtual bool is_request_type_supported(MemoryRequestType type) const override;
    virtual toml::table to_toml() const override;
    virtual void save_to_disk(const std::filesystem::path &location) const override;

    static unique_memory_t load_from_disk(const std::filesystem::path &location);
    static unique_memory_t load_from_disk(const std::filesystem::path &location, const toml::table &table);
    virtual void reset_statistics(bool from_file = false) override;
    virtual void save_statistics() override;
    protected:
    virtual toml::table to_toml_self() const;

    protected:
    struct CacheMeta
    {
        MemoryRequest &block;
        bool valid;
        bool dirty;
    };
    

    protected:
    CacheStatistics *cache_statistics;
    unique_memory_t memory;
    const uint64_t block_size;
    std::vector<MemoryRequest> cache_blocks;
    std::list<CacheMeta> cache_metadata;
    std::unordered_map<uint64_t, std::list<CacheMeta>::iterator> cache_map;
};

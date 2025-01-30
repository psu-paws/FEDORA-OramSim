#pragma once
#include <stdint.h>
#include <memory_defs.hpp>
#include <toml++/toml.h>

class MemoryStatistics{
    public:
    virtual void clear();
    virtual void record_request(const MemoryRequest &request);
    virtual void add_read_write(uint64_t bytes_read, uint64_t bytes_wrote);
    virtual toml::table to_toml() const;
    virtual void from_toml(const toml::table &table);
    virtual ~MemoryStatistics() = default;
    public:
    int64_t read_requests = 0;
    int64_t write_requests = 0;
    int64_t read_bytes_requested = 0;
    int64_t write_bytes_requested = 0;
    int64_t bytes_read = 0;
    int64_t bytes_wrote = 0;
};
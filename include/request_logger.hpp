#pragma once

#include <memory_defs.hpp>
#include <filesystem>
#include <string_view>

class RequestLogger {
    public:
    RequestLogger(std::string_view name, uint64_t size);
    void start_logging(bool append = false);
    void log_request(const MemoryRequest &request);
    void barrier();
    void stop_logging();

    private:
    const uint64_t size;
    const std::filesystem::path log_path;
    std::unique_ptr<std::ostream> output;
    static uint64_t log_count;
};
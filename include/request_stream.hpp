#pragma once

#include <memory_defs.hpp>
#include <memory>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <absl/random/random.h>

class AccessPattern {
    public:
    virtual uint64_t get_address(uint64_t max, absl::BitGen &bit_gen) const = 0;
    virtual ~AccessPattern() = default;
};

class ConcreteAccessPattern : public AccessPattern {
    private:
    const uint64_t skew_limit;
    const std::vector<uint64_t> skew_values;

    public:
    ConcreteAccessPattern(std::vector<uint64_t> &&skew_values, uint64_t skew_limit);
    uint64_t get_address(uint64_t max, absl::BitGen &bit_gen) const override;
    virtual ~ConcreteAccessPattern() = default;
};

class ExponentialPattern : public AccessPattern {
    private:
    const double lambda;
    
    public:
    ExponentialPattern(double lambda = 1.0);
    uint64_t get_address(uint64_t max, absl::BitGen &bit_gen) const override;
    virtual ~ExponentialPattern() = default;
};

extern std::unordered_map<std::string, std::unique_ptr<AccessPattern>> pattern_map;

const AccessPattern &get_pattern_by_name(std::string_view name);

class RequestStream {
    public:
    virtual bool next() = 0;
    virtual MemoryRequest get() const {
        MemoryRequest request;
        this->inplace_get(request);
        return request;
    }
    virtual void inplace_get(MemoryRequest &request) const = 0;

    virtual ~RequestStream() = default;
};

typedef std::unique_ptr<RequestStream> unique_request_stream_t;

class TraceRequestStream : public RequestStream {
    public:
    static unique_request_stream_t create(std::filesystem::path trace_file);

    public:
    virtual bool next() override;
    virtual void inplace_get(MemoryRequest &request) const;

    protected:
    TraceRequestStream(std::ifstream &&stream);

    std::ifstream stream;
    MemoryRequestType type;
    uint64_t address;
    uint64_t size;
};

class RandomRequestStream : public RequestStream {
    public:
    static unique_request_stream_t create(std::string_view pattern_type, uint64_t request_size, uint64_t memory_size);

    public:
    virtual bool next() override;
    virtual void inplace_get(MemoryRequest &request) const;

    protected:
    RandomRequestStream(const AccessPattern &pattern, uint64_t request_size, uint64_t memory_size);

    const AccessPattern &pattern;
    absl::BitGen bit_gen;
    std::ifstream stream;
    uint64_t address;
    const uint64_t request_size;
    const uint64_t memory_size;
};

class ReuseStream: public RequestStream {

    public:
    static unique_request_stream_t create(std::string_view pattern_type, uint64_t request_size, uint64_t memory_size);

    public:
    virtual bool next() override;
    virtual void inplace_get(MemoryRequest &request) const;

    ~ReuseStream() = default;

    protected:
    ReuseStream(double reuse_fraction, uint64_t request_size, uint64_t memory_size);

    absl::BitGen bit_gen;
    std::uint64_t index;
    std::uint64_t next_index;
    const double reuse_fraction;
    const uint64_t request_size;
    const uint64_t memory_size;

    std::vector<std::uint64_t> permutation;

};
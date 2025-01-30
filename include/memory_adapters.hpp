#pragma once

#include <memory_interface.hpp>
#include <map>
#include <util.hpp>

class BitfieldAdapter final: public Memory {
    public:
    static unique_memory_t create(std::string_view name, unique_memory_t child);

    protected:
    BitfieldAdapter(std::string_view type, std::string_view name, unique_memory_t &&child, MemoryStatistics *statistics);
    BitfieldAdapter(std::string_view type, const toml::table &table, unique_memory_t &&child, MemoryStatistics *statistics);

    public:
    virtual bool isBacked() const override;
    virtual uint64_t size() const override;
    virtual uint64_t page_size() const override;
    virtual void access(MemoryRequest &request) override;
    virtual void batch_access(std::vector<MemoryRequest> &requests) override;
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
    unique_memory_t child;
    const uint64_t block_size;
};

class SplitMemory final: public Memory {
    public:
    static unique_memory_t create(std::string_view name, unique_memory_t &&lower_memory, unique_memory_t &&upper_memory);

    protected:
    SplitMemory(std::string_view type, std::string_view name, unique_memory_t &&lower_memory, unique_memory_t &&upper_memory, MemoryStatistics *statistics);
    SplitMemory(std::string_view type, const toml::table &table, unique_memory_t &&lower_memory, unique_memory_t &&upper_memory, MemoryStatistics *statistics);

    public:
    virtual bool isBacked() const override;
    virtual uint64_t size() const override;
    virtual uint64_t page_size() const override;
    virtual void access(MemoryRequest &request) override;
    virtual void batch_access(std::vector<MemoryRequest> &requests) override;
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
    const uint64_t upper_offset;
    unique_memory_t lower_memory;
    unique_memory_t upper_memory;
};

class MultiSplitMemory final: public Memory {
    protected:
    struct SectionDescriptor{
        const bool is_upper;
        const addr_t child_offset;
        const addr_t memory_offset;

        SectionDescriptor(bool is_upper, addr_t child_offset, addr_t memory_offset):
        is_upper(is_upper), child_offset(child_offset), memory_offset(memory_offset)
        {}

        SectionDescriptor(const toml::table &table) :
        is_upper(table["is_upper"].value<bool>().value()),
        child_offset(parse_size(*table["child_offset"].node())),
        memory_offset(parse_size(*table["memory_offset"].node()))
        {}

        toml::table to_toml() const {
            return toml::table{
                {"is_upper", this->is_upper},
                {"child_offset", size_to_string(this->child_offset)},
                {"memory_offset", size_to_string(this->memory_offset)}
            };
        }
    };
    public:
    static unique_memory_t create(std::string_view name, unique_memory_t &&lower_memory, unique_memory_t &&upper_memory, const std::vector<addr_t> &split_points);

    protected:
    MultiSplitMemory(
        std::string_view type, std::string_view name, 
        unique_memory_t &&lower_memory, unique_memory_t &&upper_memory, 
        const std::vector<addr_t> &split_points,
        MemoryStatistics *statistics
    );
    MultiSplitMemory(
        std::string_view type,
        const toml::table &table,
        unique_memory_t &&lower_memory, unique_memory_t &&upper_memory, 
        MemoryStatistics *statistics
    );

    static std::map<addr_t, SectionDescriptor, std::greater<addr_t>> generate_mapping(const std::vector<addr_t> &split_points, addr_t total_size);
    static std::map<addr_t, SectionDescriptor, std::greater<addr_t>> mapping_from_toml(const toml::array &array);

    public:
    virtual bool isBacked() const override;
    virtual uint64_t size() const override;
    virtual uint64_t page_size() const override;
    virtual void access(MemoryRequest &request) override;
    virtual void batch_access(std::vector<MemoryRequest> &requests) override;
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
    unique_memory_t lower_memory;
    unique_memory_t upper_memory;
    const std::map<addr_t, SectionDescriptor, std::greater<addr_t>> mapping;
};
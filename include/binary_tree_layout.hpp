#pragma once
#include <stdint.h>
#include <memory>
#include <unordered_map>
#include <toml++/toml.h>
#include <memory_defs.hpp>
#include <request_coalescer.hpp>

class BinaryTreeLayout {
    public:
    BinaryTreeLayout(std::string_view type, uint64_t levels, uint64_t bucket_size, uint64_t page_size) : _type(type), levels(levels), bucket_size(bucket_size), page_size(page_size){};
    virtual uint64_t get_address(uint64_t path, uint64_t level) const = 0;
    virtual uint64_t size() const = 0;
    virtual const std::string_view type() const {return std::string_view(this->_type);};
    virtual void setup_path_access(MemoryRequest *requests, MemoryRequestType request_type, uint64_t path) const;
    virtual BlockRequestMap get_request_map(uint64_t path, uint64_t index_offset = 0);
    virtual ~BinaryTreeLayout() = default;
    protected:
    const std::string _type;
    const uint64_t levels;
    const uint64_t bucket_size;
    const uint64_t page_size;
};

typedef std::unique_ptr<BinaryTreeLayout> unique_tree_layout_t;

namespace BinaryTreeLayoutFactory
{
    // extern std::unordered_map<std::string, unique_tree_layout_t (*)(const toml::table &)> tree_address_generator_loader_map;
    extern std::unordered_map<std::string, unique_tree_layout_t (*)(uint64_t, uint64_t, uint64_t, uint64_t)> tree_layout_creator_map;
    // unique_tree_layout_t from_toml(const toml::table &table);
    unique_tree_layout_t create(std::string_view type, uint64_t levels, uint64_t bucket_size, uint64_t page_size, uint64_t offset = 0);
}


class BasicHeapLayout : public BinaryTreeLayout{
    public:
    // static unique_tree_layout_t from_toml(const toml::table &table);
    static unique_tree_layout_t create(uint64_t levels, uint64_t bucket_size, uint64_t page_size, uint64_t offset = 0);
    protected:
    BasicHeapLayout(std::string_view type,uint64_t levels, uint64_t bucket_size, uint64_t page_size, uint64_t offset);
    public:
    virtual uint64_t get_address(uint64_t path, uint64_t level) const override;
    virtual uint64_t size() const override;
    private:
    const uint64_t offset;  
};

class TwoLevelHeapLayout : public BinaryTreeLayout {
    public:
    // static unique_tree_layout_t from_toml(const toml::table &table);
    static unique_tree_layout_t create(uint64_t levels, uint64_t bucket_size, uint64_t page_size, uint64_t offset = 0);
    protected:
    TwoLevelHeapLayout(
        std::string_view type,
        uint64_t levels, uint64_t bucket_size, uint64_t page_size, uint64_t offset,
        uint64_t num_page_levels, uint64_t levels_per_page, uint64_t first_page_levels,
        std::vector<uint64_t> &&page_level_offsets
    );
    public:
    virtual uint64_t get_address(uint64_t path, uint64_t level) const override;
    virtual void setup_path_access(MemoryRequest *requests, MemoryRequestType request_type, uint64_t path) const override;
    virtual uint64_t size() const override;

    
    protected:
    const uint64_t offset;

    const uint64_t num_page_levels;
    const uint64_t levels_per_page;
    const uint64_t first_page_levels;

    const std::vector<uint64_t> page_level_offsets;
};

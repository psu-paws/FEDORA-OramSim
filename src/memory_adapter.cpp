#include "memory_adapters.hpp"

#include <request_coalescer.hpp>
#include <fstream>
#include <absl/strings/str_format.h>
#include <util.hpp>
#include <memory_loader.hpp>
#include <iostream>
#include <vector>


unique_memory_t 
BitfieldAdapter::create(std::string_view name ,unique_memory_t child) {
    return unique_memory_t(new BitfieldAdapter("BitfieldAdapter", name, std::move(child), new MemoryStatistics()));
}

BitfieldAdapter::BitfieldAdapter(std::string_view type, std::string_view name ,unique_memory_t &&child, MemoryStatistics *statistics) :
Memory(type, name, child->size() * 8, statistics),
child(std::move(child)), block_size(this->child->page_size())
{
}

BitfieldAdapter::BitfieldAdapter(std::string_view type, const toml::table &table, unique_memory_t &&child, MemoryStatistics *statistics) :
Memory(type, table, child->size() * 8, statistics),
child(std::move(child)), block_size(this->child->page_size())
{
}

bool 
BitfieldAdapter::isBacked() const {
    return this->child->isBacked();
}

uint64_t 
BitfieldAdapter::size() const {
    return this->child->size() * 8;
}

uint64_t 
BitfieldAdapter::page_size() const {
    return this->block_size * 8;
}

void
BitfieldAdapter::access(MemoryRequest &request) {
    std::vector<MemoryRequest> requests;
    requests.emplace_back(request);
    this->batch_access(requests);
    request = requests.front();
}


void 
BitfieldAdapter::batch_access(std::vector<MemoryRequest> &requests) {
    RequestCoalescer req_col(this->block_size * 8);
    
    // coalescer requests
    for (uint64_t i = 0; i < requests.size(); i++) {
        this->Memory::log_request(requests[i]);
        req_col.add_request(i, requests[i]);
    }

    const BlockRequestMap &block_request_map = req_col.get_map_ref();

    // request to be passed to the child
    std::vector<MemoryRequest> child_requests;

    for (const auto &request_pair : block_request_map) {
        child_requests.emplace_back(MemoryRequestType::READ, request_pair.first * block_size, block_size);
    }

    this->statistics->add_read_write(child_requests.size() * this->block_size, child_requests.size() * this->block_size);

    // execute requests on child
    this->child->batch_access(child_requests);

    // do the read and writes
    bool has_writes = false;
    uint64_t child_request_index = 0;
    for (const auto &request_pair: block_request_map) {
        MemoryRequest &child_request = child_requests[child_request_index];
        for (const auto &entry: request_pair.second) {
            MemoryRequest &request = requests[entry.request_id];
            for (uint64_t i = 0; i < entry.size; i++) {
                uint64_t block_bit_offset = entry.block_offset + i;
                uint64_t request_offset = entry.request_offset + i;
                byte_t mask = 1UL << (block_bit_offset % 8);

                if (request.type == MemoryRequestType::READ) {
                    if (child_request.data[block_bit_offset / 8] & mask) {
                        // read 1
                        request.data[request_offset] = 1;
                    } else {
                        // read 0
                        request.data[request_offset] = 0;
                    }
                } else if (request.type == MemoryRequestType::WRITE) {
                    has_writes = true;
                    if (request.data[request_offset]) {
                        // write 1
                        child_request.data[block_bit_offset / 8] |= mask;
                    } else {
                        // write 0
                        child_request.data[block_bit_offset / 8] &= (~mask);
                    }
                }
            }
        }
        child_request_index ++;
    }

    // write the data back if there was a write access;
    if (has_writes) {
        for (auto &child_request : child_requests) {
            child_request.type = MemoryRequestType::WRITE;
        }

        this->child->batch_access(child_requests);
    }
}

void 
BitfieldAdapter::start_logging(bool append) {
    this->Memory::start_logging(append);
    this->child->start_logging(append);
}

void 
BitfieldAdapter::stop_logging() {
    this->Memory::stop_logging();
    this->child->stop_logging();
}

bool 
BitfieldAdapter::is_request_type_supported(MemoryRequestType type) const {
    switch (type)
    {
    case MemoryRequestType::READ:
    case MemoryRequestType::WRITE:
        return true;
    
    default:
        return false;
    }
}

toml::table 
BitfieldAdapter::to_toml_self() const {
    return Memory::to_toml();
}

toml::table 
BitfieldAdapter::to_toml() const {
    auto table = this->to_toml_self();
    table.emplace("child", this->child->to_toml());
    return table;
}

void 
BitfieldAdapter::save_to_disk(const std::filesystem::path &location) const {
    // write config file
    std::ofstream config_file(location / "config.toml");
    config_file << this->to_toml_self() << "\n";

    // write out child
    std::filesystem::path child_directory = location / "child";
    std::filesystem::create_directory(child_directory);
    this->child->save_to_disk(child_directory);
}

unique_memory_t 
BitfieldAdapter::load_from_disk(const std::filesystem::path &location, const toml::table &table) {
    unique_memory_t child = MemoryLoader::load(location / "child");

    return unique_memory_t(new BitfieldAdapter("BitfieldAdapter", table, std::move(child), new MemoryStatistics())); 
}

unique_memory_t 
BitfieldAdapter::load_from_disk(const std::filesystem::path &location) {
    auto table = toml::parse_file((location / "config.toml").string());
    return BitfieldAdapter::load_from_disk(location, table);
}

void 
BitfieldAdapter::reset_statistics(bool from_file) {
    this->Memory::reset_statistics(from_file);
    this->child->reset_statistics(from_file);
}

void 
BitfieldAdapter::save_statistics() {
    this->Memory::save_statistics();
    this->child->save_statistics();
}

unique_memory_t 
SplitMemory::create(std::string_view name, unique_memory_t &&lower_memory, unique_memory_t &&upper_memory) {
    return unique_memory_t(new SplitMemory(
        "SplitMemory", name,
        std::move(lower_memory), std::move(upper_memory),
        new MemoryStatistics
    ));
}

SplitMemory::SplitMemory(std::string_view type, std::string_view name, unique_memory_t &&lower_memory, unique_memory_t &&upper_memory, MemoryStatistics *statistics) :
Memory(type, name, lower_memory->size() + upper_memory->size(), statistics),
upper_offset(lower_memory->size()),
lower_memory(std::move(lower_memory)),
upper_memory(std::move(upper_memory))
{}

SplitMemory::SplitMemory(std::string_view type, const toml::table &table, unique_memory_t &&lower_memory, unique_memory_t &&upper_memory, MemoryStatistics *statistics) :
Memory(type, table, lower_memory->size() + upper_memory->size(), statistics),
upper_offset(lower_memory->size()),
lower_memory(std::move(lower_memory)),
upper_memory(std::move(upper_memory))
{}

bool 
SplitMemory::isBacked() const {
    return this->lower_memory->isBacked() && this->upper_memory->isBacked();
}

uint64_t 
SplitMemory::size() const {
    return this->lower_memory->size() + this->upper_memory->size();
}

uint64_t 
SplitMemory::page_size() const {
    return std::max(this->lower_memory->page_size(), this->upper_memory->page_size());
}

void 
SplitMemory::access(MemoryRequest &request) {
    if (request.address >= upper_offset) {
        request.address -= upper_offset;
        this->upper_memory->access(request);
        request.address += upper_offset;
    } else {
        this->lower_memory->access(request);
    }
    this->log_request(request);
}

void 
SplitMemory::batch_access(std::vector<MemoryRequest> &requests) {
    std::vector<bool> is_upper;
    std::vector<MemoryRequest> lower_requests;
    std::vector<MemoryRequest> upper_requests;
    // split requests by memory
    for (uint64_t i = 0; i < requests.size(); i++) {
        if (requests[i].address >= this->upper_offset) {
            requests[i].address -= this->upper_offset;
            upper_requests.emplace_back(std::move(requests[i]));
            is_upper.emplace_back(true);
        } else {
            lower_requests.emplace_back(std::move(requests[i]));
            is_upper.emplace_back(false);
        }
    }

    // execute the requests
    if (lower_requests.size() > 0) {
        this->lower_memory->batch_access(lower_requests);
    }

    if (upper_requests.size() > 0) {
        this->upper_memory->batch_access(upper_requests);
    }

    // put everything back to its original place
    auto lower_iter = lower_requests.begin();
    auto upper_iter = upper_requests.begin();
    for (uint64_t i = 0; i < requests.size(); i++) {
        if (is_upper[i]) {
            upper_iter->address += upper_offset;
            requests[i] = std::move(*upper_iter);
            upper_iter++;
        } else {
            requests[i] = std::move(*lower_iter);
            lower_iter++;
        }

        this->log_request(requests[i]);
    }
}

void 
SplitMemory::start_logging(bool append) {
    this->Memory::start_logging(append);
    this->lower_memory->start_logging(append);
    this->upper_memory->start_logging(append);
}

void 
SplitMemory::stop_logging() {
    this->Memory::stop_logging();
    this->lower_memory->stop_logging();
    this->upper_memory->stop_logging();
}

bool 
SplitMemory::is_request_type_supported(MemoryRequestType type) const {
    return this->lower_memory->is_request_type_supported(type) && this->upper_memory->is_request_type_supported(type);
}

toml::table 
SplitMemory::to_toml_self() const {
    return this->Memory::to_toml();
}

toml::table 
SplitMemory::to_toml() const {
    auto table = this->to_toml_self();
    table.emplace("lower", this->lower_memory->to_toml());
    table.emplace("upper", this->upper_memory->to_toml());

    return table;
}

void 
SplitMemory::save_to_disk(const std::filesystem::path &location) const {
    // write config file
    std::ofstream config_file(location / "config.toml");
    config_file << this->to_toml_self() << "\n";

    // write out upper and lower
    std::filesystem::path lower_directory = location / "lower";
    std::filesystem::create_directory(lower_directory);
    this->lower_memory->save_to_disk(lower_directory);

    std::filesystem::path upper_directory = location / "upper";
    std::filesystem::create_directory(upper_directory);
    this->upper_memory->save_to_disk(upper_directory);
}

unique_memory_t 
SplitMemory::load_from_disk(const std::filesystem::path &location) {
    auto table = toml::parse_file((location / "config.toml").string());
    return SplitMemory::load_from_disk(location, table);
}

unique_memory_t 
SplitMemory::load_from_disk(const std::filesystem::path &location, const toml::table &table) {
    unique_memory_t lower = MemoryLoader::load(location / "lower");
    unique_memory_t upper = MemoryLoader::load(location / "upper");

    return unique_memory_t(new SplitMemory(
        "SplitMemory", table,
        std::move(lower),
        std::move(upper),
        new MemoryStatistics
    ));
}

void 
SplitMemory::reset_statistics(bool from_file) {
    this->Memory::reset_statistics(from_file);
    this->lower_memory->reset_statistics(from_file);
    this->upper_memory->reset_statistics(from_file);
}

void 
SplitMemory::save_statistics() {
    this->Memory::save_statistics();
    this->lower_memory->save_statistics();
    this->upper_memory->save_statistics();
}

MultiSplitMemory::MultiSplitMemory(
        std::string_view type, std::string_view name, 
        unique_memory_t &&lower_memory, unique_memory_t &&upper_memory, 
        const std::vector<addr_t> &split_points, 
        MemoryStatistics *statistics
) : Memory(type, name, lower_memory->size() + upper_memory->size(), statistics),
lower_memory(std::move(lower_memory)),
upper_memory(std::move(upper_memory)),
mapping(MultiSplitMemory::generate_mapping(split_points, this->lower_memory->size() + this->upper_memory->size()))
{}

MultiSplitMemory::MultiSplitMemory(
    std::string_view type,
    const toml::table &table,
    unique_memory_t &&lower_memory, unique_memory_t &&upper_memory, 
    MemoryStatistics *statistics
) : Memory(type, table,lower_memory->size() + upper_memory->size(), statistics),
lower_memory(std::move(lower_memory)),
upper_memory(std::move(upper_memory)),
mapping(MultiSplitMemory::mapping_from_toml(*table["mapping"].as_array()))
{}

unique_memory_t 
MultiSplitMemory::create(std::string_view name, unique_memory_t &&lower_memory, unique_memory_t &&upper_memory, const std::vector<addr_t> &split_points) {
    return unique_memory_t(
        new MultiSplitMemory(
            "MultiSplitMemory", name,
            std::move(lower_memory), std::move(upper_memory),
            split_points, new MemoryStatistics
        )
    );
}

std::map<addr_t, MultiSplitMemory::SectionDescriptor, std::greater<addr_t>> 
MultiSplitMemory::generate_mapping(const std::vector<addr_t> &split_points, addr_t total_size) {
    std::map<addr_t, SectionDescriptor, std::greater<addr_t>> mapping;
    addr_t start_address = 0;
    addr_t lower_offset = 0;
    addr_t upper_offset = 0;
    bool is_upper = false;
    for (uint64_t i = 0; i < split_points.size() + 1; i++) {
        addr_t end_address;
        if (i < split_points.size()) {
            end_address = split_points[i];
        } else {
            end_address = total_size;
        }

        addr_t section_size = end_address - start_address;

        if (section_size > 0) {
            mapping.emplace(start_address, SectionDescriptor(is_upper, is_upper ? upper_offset : lower_offset, start_address));

            if (is_upper) {
                upper_offset += section_size;
            } else {
                lower_offset += section_size;
            }
        }

        start_address = end_address;
        is_upper = !is_upper;
    }

    return mapping;
}

std::map<addr_t, MultiSplitMemory::SectionDescriptor, std::greater<addr_t>> 
MultiSplitMemory::mapping_from_toml(const toml::array &array) {
    std::map<addr_t, SectionDescriptor, std::greater<addr_t>> mapping;
    for (const auto &node : array) {
        const auto &table = *node.as_table();
        SectionDescriptor descriptor(table);
        mapping.emplace(descriptor.memory_offset, std::move(descriptor));
    }

    return mapping;
}

bool 
MultiSplitMemory::isBacked() const {
    return this->lower_memory->isBacked() && this->upper_memory->isBacked();
}

uint64_t 
MultiSplitMemory::size() const {
    return this->lower_memory->size() + this->upper_memory->size();
}

uint64_t 
MultiSplitMemory::page_size() const {
    return std::max(this->lower_memory->page_size(), this->upper_memory->page_size());
}

void 
MultiSplitMemory::access(MemoryRequest &request) {
    std::vector<MemoryRequest> requests;
    requests.emplace_back(request);
    this->batch_access(requests);
    request = requests.front();
    this->log_request(request);
}

void 
MultiSplitMemory::batch_access(std::vector<MemoryRequest> &requests) {
    std::vector<bool> is_upper;
    std::vector<MemoryRequest> lower_requests;
    std::vector<MemoryRequest> upper_requests;
    // split requests by memory
    for (uint64_t i = 0; i < requests.size(); i++) {
        MemoryRequest &request = requests[i];
        const auto &descriptor = this->mapping.lower_bound(request.address)->second;
        addr_t section_offset = request.address - descriptor.memory_offset;
        addr_t child_offset = descriptor.child_offset + section_offset;
        if (descriptor.is_upper) {
            upper_requests.emplace_back(request.type, child_offset, std::move(request.data));
            is_upper.emplace_back(true);
        } else {
            lower_requests.emplace_back(request.type, child_offset, std::move(request.data));
            is_upper.emplace_back(false);
        }
    }

    // execute the requests
    if (lower_requests.size() > 0) {
        this->lower_memory->batch_access(lower_requests);
    }

    if (upper_requests.size() > 0) {
        this->upper_memory->batch_access(upper_requests);
    }

    // // put everything back to its original place
    auto lower_iter = lower_requests.begin();
    auto upper_iter = upper_requests.begin();
    for (uint64_t i = 0; i < requests.size(); i++) {
        if (is_upper[i]) {
            requests[i].data = std::move(upper_iter->data);
            upper_iter++;
        } else {
            requests[i].data = std::move(lower_iter->data);;
            lower_iter++;
        }

        this->log_request(requests[i]);
    }
}

void 
MultiSplitMemory::start_logging(bool append) {
    this->Memory::start_logging(append);
    this->lower_memory->start_logging(append);
    this->upper_memory->start_logging(append);
}

void 
MultiSplitMemory::stop_logging() {
    this->Memory::stop_logging();
    this->lower_memory->stop_logging();
    this->upper_memory->stop_logging();
}

bool 
MultiSplitMemory::is_request_type_supported(MemoryRequestType type) const {
    return this->lower_memory->is_request_type_supported(type) && this->upper_memory->is_request_type_supported(type);
}

toml::table 
MultiSplitMemory::to_toml_self() const {
    auto table = this->Memory::to_toml();
    toml::array mapping_array;
    for (const auto &entry : this->mapping) {
        mapping_array.emplace_back(entry.second.to_toml());
    }
    table.emplace("mapping", mapping_array);
    return table;
}

toml::table 
MultiSplitMemory::to_toml() const {
    auto table = this->to_toml_self();
    table.emplace("lower", this->lower_memory->to_toml());
    table.emplace("upper", this->upper_memory->to_toml());

    return table;
}

void 
MultiSplitMemory::save_to_disk(const std::filesystem::path &location) const {
    // write config file
    std::ofstream config_file(location / "config.toml");
    config_file << this->to_toml_self() << "\n";

    // write out upper and lower
    std::filesystem::path lower_directory = location / "lower";
    std::filesystem::create_directory(lower_directory);
    this->lower_memory->save_to_disk(lower_directory);

    std::filesystem::path upper_directory = location / "upper";
    std::filesystem::create_directory(upper_directory);
    this->upper_memory->save_to_disk(upper_directory);
}

unique_memory_t 
MultiSplitMemory::load_from_disk(const std::filesystem::path &location) {
    auto table = toml::parse_file((location / "config.toml").string());
    return SplitMemory::load_from_disk(location, table);
}

unique_memory_t 
MultiSplitMemory::load_from_disk(const std::filesystem::path &location, const toml::table &table) {
    unique_memory_t lower = MemoryLoader::load(location / "lower");
    unique_memory_t upper = MemoryLoader::load(location / "upper");

    return unique_memory_t(new MultiSplitMemory(
        "MultiSplitMemory", table,
        std::move(lower),
        std::move(upper),
        new MemoryStatistics
    ));
}

void 
MultiSplitMemory::reset_statistics(bool from_file) {
    this->Memory::reset_statistics(from_file);
    this->lower_memory->reset_statistics(from_file);
    this->upper_memory->reset_statistics(from_file);
}

void 
MultiSplitMemory::save_statistics() {
    this->Memory::save_statistics();
    this->lower_memory->save_statistics();
    this->upper_memory->save_statistics();
}
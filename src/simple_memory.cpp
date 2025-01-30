#include <simple_memory.hpp>
#include <util.hpp>
#include <stdexcept>
#include <cstring>
#include <iostream>
#include <fstream>
#include <memory_type_ids.hpp>
#include <absl/strings/str_format.h>
#include <absl/strings/str_cat.h>
#include <memory_loader.hpp>
#include <toml++/toml.h>
#include <conditional_memcpy.hpp>

unique_memory_t 
BackedMemory::create(std::string_view name, uint64_t size, uint64_t page_size) {
    return unique_memory_t(new BackedMemory(name, size, page_size, new MemoryStatistics()));
}

BackedMemory::BackedMemory(std::string_view name, std::uint64_t size, uint64_t page_size, MemoryStatistics *statistics) : 
Memory("BackedMemory", name, size, statistics), _page_size(page_size){
    this->memory.resize(size);
}

BackedMemory::BackedMemory(const toml::table &table, MemoryStatistics *statistics) :
Memory("BackedMemory", table, parse_size(*table["size"].node()), statistics),
_page_size(parse_size(*table["page_size"].node()))
{
    this->memory.resize(parse_size(*table["size"].node()));
}

std::uint64_t 
BackedMemory::size() const {
    return this->memory.size();
}

bool 
BackedMemory::isBacked() const {
    return true;
}

void 
BackedMemory::access(MemoryRequest &request) {
    if (!check_access_range(request, 0, this->size() - 1)) {
        throw std::runtime_error("Access out of range!");
    }

    this->log_request(request);

    switch (request.type) {
        case MemoryRequestType::READ:
        request.data.resize(request.size); // make sure the data vector has enough space
        std::memcpy(request.data.data(), this->memory.data() + request.address, request.size); // copy data over
        this->statistics->add_read_write(request.size, 0UL);
        break;

        case MemoryRequestType::WRITE:
        std::memcpy(this->memory.data() + request.address, request.data.data(), request.size);
        this->statistics->add_read_write(0UL, request.size);
        break;

        default:
        throw std::invalid_argument("unkown memory request type");
    }
}

toml::table 
BackedMemory::to_toml() const {
    auto table = this->Memory::to_toml();
    table.emplace("size", absl::StrFormat("%sB", size_to_string(this->memory.size())));
    table.emplace("page_size", absl::StrFormat("%sB", size_to_string(this->_page_size)));
    return table;
}

void 
BackedMemory::save_to_disk(const std::filesystem::path &location) const {
    {
        std::ofstream config_file(location / "config.toml");
        auto config_table = this->to_toml();
        config_file << config_table << "\n";
    }


    // write out contents
    std::ofstream content_file(location / "contents.bin", std::ios::out | std::ios::binary);
    content_file.write((const char*)this->memory.data(), this->memory.size());

    // end
}

unique_memory_t 
BackedMemory::load_from_disk(const std::filesystem::path &location, const toml::table &table) {
    BackedMemory *memory = new BackedMemory(table, new MemoryStatistics());
    std::ifstream content_file(location / "contents.bin", std::ios::in | std::ios::binary);
    content_file.read((char *)memory->memory.data(), memory->size());
    
    return unique_memory_t(memory);
}

unique_memory_t 
BackedMemory::load_from_disk(const std::filesystem::path &location) {
    // std::ifstream config_file(location / "config.toml");
    // std::string line;
    // std::getline(config_file, line);
    // auto type = remove_comment(line);
    // if (type.find("BackedMemory") == std::string::npos) {
    //     throw std::runtime_error(absl::StrFormat("Incorrect type, expecting BackedMemory, got %s", type));
    // }
    // std::getline(config_file, line);
    // std::uint64_t size = std::stoul(remove_comment(line));
    toml::table table = toml::parse_file((location / "config.toml").string());

    return BackedMemory::load_from_disk(location, table);
    
}

bool 
BackedMemory::is_request_type_supported(MemoryRequestType type) const {
    switch (type)
    {
    case MemoryRequestType::READ:
    case MemoryRequestType::WRITE:
        return true;
    
    default:
        return false;
    }
}


unique_memory_t 
LinearScannedMemory::create(std::string_view name, uint64_t size, uint64_t page_size) {
    return unique_memory_t(new LinearScannedMemory(name, size, page_size, new MemoryStatistics()));
}

LinearScannedMemory::LinearScannedMemory(std::string_view name, std::uint64_t size, uint64_t page_size, MemoryStatistics *statistics) : 
Memory("LinearScannedMemory", name, size, statistics), _page_size(page_size),
conditional_memcpy_func(get_conditional_memcpy_function(this->_page_size))
{
    this->memory.resize(size);
}

LinearScannedMemory::LinearScannedMemory(const toml::table &table, MemoryStatistics *statistics) :
Memory("LinearScannedMemory", table, parse_size(*table["size"].node()), statistics),
_page_size(parse_size(*table["page_size"].node())),
conditional_memcpy_func(get_conditional_memcpy_function(this->_page_size))
{
    this->memory.resize(parse_size(*table["size"].node()));
}

std::uint64_t 
LinearScannedMemory::size() const {
    return this->memory.size();
}

bool 
LinearScannedMemory::isBacked() const {
    return true;
}

void 
LinearScannedMemory::fast_init() {
    uint64_t data_size;
    if (this->_page_size >= 8) {
        data_size = 8;
    } else if (this->_page_size >= 4) {
        data_size = 4;
    } else if (this->_page_size >= 2) {
        data_size = 2;
    } else {
        data_size = 1;
    }

    for (std::uint64_t i = 0; i < this->memory.size() / this->_page_size; i++) {
        if (i % 100000 == 0) {
            std::cout << absl::StrFormat("Writing Block %lu of %lu\n", i + 1, this->memory.size() / this->_page_size);
        }
        std::memcpy(this->memory.data() + i * this->_page_size, &i, data_size);
    }
}

void 
LinearScannedMemory::access(MemoryRequest &request) {
    if (!check_access_range(request, 0, this->size() - 1)) {
        throw std::runtime_error("Access out of range!");
    }

    // check for non page accesses
    if (request.size != this->_page_size || request.address % this->_page_size != 0) {
        throw std::runtime_error("Linear Scanned Memory Doesn't support unaligned accesses");
    }

    this->log_request(request);
    bytes_t buffer;

    switch (request.type) {
        case MemoryRequestType::READ:
        request.data.resize(request.size); // make sure the data vector has enough space
        for (uint64_t offset = 0; offset < this->memory.size(); offset += this->_page_size) {
            bool is_target = (offset == request.address);
            this->conditional_memcpy_func(is_target, request.data.data(), this->memory.data() + offset, this->_page_size);
        }
        this->statistics->add_read_write(request.size, 0UL);
        break;

        case MemoryRequestType::WRITE:
        for (uint64_t offset = 0; offset < this->memory.size(); offset += this->_page_size) {
            bool is_target = (offset == request.address);
            this->conditional_memcpy_func(is_target, this->memory.data() + offset, request.data.data(), this->_page_size);
        }
        // std::memcpy(this->memory.data() + request.address, request.data.data(), request.size);
        this->statistics->add_read_write(0UL, request.size);
        break;

        case MemoryRequestType::READ_WRITE:
        buffer.resize(this->_page_size);
        std::memcpy(buffer.data(), request.data.data(), this->_page_size);
         for (uint64_t offset = 0; offset < this->memory.size(); offset += this->_page_size) {
            bool is_target = (offset == request.address);
            this->conditional_memcpy_func(is_target, request.data.data(), this->memory.data() + offset, this->_page_size);
            this->conditional_memcpy_func(is_target, this->memory.data() + offset, buffer.data(), this->_page_size);
        }
        // std::memcpy(this->memory.data() + request.address, request.data.data(), request.size);
        this->statistics->add_read_write(request.size, request.size);
        break;

        default:
        throw std::invalid_argument("unkown memory request type");
    }
}

toml::table 
LinearScannedMemory::to_toml() const {
    auto table = this->Memory::to_toml();
    table.emplace("size", absl::StrFormat("%sB", size_to_string(this->memory.size())));
    table.emplace("page_size", absl::StrFormat("%sB", size_to_string(this->_page_size)));
    return table;
}

void 
LinearScannedMemory::save_to_disk(const std::filesystem::path &location) const {
    {
        std::ofstream config_file(location / "config.toml");
        auto config_table = this->to_toml();
        config_file << config_table << "\n";
    }


    // write out contents
    std::ofstream content_file(location / "contents.bin", std::ios::out | std::ios::binary);
    content_file.write((const char*)this->memory.data(), this->memory.size());

    // end
}

unique_memory_t 
LinearScannedMemory::load_from_disk(const std::filesystem::path &location, const toml::table &table) {
    LinearScannedMemory *memory = new LinearScannedMemory(table, new MemoryStatistics());
    std::ifstream content_file(location / "contents.bin", std::ios::in | std::ios::binary);
    content_file.read((char *)memory->memory.data(), memory->size());
    
    return unique_memory_t(memory);
}

unique_memory_t 
LinearScannedMemory::load_from_disk(const std::filesystem::path &location) {
    toml::table table = toml::parse_file((location / "config.toml").string());

    return LinearScannedMemory::load_from_disk(location, table);
    
}

bool 
LinearScannedMemory::is_request_type_supported(MemoryRequestType type) const {
    switch (type)
    {
    case MemoryRequestType::READ:
    case MemoryRequestType::WRITE:
    case MemoryRequestType::READ_WRITE:
        return true;
    
    default:
        return false;
    }
}

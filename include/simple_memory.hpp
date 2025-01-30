#pragma once

#include <memory_interface.hpp>
#include <stdint.h>
#include <memory>
#include <ostream>
#include <filesystem>
#include <toml++/toml.h>
#include <conditional_memcpy.hpp>

/**
 * @brief A Memory object backed by real memory
 * 
 */
class BackedMemory : public Memory{
    public:
        static unique_memory_t create(std::string_view name, uint64_t size, uint64_t page_size = 64);
    protected:
        BackedMemory(std::string_view name,  uint64_t size, uint64_t page_size, MemoryStatistics *statistics); // TODO: make private
        BackedMemory(uint64_t size): BackedMemory("UNKNOWN", size, 64, new MemoryStatistics()) {}; // TODO: remove!
        BackedMemory(const toml::table &table, MemoryStatistics *statistics);
    public:
        virtual uint64_t size() const override;
        virtual uint64_t page_size() const override {return this->_page_size;};
        virtual bool isBacked() const override;
        virtual void access(MemoryRequest &request) override;
        virtual bool is_request_type_supported(MemoryRequestType type) const override;
        virtual toml::table to_toml() const override;
        virtual void save_to_disk(const std::filesystem::path &location) const override;

        static unique_memory_t load_from_disk(const std::filesystem::path &location);
        static unique_memory_t load_from_disk(const std::filesystem::path &location, const toml::table &table);
        static unique_memory_t load_from_toml(const toml::table &table);
    
    private:
        const uint64_t _page_size; 
        std::vector<uint8_t> memory;
};

// /**
//  * @brief A Memory object not backed by anything
//  * 
//  * does pretty much nothing.
//  * 
//  */
// class UnbackedMemory : public Memory {
//     public:
//         UnbackedMemory(uint64_t size);
//         virtual uint64_t size() const override;
//         virtual bool isBacked() const override;
//         virtual void access(MemoryRequest &request) override;
//         virtual bool is_request_type_supported(MemoryRequestType type) const override;
    
//     private:
//         const uint64_t memorySize;
// };


// struct SerializedAccessLogger {
//     SerializedMemoryHeader header;
//     uint32_t 
// }


class LinearScannedMemory : public Memory{
    public:
        static unique_memory_t create(std::string_view name, uint64_t size, uint64_t page_size = 64);
    protected:
        LinearScannedMemory(std::string_view name,  uint64_t size, uint64_t page_size, MemoryStatistics *statistics); // TODO: make private
        LinearScannedMemory(const toml::table &table, MemoryStatistics *statistics);
    public:
        virtual uint64_t size() const override;
        virtual uint64_t page_size() const override {return this->_page_size;};
        virtual bool isBacked() const override;
        virtual void access(MemoryRequest &request) override;
        virtual bool is_request_type_supported(MemoryRequestType type) const override;
        virtual toml::table to_toml() const override;
        virtual void save_to_disk(const std::filesystem::path &location) const override;
        virtual void fast_init();

        static unique_memory_t load_from_disk(const std::filesystem::path &location);
        static unique_memory_t load_from_disk(const std::filesystem::path &location, const toml::table &table);
        static unique_memory_t load_from_toml(const toml::table &table);
    
    private:
        const uint64_t _page_size; 
        std::vector<uint8_t> memory;
        const conditional_memcpy_signature conditional_memcpy_func;
};

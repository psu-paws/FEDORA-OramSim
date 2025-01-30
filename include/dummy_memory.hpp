#pragma once
#include <memory_interface.hpp>
#include <util.hpp>

class DummyMemory final: public Memory{
    public:
        static unique_memory_t create(std::string_view name, std::uint64_t size, std::uint64_t page_size = 64) {
            return unique_memory_t(
                new DummyMemory(name, size, page_size)
            );
        }

        static unique_memory_t create(const toml::table &table) {
            return unique_memory_t(
                new DummyMemory(table)
            );
        }
    protected:
        DummyMemory(std::string_view name, std::uint64_t size, std::uint64_t page_size = 64) :
        Memory("DummyMemory", name, size, new MemoryStatistics),
        _size(size),
        _page_size(page_size)
        {}

        // TODO: this is just here so we can do this change in stages, remove!
        // Memory(): type("UNKNOWN"), name("UNKNOWN"), statistics(new MemoryStatistics()), request_logger("UNKNOWN", 0) {};
        DummyMemory(const toml::table &table) :
        Memory("DummyMemory", table, parse_size(table["size"]), new MemoryStatistics),
        _size(parse_size(table["size"])),
        _page_size(parse_size(table["page_size"]))
        {}

    public:
        virtual ~DummyMemory() = default;

        /**
         * @brief Return the size of the memory
         * 
         * @return uint64_t size in bytes
         */
        virtual uint64_t size() const {
            return this->_size;
        }

        /**
         * @brief Is this memory backed by physical storage
         * 
         * @return true memory is backed and writes will be reflected in future reads
         * @return false memory is not backed, writes are lost
         */
        virtual bool isBacked() const {
            // nothing actually checks this
            return false;
        };

        /**
         * @brief Perform access on memory
         * 
         * @param request memory request
         */
        virtual void access(MemoryRequest &request) {
            if (request.type != MemoryRequestType::WRITE) {
                throw std::runtime_error("Attempted to read from dummy memory");
            }
        }

        /**
         * @brief Return a toml table that can be used to reconstruct the memory without any data.
         * It is recommended to start with the result of this function of the base class and add values
         * needed by the derived class 
         * 
         * @return toml::table 
         */
        virtual toml::table to_toml() const { 
            auto table = this->Memory::to_toml();
            table.emplace("size", size_to_string(this->_size));
            table.emplace("page_size", size_to_string(this->_page_size));

            return table;
        };

        /**
         * @brief Tell this memory and any children to stop logging
         * 
         */
        virtual void stop_logging() {
            this->save_statistics();
            this->request_logger.stop_logging();
        };

        /**
         * @brief Queries if the given request type is supported by this memory
         * 
         * @param type request type
         * @return true request type is supported
         * @return false request type is not supported
         */
        virtual bool is_request_type_supported(MemoryRequestType type) const {
            return type == MemoryRequestType::WRITE;
        }

        virtual uint64_t page_size() const {
            return this->_page_size;
        }
    
    protected:
        const std::uint64_t _size;
        const std::uint64_t _page_size;
        
};
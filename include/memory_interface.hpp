#pragma once

#include <vector>
#include <stdint.h>
#include <memory>
#include <unordered_map>
#include <ostream>
#include <filesystem>
#include <statistics.hpp>
#include <request_logger.hpp>
#include <absl/strings/str_format.h>
#include <fstream>
#include <toml++/toml.h>
#include <memory_defs.hpp>
#include <util.hpp>

class Memory {
    protected:
        Memory(std::string_view type, std::string_view name, uint64_t size, MemoryStatistics *statistics) : type(type), name(name), statistics(statistics), request_logger(name, size){};

        // TODO: this is just here so we can do this change in stages, remove!
        // Memory(): type("UNKNOWN"), name("UNKNOWN"), statistics(new MemoryStatistics()), request_logger("UNKNOWN", 0) {};

        Memory(std::string_view type, const toml::table &table, uint64_t size, MemoryStatistics *statistics) : 
        type(table["type"].value<std::string_view>().value()), 
        name(table["name"].value<std::string_view>().value()),
        statistics(statistics),
        request_logger(name, size)
        {

            if (this->type != type) {
                throw std::invalid_argument(absl::StrFormat("Expecting type \"%s\" but got \"%s\" from table!", type, this->type));
            }
        };

    public:
        virtual ~Memory() = default;

        /**
         * @brief Perform any needed initializations in here
         * 
         * 
         */
        virtual void init() {};

        /**
         * @brief Return the size of the memory
         * 
         * @return uint64_t size in bytes
         */
        virtual uint64_t size() const = 0;

        /**
         * @brief Is this memory backed by physical storage
         * 
         * @return true memory is backed and writes will be reflected in future reads
         * @return false memory is not backed, writes are lost
         */
        virtual bool isBacked() const = 0;

        /**
         * @brief Perform access on memory
         * 
         * @param request memory request
         */
        virtual void access(MemoryRequest &request) = 0;

        /**
         * @brief Return a toml table that can be used to reconstruct the memory without any data.
         * It is recommended to start with the result of this function of the base class and add values
         * needed by the derived class 
         * 
         * @return toml::table 
         */
        virtual toml::table to_toml() const { 
            return toml::table {
                {"type", this->type},
                {"name", this->name}
            };
        };

        /**
         * @brief Save this memory and any children in the directory specified by location.
         * This function can assume that the directory specified by location exists.
         * 
         * @param location the directory to save the memory
         */
        virtual void save_to_disk(const std::filesystem::path &location) const {};

        /**
         * @brief Tell this memory and any children to start logging
         * 
         * @param append open log file in append mode
         */
        virtual void start_logging(bool append = false) {
            this->reset_statistics(append);
            this->request_logger.start_logging(append);
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
        virtual bool is_request_type_supported(MemoryRequestType type) const = 0;

        /**
         * @brief Issues a barrier to this memory
         * 
         * Any requests issued before the barrier must be completed before any request that comes after.
         * 
         */
        virtual void barrier(){
            this->request_logger.barrier();
        };

        virtual void batch_access(std::vector<MemoryRequest> &requests) {
            for (auto &request : requests) {
                this->access(request);
            }
        }

        virtual uint64_t page_size() const = 0;

        virtual void reset_statistics(bool from_file = false) {
            this->statistics->clear();
            if (from_file) {
                std::filesystem::path stat_file_path(absl::StrFormat("%s_stat.toml", this->name));
                if (std::filesystem::is_regular_file(stat_file_path)){
                    auto table = toml::parse_file(stat_file_path.string());
                    this->statistics->from_toml(table);
                }
            }
        }

        virtual void save_statistics() {
            std::filesystem::path stat_file_path(absl::StrFormat("%s_stat.toml", this->name));
            std::ofstream stat_file(stat_file_path, std::ios::out);
            stat_file << this->statistics->to_toml() << "\n";
        }

    protected:
        void log_request(const MemoryRequest &request) {
            this->statistics->record_request(request);
            this->request_logger.log_request(request);
        }
    
    protected:
        const std::string type;
        const std::string name;

        std::unique_ptr<MemoryStatistics> statistics;
        RequestLogger request_logger;
        
};

typedef std::unordered_map<std::string, std::shared_ptr<Memory>> memory_directory_t;
typedef std::unique_ptr<Memory> unique_memory_t;
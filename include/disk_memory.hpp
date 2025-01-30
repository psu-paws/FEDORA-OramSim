#pragma once

#include <memory_interface.hpp>
#include <filesystem>
#include <libaio.h>

class DiskMemory : public Memory{
    public:
        static unique_memory_t create(std::string_view name,uint64_t size);
        static unique_memory_t create(std::string_view name, std::filesystem::path data_file);
        DiskMemory(std::string_view name,std::filesystem::path file_location, int fd, uint64_t size, uint64_t fs_block_size);
        ~DiskMemory();
        virtual uint64_t size() const override;
        virtual bool isBacked() const override;
        virtual void access(MemoryRequest &request) override;
        virtual void batch_access(std::vector<MemoryRequest> &requests) override;
        virtual bool is_request_type_supported(MemoryRequestType type) const override;
        virtual uint64_t page_size() const override;
        virtual toml::table to_toml() const override;
        virtual void save_to_disk(const std::filesystem::path &location) const override;

        static unique_memory_t load_from_disk(const std::filesystem::path &location);
        static unique_memory_t load_from_disk(const std::filesystem::path &location, const toml::table &table);
    private:
        static unique_memory_t create_second_stage(std::string_view name, std::filesystem::path temp_file_path);

    private:
        const std::filesystem::path file_location;  /*!< location of temporary file */
        const int fd;  /*!< file descriptor of temporary file */
        const uint64_t file_size;  /*!< file size temporary file */
        const uint64_t fs_block_size;  /*!< filesystem block size */
};

class BlockDiskMemory : public Memory{
    public:
        static unique_memory_t create(std::string_view name, uint64_t size);
        static unique_memory_t create(std::string_view name, std::filesystem::path data_file);
        BlockDiskMemory(std::string_view name,std::filesystem::path file_location, int fd, uint64_t size, uint64_t fs_block_size);
        ~BlockDiskMemory();
        virtual uint64_t size() const override;
        virtual bool isBacked() const override;
        virtual void access(MemoryRequest &request) override;
        virtual void batch_access(std::vector<MemoryRequest> &requests) override;
        virtual bool is_request_type_supported(MemoryRequestType type) const override;
        virtual uint64_t page_size() const override;
        virtual toml::table to_toml() const override;
        virtual void save_to_disk(const std::filesystem::path &location) const override;

        static unique_memory_t load_from_disk(const std::filesystem::path &location);
        static unique_memory_t load_from_disk(const std::filesystem::path &location, const toml::table &table);
    private:
        static unique_memory_t create_second_stage(std::string_view name, std::filesystem::path temp_file_path);

    private:
        const std::filesystem::path file_location;
        const int fd;
        const uint64_t file_size;
        const uint64_t fs_block_size;
};

// template <typename D>
// concept BlockDiskBackEnd = requires
// (D disk_back_end, MemoryRequestSpan &requests) { disk_back_end.execute_accesses(requests);} &&
// std::is_default_constructible_v<D>() && std::is_nothrow_destructible_v<D>();

// class BlockDiskSynchonousBackEnd {
//     private:

// };

class BlockDiskMemoryLibAIO : public Memory{
    public:
        [[nodiscard]] static unique_memory_t create(std::string_view name,uint64_t size, std::optional<uint64_t> page_size = {});
        [[nodiscard]] static unique_memory_t create(std::string_view name, std::filesystem::path data_file, std::optional<uint64_t> page_size = {});
        ~BlockDiskMemoryLibAIO() noexcept;
        [[nodiscard]] virtual uint64_t size() const noexcept override;
        [[nodiscard]] virtual bool isBacked() const noexcept override;
        virtual void access(MemoryRequest &request) override;
        virtual void batch_access(std::vector<MemoryRequest> &requests) override;
        virtual void barrier() override;
        [[nodiscard]] virtual bool is_request_type_supported(MemoryRequestType type) const noexcept override;
        [[nodiscard]] virtual uint64_t page_size() const noexcept override;
        [[nodiscard]] virtual toml::table to_toml() const noexcept override;
        virtual void save_to_disk(const std::filesystem::path &location) const override;

        [[nodiscard]] static unique_memory_t load_from_disk(const std::filesystem::path &location);
        [[nodiscard]] static unique_memory_t load_from_disk(const std::filesystem::path &location, const toml::table &table);
    private:
        BlockDiskMemoryLibAIO(
            std::string_view name,
            std::filesystem::path file_location,
            int fd,
            uint64_t size,
            uint64_t fs_block_size,
            uint64_t page_size,
            io_context_t io_context
        );
        static unique_memory_t create_second_stage(std::string_view name, std::filesystem::path temp_file_path, std::optional<uint64_t> page_size = {});
        

    private:
        const std::filesystem::path file_location;
        const io_context_t io_context;
        const int fd;
        const uint64_t file_size;
        const uint64_t _page_size;
        const uint64_t fs_block_size;

        uint64_t allocated_buffer_size;
        std::vector<struct iocb> io_control_blocks;
        std::vector<struct iocb*> io_control_block_pointers;
        char* buffer;
        std::vector<struct io_event> io_events;
};

class BlockDiskMemoryLibAIOCached : public Memory{
    public:
        [[nodiscard]] static unique_memory_t create(std::string_view name,uint64_t size, uint64_t cache_size, std::optional<uint64_t> page_size = {});
        [[nodiscard]] static unique_memory_t create(std::string_view name, std::filesystem::path data_file, uint64_t cache_size, std::optional<uint64_t> page_size = {});
        ~BlockDiskMemoryLibAIOCached() noexcept;
        [[nodiscard]] virtual uint64_t size() const noexcept override;
        [[nodiscard]] virtual bool isBacked() const noexcept override;
        virtual void access(MemoryRequest &request) override;
        virtual void batch_access(std::vector<MemoryRequest> &requests) override;
        virtual void barrier() override;
        [[nodiscard]] virtual bool is_request_type_supported(MemoryRequestType type) const noexcept override;
        [[nodiscard]] virtual uint64_t page_size() const noexcept override;
        [[nodiscard]] virtual toml::table to_toml() const noexcept override;
        virtual void save_to_disk(const std::filesystem::path &location) const override;

        [[nodiscard]] static unique_memory_t load_from_disk(const std::filesystem::path &location);
        [[nodiscard]] static unique_memory_t load_from_disk(const std::filesystem::path &location, const toml::table &table);
    private:
        BlockDiskMemoryLibAIOCached(
            std::string_view name,
            std::filesystem::path file_location,
            int fd,
            uint64_t size,
            uint64_t fs_block_size,
            uint64_t page_size,
            io_context_t io_context,
            bytes_t &&cache
        );
        static unique_memory_t create_second_stage(std::string_view name, std::filesystem::path temp_file_path, uint64_t cache_size, std::optional<uint64_t> page_size = {});
        

    private:
        const std::filesystem::path file_location;
        const io_context_t io_context;
        const int fd;
        const uint64_t file_size;
        // const uint64_t cache_size;
        const uint64_t _page_size;
        const uint64_t fs_block_size;

        uint64_t allocated_buffer_size;
        std::vector<struct iocb> io_control_blocks;
        std::vector<struct iocb*> io_control_block_pointers;
        char* buffer;
        std::vector<struct io_event> io_events;
        bytes_t cache;
};

void set_disk_memory_temp_file_directory(const std::filesystem::path path);
void set_additional_cache_amount(uint64_t cache);
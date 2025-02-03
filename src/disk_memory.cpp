#include <disk_memory.hpp>
#include <util.hpp>
#include <filesystem>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <absl/strings/str_format.h>
#include <iostream>
#include <fstream>
#include <request_coalescer.hpp>
#include <string.h>

#include <sys/types.h>
#include <aio.h>

constexpr int file_mode = O_DIRECT | O_RDWR;

std::filesystem::path disk_memory_temp_file_directory;

void set_disk_memory_temp_file_directory(const std::filesystem::path path) {
    disk_memory_temp_file_directory = path;
}

uint64_t additional_cache = 0;

void set_additional_cache_amount(uint64_t cache) {
    additional_cache = cache;
}

static std::filesystem::path generate_temp_file_path() {
    std::filesystem::path temp_file_path;
    do {
        // keep generating new file names until we find one that isn't being used
        // this shouldn't take too long.
        temp_file_path = disk_memory_temp_file_directory / std::filesystem::path(
            absl::StrFormat(
                "%c%c%c%c%c%c%c%c.tmp",
                generate_random_character(),
                generate_random_character(),
                generate_random_character(),
                generate_random_character(),
                generate_random_character(),
                generate_random_character(),
                generate_random_character(),
                generate_random_character()
            )
        );
    } while (std::filesystem::exists(temp_file_path));
    
    return temp_file_path;
}

unique_memory_t 
DiskMemory::create(std::string_view name,uint64_t size) {
    std::filesystem::path temp_file_path = generate_temp_file_path();

    // create the temp file and fill with zeros
    int temp_file_fd = open(temp_file_path.c_str(), O_CREAT | O_RDWR, 0644);
    
    if (fallocate(temp_file_fd, FALLOC_FL_ZERO_RANGE, 0, size)) {
        throw std::runtime_error(absl::StrFormat("Fallocate failed with errno %d: %s", errno, strerror(errno)));
    }

    close(temp_file_fd);

    return DiskMemory::create_second_stage(name, temp_file_path);
}

unique_memory_t 
DiskMemory::create(std::string_view name, std::filesystem::path data_file) {
    std::filesystem::path temp_file_path = generate_temp_file_path();

    std::filesystem::copy_file(data_file, temp_file_path);

    return DiskMemory::create_second_stage(name, temp_file_path);
}

unique_memory_t 
DiskMemory::create_second_stage(std::string_view name, std::filesystem::path temp_file_path) {
    int fd = open(temp_file_path.c_str(), file_mode);

    // get some stats of the file
    struct stat file_stat;
    fstat(fd, &file_stat);
    
    std::cout << absl::StrFormat("File of size %lu\n", file_stat.st_size);
    std::cout << absl::StrFormat("File system block size %lu\n", file_stat.st_blksize);

    return unique_memory_t(new DiskMemory(name, temp_file_path, fd, file_stat.st_size, file_stat.st_blksize));
}

DiskMemory::DiskMemory(std::string_view name,std::filesystem::path file_location, int fd, uint64_t size, uint64_t fs_block_size) :
Memory("DiskMemory", name, size, new MemoryStatistics),
file_location(file_location), fd(fd), file_size(size), fs_block_size(fs_block_size)
{
    
}

DiskMemory::~DiskMemory() {
    close(this->fd);
    std::filesystem::remove(this->file_location);
}

uint64_t 
DiskMemory::size() const {
    return this->file_size;
}

bool 
DiskMemory::isBacked() const {
    return true;
}

void 
DiskMemory::access(MemoryRequest &request) {
    std::vector<MemoryRequest> requests;
    requests.emplace_back(request);
    this->batch_access(requests);
    request = requests.front();
}

void 
DiskMemory::batch_access(std::vector<MemoryRequest> &requests) {
    RequestCoalescer read_coalescer(this->fs_block_size);
    RequestCoalescer write_coalescer(this->fs_block_size);

    // coalesce read and write requests separately.
    for (uint64_t i = 0; i < requests.size(); i++) {
        this->Memory::log_request(requests[i]);
        if (requests[i].type == MemoryRequestType::READ) {
            read_coalescer.add_request(i, requests[i]);
        } else {
            write_coalescer.add_request(i, requests[i]);
        }
    }

    const BlockRequestMap &read_block_request_map = read_coalescer.get_map_ref();
    const BlockRequestMap &write_block_request_map = write_coalescer.get_map_ref();

    // set up async io for the batch
    std::vector<aiocb> read_aio_control_blocks;
    std::vector<aiocb> write_aio_control_blocks;

    unsigned char *read_buffers = (unsigned char *)aligned_alloc(this->fs_block_size, read_block_request_map.size() * this->fs_block_size);
    memset(read_buffers, 0, read_block_request_map.size() * this->fs_block_size);
    unsigned char *write_buffers = (unsigned char *)aligned_alloc(this->fs_block_size, write_block_request_map.size() * this->fs_block_size);
    memset(write_buffers, 0, write_block_request_map.size() * this->fs_block_size);

    uint64_t read_buffers_offset = 0;
    for (const auto &read_pair : read_block_request_map) {
        read_aio_control_blocks.emplace_back();
        read_aio_control_blocks.back().aio_fildes = this->fd;   // file descripter
        read_aio_control_blocks.back().aio_buf = read_buffers + read_buffers_offset; // buffer 
        read_aio_control_blocks.back().aio_nbytes = this->fs_block_size; // always read one block
        read_aio_control_blocks.back().aio_offset = this->fs_block_size * read_pair.first; // offset into the file
        read_aio_control_blocks.back().aio_lio_opcode = LIO_READ;

        // issue the request
        // if (aio_read(&(read_aio_control_blocks.back())) == -1) {
        //     throw std::runtime_error("Can not create async read request");
        // }

        read_buffers_offset += this->fs_block_size; // increment buffer
    }

    // we need to a read-modify-write since we don't know if the block is completely overwritten or not
    uint64_t write_buffers_offset = 0;
    for (const auto &write_pair : write_block_request_map) {
        unsigned char *write_buffer = write_buffers + write_buffers_offset;
        write_aio_control_blocks.emplace_back();
        write_aio_control_blocks.back().aio_fildes = this->fd;   // file descripter
        write_aio_control_blocks.back().aio_buf = write_buffer; // buffer 
        write_aio_control_blocks.back().aio_nbytes = this->fs_block_size; // always read one block
        write_aio_control_blocks.back().aio_offset = this->fs_block_size * write_pair.first; // offset into the file
        write_aio_control_blocks.back().aio_lio_opcode = LIO_READ;

        write_buffers_offset += this->fs_block_size; // increment buffer
    }

    // std::cout << absl::StrFormat("Issued %lu reads and %lu writes.\n", read_aio_control_blocks.size(), write_aio_control_blocks.size());
    std::vector<aiocb*> aiocb_pointers;

    for (auto &read_aio_control_block: read_aio_control_blocks) {
        aiocb_pointers.emplace_back(&read_aio_control_block);
    }

    for (auto &write_aio_control_block: write_aio_control_blocks) {
        aiocb_pointers.emplace_back(&write_aio_control_block);
    }

    lio_listio(LIO_WAIT, aiocb_pointers.data(), aiocb_pointers.size(), nullptr);
    
    if (write_block_request_map.size() > 0) {

        write_buffers_offset = 0;
        for (const auto &write_pair : write_block_request_map) {
            unsigned char *write_buffer = write_buffers + write_buffers_offset;
            for (const auto &block_request_entry : write_pair.second) {
                const MemoryRequest &request = requests[block_request_entry.request_id];
                memcpy(write_buffer + block_request_entry.block_offset, request.data.data() + block_request_entry.request_offset, block_request_entry.size);
            }
            write_buffers_offset += this->fs_block_size;
        }

        for (auto &aiocb : write_aio_control_blocks) {
            aiocb.aio_lio_opcode = LIO_WRITE;
        }

        // execute the write
        lio_listio(LIO_WAIT, aiocb_pointers.data() + read_aio_control_blocks.size(), aiocb_pointers.size() - read_aio_control_blocks.size(), nullptr);
        fdatasync(this->fd);
    }

    // now we will finish the read requests
    read_buffers_offset = 0UL;
    for (const auto &read_pair : read_block_request_map) {
        unsigned char *read_buffer = read_buffers + read_buffers_offset;
        for (const auto &block_request_entry : read_pair.second) {
            MemoryRequest &request = requests[block_request_entry.request_id];
            memcpy(request.data.data() + block_request_entry.request_offset, read_buffer + block_request_entry.block_offset, block_request_entry.size);
        }
        read_buffers_offset += this->fs_block_size;
    }

    this->statistics->add_read_write(
        (read_aio_control_blocks.size() + write_aio_control_blocks.size())* this->fs_block_size,
        write_aio_control_blocks.size() * this->fs_block_size
    );

    // clean up
    free(read_buffers);
    free(write_buffers);
}

bool 
DiskMemory::is_request_type_supported(MemoryRequestType type) const {
    switch (type)
    {
    case MemoryRequestType::READ:
    case MemoryRequestType::WRITE:
        return true;
    
    default:
        return false;
    }
}

uint64_t 
DiskMemory::page_size() const {
    return this->fs_block_size;
}

toml::table 
DiskMemory::to_toml() const {
    auto table = this->Memory::to_toml();
    table.emplace("size", absl::StrFormat("%sB", size_to_string(this->size())));
    return table;
}

void 
DiskMemory::save_to_disk(const std::filesystem::path &location) const {
    std::ofstream config_file(location / "config.toml");
    config_file << this->to_toml() << "\n";
    
    // copy file to location
    std::filesystem::copy_file(this->file_location, location / "contents.bin");
}

unique_memory_t 
DiskMemory::load_from_disk(const std::filesystem::path &location, const toml::table &table) {
    // uint64_t size = parse_size(*table["size"].node());
    std::string_view name = table["name"].value<std::string_view>().value();
    return DiskMemory::create(name, location / "contents.bin");
}

unique_memory_t 
DiskMemory::load_from_disk(const std::filesystem::path &location) {
    auto table = toml::parse_file((location / "config.txt").string());
    return DiskMemory::load_from_disk(location, table);
}

unique_memory_t 
BlockDiskMemory::create(std::string_view name,uint64_t size) {
    std::filesystem::path temp_file_path = generate_temp_file_path();

    // create the temp file and fill with zeros
    int temp_file_fd = open(temp_file_path.c_str(), O_CREAT | O_RDWR, 0644);

    if (fallocate(temp_file_fd, FALLOC_FL_ZERO_RANGE, 0, size)) {
        throw std::runtime_error(absl::StrFormat("Fallocate failed with errno %d: %s", errno, strerror(errno)));
    }

    close(temp_file_fd);

    return BlockDiskMemory::create_second_stage(name, temp_file_path);
}

unique_memory_t 
BlockDiskMemory::create(std::string_view name, std::filesystem::path data_file) {
    std::filesystem::path temp_file_path = generate_temp_file_path();

    std::filesystem::copy_file(data_file, temp_file_path);

    return BlockDiskMemory::create_second_stage(name, temp_file_path);
}

unique_memory_t 
BlockDiskMemory::create_second_stage(std::string_view name, std::filesystem::path temp_file_path) {
    int fd = open(temp_file_path.c_str(), file_mode);

    // get some stats of the file
    struct stat file_stat;
    fstat(fd, &file_stat);
    
    std::cout << absl::StrFormat("File of size %lu\n", file_stat.st_size);
    std::cout << absl::StrFormat("File system block size %lu\n", file_stat.st_blksize);

    return unique_memory_t(new BlockDiskMemory(name, temp_file_path, fd, file_stat.st_size, file_stat.st_blksize));
}

BlockDiskMemory::BlockDiskMemory(std::string_view name,std::filesystem::path file_location, int fd, uint64_t size, uint64_t fs_block_size) :
Memory("BlockDiskMemory", name, size, new MemoryStatistics),
file_location(file_location), fd(fd), file_size(size), fs_block_size(fs_block_size)
{
    
}

BlockDiskMemory::~BlockDiskMemory() {
    close(this->fd);
    std::filesystem::remove(this->file_location);
}

uint64_t 
BlockDiskMemory::size() const {
    return this->file_size;
}

bool 
BlockDiskMemory::isBacked() const {
    return true;
}

void 
BlockDiskMemory::access(MemoryRequest &request) {
    std::vector<MemoryRequest> requests;
    requests.emplace_back(request);
    this->batch_access(requests);
    request = requests.front();
}

void 
BlockDiskMemory::batch_access(std::vector<MemoryRequest> &requests) {

    // check that all requests are page-aligned
    for (const auto &request : requests) {
        if (request.address % this->fs_block_size != 0 || request.size != this->fs_block_size) {
            throw std::runtime_error("BlockDiskMemory only supports page_aligned accesses");
        }
        this->Memory::log_request(request);
    }

    // set up async io for the batch
    std::vector<aiocb> aio_control_blocks;

    unsigned char *request_buffers = (unsigned char *)aligned_alloc(this->fs_block_size, requests.size() * this->fs_block_size);
    memset(request_buffers, 0, requests.size() * this->fs_block_size);

    uint64_t buffer_offset = 0;
    uint64_t read_page_count = 0;
    uint64_t write_page_count = 0;
    for (auto &request : requests) {
        aio_control_blocks.emplace_back();
        aio_control_blocks.back().aio_fildes = this->fd;   // file descripter
        aio_control_blocks.back().aio_buf = request_buffers + buffer_offset; // buffer 
        aio_control_blocks.back().aio_nbytes = this->fs_block_size; // always read one block
        aio_control_blocks.back().aio_offset = request.address; // offset into the file
        aio_control_blocks.back().aio_lio_opcode = request.type == MemoryRequestType::READ ? LIO_READ : LIO_WRITE;

        if (request.type != MemoryRequestType::READ) {
            // copy write requests into the buffer
            memcpy(request_buffers + buffer_offset, request.data.data(), this->fs_block_size);
            write_page_count++;
        } else {
            read_page_count++;
        }

        buffer_offset += this->fs_block_size; // increment buffer
    }

    std::vector<aiocb*> aiocb_pointers;

    for (auto &aio_control_block: aio_control_blocks) {
        aiocb_pointers.emplace_back(&aio_control_block);
    }

    lio_listio(LIO_WAIT, aiocb_pointers.data(), aiocb_pointers.size(), nullptr);

    // now we will finish the read requests
    buffer_offset = 0UL;
    for (auto &request : requests) {
        if (request.type == MemoryRequestType::READ) {
            unsigned char *buffer = request_buffers + buffer_offset;
            memcpy(request.data.data(), buffer, this->fs_block_size);
        }
        buffer_offset += this->fs_block_size;
    }

    this->statistics->add_read_write(
        read_page_count * this->fs_block_size,
        write_page_count * this->fs_block_size
    );

    fdatasync(this->fd);

    // clean up
    free(request_buffers);
}

bool 
BlockDiskMemory::is_request_type_supported(MemoryRequestType type) const {
    switch (type)
    {
    case MemoryRequestType::READ:
    case MemoryRequestType::WRITE:
        return true;
    
    default:
        return false;
    }
}

uint64_t 
BlockDiskMemory::page_size() const {
    return this->fs_block_size;
}

toml::table 
BlockDiskMemory::to_toml() const {
    auto table = this->Memory::to_toml();
    table.emplace("size", absl::StrFormat("%sB", size_to_string(this->size())));
    return table;
}

void 
BlockDiskMemory::save_to_disk(const std::filesystem::path &location) const {
    std::ofstream config_file(location / "config.toml");
    config_file << this->to_toml() << "\n";
    
    // copy file to location
    std::filesystem::copy_file(this->file_location, location / "contents.bin");
}

unique_memory_t 
BlockDiskMemory::load_from_disk(const std::filesystem::path &location, const toml::table &table) {
    // uint64_t size = parse_size(*table["size"].node());
    std::string_view name = table["name"].value<std::string_view>().value();
    return BlockDiskMemory::create(name, location / "contents.bin");
}

unique_memory_t 
BlockDiskMemory::load_from_disk(const std::filesystem::path &location) {
    auto table = toml::parse_file((location / "config.txt").string());
    return BlockDiskMemory::load_from_disk(location, table);
}


unique_memory_t 
BlockDiskMemoryLibAIO::create(std::string_view name, uint64_t size, std::optional<uint64_t> page_size) {
    std::filesystem::path temp_file_path = generate_temp_file_path();

    // create the temp file and fill with zeros
    int temp_file_fd = open(temp_file_path.c_str(), O_CREAT | O_RDWR, 0644);

    if (fallocate(temp_file_fd, FALLOC_FL_ZERO_RANGE, 0, size)) {
        throw std::runtime_error(absl::StrFormat("Fallocate failed with errno %d: %s", errno, strerror(errno)));
    }

    close(temp_file_fd);

    return BlockDiskMemoryLibAIO::create_second_stage(name, temp_file_path, page_size);
}

unique_memory_t 
BlockDiskMemoryLibAIO::create(std::string_view name, std::filesystem::path data_file, std::optional<uint64_t> page_size) {
    std::filesystem::path temp_file_path = generate_temp_file_path();

    std::filesystem::copy_file(data_file, temp_file_path);

    return BlockDiskMemoryLibAIO::create_second_stage(name, temp_file_path, page_size);
}

unique_memory_t 
BlockDiskMemoryLibAIO::create_second_stage(std::string_view name, std::filesystem::path temp_file_path, std::optional<uint64_t> page_size) {
    int fd = open(temp_file_path.c_str(), file_mode);

    assert(fd > 0);

    // get some stats of the file
    struct stat file_stat;
    fstat(fd, &file_stat);
    
    std::cout << "BlockMemoryLibAIO\n";
    std::cout << absl::StrFormat("File of size %lu\n", file_stat.st_size);
    std::cout << absl::StrFormat("File system block size %lu\n", file_stat.st_blksize);

    // create the io_context
    constexpr int max_events = 128;
    io_context_t io_context = nullptr;
    int ret_value = io_setup(max_events, &io_context);
     if (ret_value < 0 ){
        throw std::runtime_error(absl::StrFormat("io_setup failed with code %d: %s: %s", -ret_value, strerrorname_np(-ret_value), strerrordesc_np(-ret_value)));
    }

    uint64_t unwrapped_page_size = page_size.value_or(file_stat.st_blksize);

    if (unwrapped_page_size < static_cast<uint64_t>(file_stat.st_blksize) || unwrapped_page_size % static_cast<uint64_t>(file_stat.st_blksize) != 0) {
        throw std::runtime_error(absl::StrFormat("Page size %lu is not an integer multiple of File system block size %lu!", unwrapped_page_size, file_stat.st_blksize));
    }

    return unique_memory_t(
        new BlockDiskMemoryLibAIO(
            name, 
            temp_file_path, 
            fd, 
            file_stat.st_size,
            unwrapped_page_size,
            file_stat.st_blksize, 
            io_context
        )
    );
}

BlockDiskMemoryLibAIO::BlockDiskMemoryLibAIO(
    std::string_view name,
    std::filesystem::path file_location,
    int fd,
    uint64_t size,
    uint64_t page_size,
    uint64_t fs_block_size,
    io_context_t io_context
):
Memory("BlockDiskMemoryLibAIO", name, size, new MemoryStatistics),
file_location(file_location),
io_context(io_context),
fd(fd),
file_size(size),
_page_size(page_size),
fs_block_size(fs_block_size),
allocated_buffer_size(0),
buffer(nullptr)
{}

BlockDiskMemoryLibAIO::~BlockDiskMemoryLibAIO() noexcept {
    io_destroy(this->io_context);
    close(this->fd);
    std::free(this->buffer);
    try{
        std::filesystem::remove(this->file_location);
    } catch (...){
        std::cout << "An exception occurred while trying to delete temp data file!\n";
    }
}

uint64_t 
BlockDiskMemoryLibAIO::size() const noexcept {
    return this->file_size;
}

bool 
BlockDiskMemoryLibAIO::isBacked() const noexcept {
    return true;
}

void 
BlockDiskMemoryLibAIO::access(MemoryRequest &request) {
    std::vector<MemoryRequest> requests;
    requests.emplace_back(request);
    this->batch_access(requests);
    request = requests.front();
}

void 
BlockDiskMemoryLibAIO::batch_access(std::vector<MemoryRequest> &requests) {

    // check that all requests are page-aligned
    for (const auto &request : requests) {
        if (request.type != MemoryRequestType::READ && request.type != MemoryRequestType::WRITE) {
            throw std::runtime_error("BlockDiskMemoryLibAIO only supports READ and WRITE operations");
        }
        if (request.address % this->_page_size != 0 || request.size != this->_page_size) {
            throw std::runtime_error("BlockDiskMemoryLibAIO only supports page_aligned accesses");
        }
        this->Memory::log_request(request);
    }

    // set up async io for the batch
    // std::vector<aiocb> aio_control_blocks;

    if (requests.size() > this->allocated_buffer_size) {
        this->io_control_blocks.resize(requests.size());
        this->io_control_block_pointers.resize(requests.size());
        this->io_events.resize(requests.size());
        std::free(this->buffer);
        this->buffer = static_cast<char*>(std::aligned_alloc(this->fs_block_size, requests.size() * this->_page_size));

        for (std::size_t i = 0; i < requests.size(); i++) {
            this->io_control_block_pointers[i] = &(this->io_control_blocks[i]);
        }

        this->allocated_buffer_size = requests.size();
    }

    // unsigned char *request_buffers = (unsigned char *)aligned_alloc(this->fs_block_size, requests.size() * this->fs_block_size);
    memset(this->buffer, 0, requests.size() * this->_page_size);

    uint64_t buffer_offset = 0;
    uint64_t read_page_count = 0;
    uint64_t write_page_count = 0;
    for (std::size_t i = 0; i < requests.size(); i++) {
        auto &request = requests[i];

        if (request.type == MemoryRequestType::READ) {
            io_prep_pread(&(this->io_control_blocks[i]), this->fd, this->buffer + buffer_offset, this->_page_size, request.address);
        } else {
            io_prep_pwrite(&(this->io_control_blocks[i]), this->fd, this->buffer + buffer_offset, this->_page_size, request.address);
        }

        if (request.type != MemoryRequestType::READ) {
            // copy write requests into the buffer
            memcpy(this->buffer + buffer_offset, request.data.data(), this->_page_size);
            write_page_count++;
        } else {
            read_page_count++;
        }

        buffer_offset += this->_page_size; // increment buffer
    }

    int ret_value = io_submit(this->io_context, requests.size(), this->io_control_block_pointers.data());
    if (ret_value < 0 ){
        throw std::runtime_error(absl::StrFormat("io_submit failed with code %d: %s: %s", -ret_value, strerrorname_np(-ret_value), strerrordesc_np(-ret_value)));
    }

    // wait for completion
    ret_value = io_getevents(this->io_context, requests.size(), requests.size(), this->io_events.data(), NULL);
    if (ret_value < 0 ){
        throw std::runtime_error(absl::StrFormat("io_getevents failed with code %d: %s: %s", -ret_value, strerrorname_np(-ret_value), strerrordesc_np(-ret_value)));
    }

    std::size_t fail_count = 0;
    for (std::size_t i = 0; i < requests.size(); i++) {
        auto &event = this->io_events[i];
        if (event.res != this->page_size()) {
            size_t index = (event.obj - this->io_control_blocks.data()) / sizeof(struct iocb);
            std::cout << absl::StreamFormat("IO request %lu failed, returned %lu bytes out of expected %lu\n", index, event.res, this->page_size());
            fail_count++;
        }
    }

    if (fail_count > 0) {
        std::cout.flush();
        throw std::runtime_error("Some IO requests failed!");
    }

    // now we will finish the read requests
    buffer_offset = 0UL;
    for (auto &request : requests) {
        if (request.type == MemoryRequestType::READ) {
            char *buffer = this->buffer + buffer_offset;
            memcpy(request.data.data(), buffer, this->_page_size);
        }
        buffer_offset += this->_page_size;
    }

    this->statistics->add_read_write(
        read_page_count * this->_page_size,
        write_page_count * this->_page_size
    );
}

void 
BlockDiskMemoryLibAIO::barrier() {
    this->Memory::barrier();
    fdatasync(this->fd);
}

bool 
BlockDiskMemoryLibAIO::is_request_type_supported(MemoryRequestType type) const noexcept {
    switch (type)
    {
    case MemoryRequestType::READ:
    case MemoryRequestType::WRITE:
        return true;
    
    default:
        return false;
    }
}

uint64_t 
BlockDiskMemoryLibAIO::page_size() const noexcept {
    return this->_page_size;
}

toml::table 
BlockDiskMemoryLibAIO::to_toml() const noexcept {
    auto table = this->Memory::to_toml();
    table.emplace("size", absl::StrFormat("%sB", size_to_string(this->size())));
    table.emplace("page_size", absl::StrFormat("%sB", size_to_string(this->page_size())));
    return table;
}

void 
BlockDiskMemoryLibAIO::save_to_disk(const std::filesystem::path &location) const {
    std::ofstream config_file(location / "config.toml");
    config_file << this->to_toml() << "\n";
    
    // copy file to location
    std::filesystem::copy_file(this->file_location, location / "contents.bin");
}

unique_memory_t 
BlockDiskMemoryLibAIO::load_from_disk(const std::filesystem::path &location, const toml::table &table) {
    // uint64_t size = parse_size(table["size"]);
    uint64_t page_size_temp = parse_size_or(table["page_size"], 0);
    std::optional<uint64_t> page_size;
    if (page_size_temp != 0) {
        page_size = {page_size_temp};
    }
    std::string_view name = table["name"].value<std::string_view>().value();
    return BlockDiskMemoryLibAIO::create(name, location / "contents.bin", page_size);
}

unique_memory_t 
BlockDiskMemoryLibAIO::load_from_disk(const std::filesystem::path &location) {
    auto table = toml::parse_file((location / "config.txt").string());
    return BlockDiskMemoryLibAIO::load_from_disk(location, table);
}

unique_memory_t 
BlockDiskMemoryLibAIOCached::create(std::string_view name, uint64_t size, uint64_t cache_size, std::optional<uint64_t> page_size) {
    std::filesystem::path temp_file_path = generate_temp_file_path();

    // create the temp file and fill with zeros
    int temp_file_fd = open(temp_file_path.c_str(), O_CREAT | O_RDWR, 0644);

    if (fallocate(temp_file_fd, FALLOC_FL_ZERO_RANGE, 0, size)) {
        throw std::runtime_error(absl::StrFormat("Fallocate failed with errno %d: %s", errno, strerror(errno)));
    }

    close(temp_file_fd);

    return BlockDiskMemoryLibAIOCached::create_second_stage(name, temp_file_path, cache_size, page_size);
}

unique_memory_t 
BlockDiskMemoryLibAIOCached::create(std::string_view name, std::filesystem::path data_file, uint64_t cache_size, std::optional<uint64_t> page_size) {
    std::filesystem::path temp_file_path = generate_temp_file_path();

    std::filesystem::copy_file(data_file, temp_file_path);

    return BlockDiskMemoryLibAIOCached::create_second_stage(name, temp_file_path, cache_size, page_size);
}

unique_memory_t 
BlockDiskMemoryLibAIOCached::create_second_stage(std::string_view name, std::filesystem::path temp_file_path, uint64_t cache_size, std::optional<uint64_t> page_size) {
    int fd = open(temp_file_path.c_str(), file_mode);

    assert(fd > 0);

    // get some stats of the file
    struct stat file_stat;
    fstat(fd, &file_stat);
    
    std::cout << "BlockMemoryLibAIOCached\n";
    std::cout << absl::StrFormat("File of size %lu\n", file_stat.st_size);
    std::cout << absl::StrFormat("File system block size %lu\n", file_stat.st_blksize);
    std::cout << absl::StreamFormat("Cache of size %lu\n", cache_size);

    // create the io_context
    constexpr int max_events = 128;
    io_context_t io_context = nullptr;
    int ret_value = io_setup(max_events, &io_context);
     if (ret_value < 0 ){
        throw std::runtime_error(absl::StrFormat("io_setup failed with code %d: %s: %s", -ret_value, strerrorname_np(-ret_value), strerrordesc_np(-ret_value)));
    }

    uint64_t unwrapped_page_size = page_size.value_or(file_stat.st_blksize);

    if (unwrapped_page_size < static_cast<uint64_t>(file_stat.st_blksize) || unwrapped_page_size % static_cast<uint64_t>(file_stat.st_blksize) != 0) {
        throw std::runtime_error(absl::StrFormat("Page size %lu is not an integer multiple of File system block size %lu!", unwrapped_page_size, file_stat.st_blksize));
    }

    if (cache_size < unwrapped_page_size || cache_size % unwrapped_page_size != 0) {
        throw std::runtime_error(absl::StrFormat("Cache size %lu is not an integer multiple of Page Size %lu", cache_size, unwrapped_page_size));
    }

    bytes_t cache(cache_size);

    // read cached portion into memory 
    [[maybe_unused]] auto ret_val = pread(fd, cache.data(), cache_size, 0);
    assert(ret_val == cache_size);

    return unique_memory_t(
        new BlockDiskMemoryLibAIOCached(
            name, 
            temp_file_path, 
            fd, 
            file_stat.st_size,
            unwrapped_page_size,
            file_stat.st_blksize, 
            io_context,
            std::move(cache)
        )
    );
}

BlockDiskMemoryLibAIOCached::BlockDiskMemoryLibAIOCached(
    std::string_view name,
    std::filesystem::path file_location,
    int fd,
    uint64_t size,
    uint64_t page_size,
    uint64_t fs_block_size,
    io_context_t io_context,
    bytes_t &&cache
):
Memory("BlockDiskMemoryLibAIOCached", name, size, new MemoryStatistics),
file_location(file_location),
io_context(io_context),
fd(fd),
file_size(size),
_page_size(page_size),
fs_block_size(fs_block_size),
allocated_buffer_size(0),
buffer(nullptr),
cache(std::move(cache))
{}

BlockDiskMemoryLibAIOCached::~BlockDiskMemoryLibAIOCached() noexcept {
    io_destroy(this->io_context);
    close(this->fd);
    std::free(this->buffer);
    try{
        std::filesystem::remove(this->file_location);
    } catch (...){
        std::cout << "An exception occurred while trying to delete temp data file!\n";
    }
}

uint64_t 
BlockDiskMemoryLibAIOCached::size() const noexcept {
    return this->file_size;
}

bool 
BlockDiskMemoryLibAIOCached::isBacked() const noexcept {
    return true;
}


void 
BlockDiskMemoryLibAIOCached::access(MemoryRequest &request) {
    std::vector<MemoryRequest> requests;
    requests.emplace_back(request);
    this->batch_access(requests);
    request = requests.front();
}

void 
BlockDiskMemoryLibAIOCached::batch_access(std::vector<MemoryRequest> &requests) {

    // check that all requests are page-aligned
    for (const auto &request : requests) {
        if (request.type != MemoryRequestType::READ && request.type != MemoryRequestType::WRITE) {
            throw std::runtime_error("BlockDiskMemoryLibAIO only supports READ and WRITE operations");
        }
        if (request.address % this->_page_size != 0 || request.size != this->_page_size) {
            throw std::runtime_error("BlockDiskMemoryLibAIO only supports page_aligned accesses");
        }
        this->Memory::log_request(request);
    }

    // set up async io for the batch
    // std::vector<aiocb> aio_control_blocks;

    if (requests.size() > this->allocated_buffer_size) {
        this->io_control_blocks.resize(requests.size());
        this->io_control_block_pointers.resize(requests.size());
        this->io_events.resize(requests.size());
        std::free(this->buffer);
        this->buffer = static_cast<char*>(std::aligned_alloc(this->fs_block_size, requests.size() * this->_page_size));

        for (std::size_t i = 0; i < requests.size(); i++) {
            this->io_control_block_pointers[i] = &(this->io_control_blocks[i]);
        }

        this->allocated_buffer_size = requests.size();
    }

    // unsigned char *request_buffers = (unsigned char *)aligned_alloc(this->fs_block_size, requests.size() * this->fs_block_size);
    memset(this->buffer, 0, requests.size() * this->_page_size);

    uint64_t buffer_offset = 0;
    uint64_t read_page_count = 0;
    uint64_t write_page_count = 0;
    uint64_t io_control_block_offset = 0;
    for (std::size_t i = 0; i < requests.size(); i++) {
        auto &request = requests[i];

        if (request.address >= this->cache.size()) {
            if (request.type == MemoryRequestType::READ) {
                io_prep_pread(&(this->io_control_blocks[io_control_block_offset]), this->fd, this->buffer + buffer_offset, this->_page_size, request.address);
            } else {
                io_prep_pwrite(&(this->io_control_blocks[io_control_block_offset]), this->fd, this->buffer + buffer_offset, this->_page_size, request.address);
            }

            if (request.type != MemoryRequestType::READ) {
                // copy write requests into the buffer
                memcpy(this->buffer + buffer_offset, request.data.data(), this->_page_size);
                write_page_count++;
            } else {
                read_page_count++;
            }

            buffer_offset += this->_page_size; // increment buffer
            io_control_block_offset ++;
        } else {
            if (request.type == MemoryRequestType::READ) {
                memcpy(request.data.data(), this->cache.data() + request.address, request.size);
                // read_page_count++;
            } else {
                memcpy(this->cache.data() + request.address, request.data.data(), request.size);
            }
        }
    }

    int ret_value = io_submit(this->io_context, io_control_block_offset, this->io_control_block_pointers.data());
    if (ret_value < 0 ){
        throw std::runtime_error(absl::StrFormat("io_submit failed with code %d: %s: %s", -ret_value, strerrorname_np(-ret_value), strerrordesc_np(-ret_value)));
    }

    // wait for completion
    ret_value = io_getevents(this->io_context, io_control_block_offset, io_control_block_offset, this->io_events.data(), NULL);
    if (ret_value < 0 ){
        throw std::runtime_error(absl::StrFormat("io_getevents failed with code %d: %s: %s", -ret_value, strerrorname_np(-ret_value), strerrordesc_np(-ret_value)));
    }

    std::size_t fail_count = 0;
    for (std::size_t i = 0; i < io_control_block_offset; i++) {
        auto &event = this->io_events[i];
        if (event.res != this->page_size()) {
            size_t index = (event.obj - this->io_control_blocks.data()) / sizeof(struct iocb);
            std::cout << absl::StreamFormat("IO request %lu failed, returned %lu bytes out of expected %lu\n", index, event.res, this->page_size());
            fail_count++;
        }
    }

    if (fail_count > 0) {
        std::cout.flush();
        throw std::runtime_error("Some IO requests failed!");
    }

    // now we will finish the read requests
    buffer_offset = 0UL;
    io_control_block_offset = 0;
    for (auto &request : requests) {
        if (request.address >= this->cache.size()) {
            if (request.type == MemoryRequestType::READ) {
                char *buffer = this->buffer + buffer_offset;
                memcpy(request.data.data(), buffer, this->_page_size);
            }
            buffer_offset += this->_page_size;
            io_control_block_offset ++;
        }
    }

    this->statistics->add_read_write(
        read_page_count * this->_page_size,
        write_page_count * this->_page_size
    );
}

void 
BlockDiskMemoryLibAIOCached::barrier() {
    this->Memory::barrier();
    fdatasync(this->fd);
}

bool 
BlockDiskMemoryLibAIOCached::is_request_type_supported(MemoryRequestType type) const noexcept {
    switch (type)
    {
    case MemoryRequestType::READ:
    case MemoryRequestType::WRITE:
        return true;
    
    default:
        return false;
    }
}

uint64_t 
BlockDiskMemoryLibAIOCached::page_size() const noexcept {
    return this->_page_size;
}

toml::table 
BlockDiskMemoryLibAIOCached::to_toml() const noexcept {
    auto table = this->Memory::to_toml();
    table.emplace("size", absl::StrFormat("%sB", size_to_string(this->size())));
    table.emplace("page_size", absl::StrFormat("%sB", size_to_string(this->page_size())));
    table.emplace("cache_size", absl::StrFormat("%sB", size_to_string(this->cache.size())));
    return table;
}

void 
BlockDiskMemoryLibAIOCached::save_to_disk(const std::filesystem::path &location) const {
    std::ofstream config_file(location / "config.toml");
    config_file << this->to_toml() << "\n";

    // write cache to disk
    [[maybe_unused]] auto ret_val = pwrite(this->fd, this->cache.data(), this->cache.size(), 0);
    assert(ret_val == this->cache.size());
    
    // copy file to location
    std::filesystem::copy_file(this->file_location, location / "contents.bin");
}

unique_memory_t 
BlockDiskMemoryLibAIOCached::load_from_disk(const std::filesystem::path &location, const toml::table &table) {
    // uint64_t size = parse_size(table["size"]);
    uint64_t page_size_temp = parse_size_or(table["page_size"], 0);
    uint64_t cache_size = parse_size(table["cache_size"]) + additional_cache;
    std::optional<uint64_t> page_size;
    if (page_size_temp != 0) {
        page_size = {page_size_temp};
    }
    std::string_view name = table["name"].value<std::string_view>().value();
    return BlockDiskMemoryLibAIOCached::create(name, location / "contents.bin", cache_size, page_size);
}

unique_memory_t 
BlockDiskMemoryLibAIOCached::load_from_disk(const std::filesystem::path &location) {
    auto table = toml::parse_file((location / "config.txt").string());
    return BlockDiskMemoryLibAIOCached::load_from_disk(location, table);
}


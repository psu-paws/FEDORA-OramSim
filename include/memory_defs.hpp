#pragma once
#include <vector>
#include <stdint.h>
#include <ranges>
#include <span>
#include <functional>

typedef unsigned char byte_t;
typedef std::vector<byte_t> bytes_t;

typedef uint64_t addr_t;

typedef std::function<void(byte_t * value, const byte_t * data)> memory_update_function;

enum MemoryRequestType {
    READ,
    WRITE,
    READ_WRITE, //!< Writes to the memory and returns its original contents.
    UPDATE,
    POP, //!< Removes blocks from memory
    DUMMY_POP,
    PUSH, //!< Add a new block to memory
    DUMMY_PUSH,
};

struct MemoryRequest {
    MemoryRequestType type;
    uint64_t address;
    uint64_t size;
    bytes_t data;
    memory_update_function update_function;
    MemoryRequest() : type(MemoryRequestType::READ), address(0), size(0), data(){
    };

    /**
     * @brief Construct a new Memory Request object
     * 
     * @deprecated Try to use the other two constructors to avoid duplicated arguments.
     * 
     * @param type type of access
     * @param address the address of the access, some memory may have alignment requirements
     * @param size the size of the access, some memory may have alignment requirements
     * @param data a bytes_t object to hold the data
     */
    MemoryRequest(MemoryRequestType type, uint64_t address, uint64_t size, bytes_t &&data) : 
        type(type), address(address), size(size), data(std::move(data)) {};
    
    /**
     * @brief Construct a new Memory Request object.
     * 
     * The data field is initalized with a zero filled bytes_t object of the given size.
     * @param type type of access
     * @param address the address of the access, some memory may have alignment requirements
     * @param size the size of the access, some memory may have alignment requirements 
     */
    MemoryRequest(MemoryRequestType type, uint64_t address, uint64_t size) : 
        type(type), address(address), size(size), data(bytes_t(size)) {};
    
    MemoryRequest(MemoryRequestType type, uint64_t address, bytes_t &&data) : 
        type(type), address(address), size(data.size()), data(std::move(data)) {};
};

using ConstMemoryRequestSpan = std::span<const MemoryRequest>;
using MemoryRequestSpan = std::span<MemoryRequest>;
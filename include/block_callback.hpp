#pragma once
#include <memory_defs.hpp>
#include <oram_defs.hpp>
#include <functional>

// typedef void (*block_call_back) (const BlockMetadata *metadata, const byte_t * data);
typedef std::function<void(const BlockMetadata * metadata, const byte_t * data)> block_call_back;
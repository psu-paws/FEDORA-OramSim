#pragma once
#include <cstdint>
#include <immintrin.h>
#include <cstring>

// force inline so the compiler can optimize this stuff away
__attribute__((always_inline)) inline void * conditional_memcpy(bool condition, void * dest, const void * src, std::size_t count) {
    std::size_t offset = 0;
    unsigned char * dest_char = static_cast<unsigned char*>(dest);
    const unsigned char * src_char = static_cast<const unsigned char*>(src);

    std::int64_t single_mask_element = condition ? -1: 0;
    __m256i mask = _mm256_set1_epi64x(single_mask_element);

    for(; offset + 32 <= count; offset += 32) {
        __m256i local_src = _mm256_loadu_si256(reinterpret_cast<const __m256i_u *>(src_char + offset));
        __m256i local_dest = _mm256_loadu_si256(reinterpret_cast<const __m256i_u *>(dest_char + offset));

        local_dest = _mm256_blendv_epi8(local_dest, local_src, mask);

        _mm256_storeu_si256(reinterpret_cast<__m256i_u *>(dest_char + offset), local_dest);
    }

    __m128i mask_2 = _mm_set1_epi64x(single_mask_element);
    for(; offset + 16 <= count; offset += 16) {
        __m128i local_src = _mm_loadu_si128(reinterpret_cast<const __m128i_u *>(src_char + offset));
        __m128i local_dest = _mm_loadu_si128(reinterpret_cast<const __m128i_u *>(dest_char + offset));

        local_dest = _mm_blendv_epi8(local_dest, local_src, mask_2);

        _mm_storeu_si128(reinterpret_cast<__m128i_u *>(dest_char + offset), local_dest);
    }

    // copy in units of 8 bytes
    // X86 can handel misaligned accesses
    for(; offset + 8 <= count; offset += 8) {
        std::uint64_t local_src = 0;
        std::uint64_t local_dest = 0;

        std::memcpy(&local_src, src_char + offset, 8);
        std::memcpy(&local_dest, dest_char + offset, 8);

        asm volatile(
            "test %2, %2 \n\t"
            "cmovne %1, %0 \n\t"
            : "+r"(local_dest)
            : "r"(local_src), "r"(condition)
            :

        );

        std::memcpy(dest_char + offset, &local_dest, 8);
    }

    // copy in units of 4 bytes
    // X86 can handel misaligned accesses
    for(; offset + 4 <= count; offset += 4) {
        std::uint32_t local_src = 0;
        std::uint32_t local_dest = 0;

        std::memcpy(&local_src, src_char + offset, 4);
        std::memcpy(&local_dest, dest_char + offset, 4);

        asm volatile(
            "test %2, %2 \n\t"
            "cmovne %1, %0 \n\t"
            : "+r"(local_dest)
            : "r"(local_src), "r"(condition)
            :

        );

        std::memcpy(dest_char + offset, &local_dest, 4);
    }

    // copy in units of 2 bytes
    // X86 can handel misaligned accesses
    for(; offset + 2 <= count; offset += 2) {
        std::uint16_t local_src = 0;
        std::uint16_t local_dest = 0;

        std::memcpy(&local_src, src_char + offset, 2);
        std::memcpy(&local_dest, dest_char + offset, 2);

        asm volatile(
            "test %2, %2 \n\t"
            "cmovne %1, %0 \n\t"
            : "+r"(local_dest)
            : "r"(local_src), "r"(condition)
            :

        );

        std::memcpy(dest_char + offset, &local_dest, 2);
    }

    // copy in units of 1 bytes
    // X86 can handel misaligned accesses
    for (; offset < count; offset ++) {
        // there is no 8-bit conditional move instruction
        // so we are still using the 16-bit version and just discarding the upper
        // byte
        std::uint16_t local_src = 0;
        std::uint16_t local_dest = 0;

        std::memcpy(&local_src, src_char + offset, 1);
        std::memcpy(&local_dest, dest_char + offset, 1);

        asm volatile(
            "test %2, %2 \n\t"
            "cmovne %1, %0 \n\t"
            : "+r"(local_dest)
            : "r"(local_src), "r"(condition)
            :

        );

        std::memcpy(dest_char + offset, &local_dest, 1);
    }


    return dest;
}

inline void * conditional_zero(bool condition, void * dest, std::size_t count) {
    std::size_t offset = 0;
    unsigned char * dest_char = static_cast<unsigned char*>(dest);
    // const unsigned char * src_char = static_cast<const unsigned char*>(src);


    // copy in units of 8 bytes
    // X86 can handel misaligned accesses
    for(; offset + 8 <= count; offset += 8) {
        std::uint64_t local_src = 0;
        std::uint64_t local_dest = 0;

        std::memcpy(&local_dest, dest_char + offset, 8);

        asm volatile(
            "test %2, %2 \n\t"
            "cmovne %1, %0 \n\t"
            : "+r"(local_dest)
            : "r"(local_src), "r"(condition)
            :

        );

        std::memcpy(dest_char + offset, &local_dest, 8);
    }

    // copy in units of 4 bytes
    // X86 can handel misaligned accesses
    for(; offset + 4 <= count; offset += 4) {
        std::uint32_t local_src = 0;
        std::uint32_t local_dest = 0;

        std::memcpy(&local_dest, dest_char + offset, 4);

        asm volatile(
            "test %2, %2 \n\t"
            "cmovne %1, %0 \n\t"
            : "+r"(local_dest)
            : "r"(local_src), "r"(condition)
            :

        );

        std::memcpy(dest_char + offset, &local_dest, 4);
    }

    // copy in units of 2 bytes
    // X86 can handel misaligned accesses
    for(; offset + 2 <= count; offset += 2) {
        std::uint16_t local_src = 0;
        std::uint16_t local_dest = 0;

        std::memcpy(&local_dest, dest_char + offset, 2);

        asm volatile(
            "test %2, %2 \n\t"
            "cmovne %1, %0 \n\t"
            : "+r"(local_dest)
            : "r"(local_src), "r"(condition)
            :

        );

        std::memcpy(dest_char + offset, &local_dest, 2);
    }

    // copy in units of 1 bytes
    for (; offset < count; offset ++) {
        // there is no 8-bit conditional move instruction
        // so we are still using the 16-bit version and just discarding the upper
        // byte
        std::uint16_t local_src = 0;
        std::uint16_t local_dest = 0;

        std::memcpy(&local_dest, dest_char + offset, 1);

        asm volatile(
            "test %2, %2 \n\t"
            "cmovne %1, %0 \n\t"
            : "+r"(local_dest)
            : "r"(local_src), "r"(condition)
            :

        );

        std::memcpy(dest_char + offset, &local_dest, 1);
    }


    return dest;
}

typedef void *(*conditional_memcpy_signature)(bool, void*, const void*, std::size_t count);

conditional_memcpy_signature get_conditional_memcpy_function(std::size_t count);
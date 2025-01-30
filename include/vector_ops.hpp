#if defined(__AVX__)
#include <immintrin.h>
#endif

#if defined(__SSE__)
#include <xmmintrin.h>
#endif

#include <type_traits>
#include <math.h>
#include <numeric>

inline float vector_reduce_sum_f(const float *data, size_t count) {
    size_t index = 0;
    float sum = 0;

    #if defined(__AVX512F__)
    constexpr size_t num_floats_per_512_vector = 256 / (8 * sizeof(float));
    __m512 accumulator512 = _mm512_set1_ps(0.0);
    #endif

    #if defined(__AVX__)
    constexpr size_t num_floats_per_256_vector = 256 / (8 * sizeof(float));
    __m256 accumulator256 = _mm256_set1_ps(0.0);
    #endif

    #if defined(__SSE__)
    constexpr size_t num_floats_per_128_vector = 128 / (8 * sizeof(float));
    __m128 accumulator128 = _mm_set1_ps(0.0);
    #endif

    #if defined(__AVX512F__)
    for (;index < count - num_floats_per_512_vector + 1; index += num_floats_per_512_vector) {
        __m512 input = _mm512_loadu_ps(data + index);
        accumulator512 = _mm512_add_ps(accumulator512, input);
    }

    // extract top and bottom halves of 512 bit accumulator and sum them
    // this has to be written this way because Intel
    __m256 upper256 = _mm256_castsi256_ps(_mm512_extracti64x4_epi64(_mm512_castps_si512(accumulator512), 1));
    accumulator256 = _mm512_castps512_ps256(accumulator512);

    accumulator256 = _mm256_add_ps(upper256, accumulator256);

    #endif

    #if defined(__AVX__)
    for (;index < count - num_floats_per_256_vector + 1; index += num_floats_per_256_vector) {
        __m256 input = _mm256_loadu_ps(data + index);
        accumulator256 = _mm256_add_ps(accumulator256, input);
    }

    // extract top and bottom halves of 256 bit accumulator and sum them
    __m128 upper128 = _mm256_extractf128_ps(accumulator256, 1);
    accumulator128 = _mm256_castps256_ps128(accumulator256);

    accumulator128 = _mm_add_ps(upper128, accumulator128);

    #endif

    #if defined(__SSE__)
        
    for (;index < count - num_floats_per_128_vector + 1; index += num_floats_per_128_vector) {
        __m128 input = _mm_loadu_ps(data + index);
        accumulator128 = _mm_add_ps(accumulator128, input);
    }

    // compute horizontal sum

    // accumulator128 = [D C B A]
    // shuffled = [C D A B]
    __m128 shuffled = _mm_shuffle_ps(accumulator128, accumulator128, _MM_SHUFFLE(2, 3, 0, 1));

    // accumulator128 = [C+D C+D A+B A+B]
    accumulator128 = _mm_add_ps(accumulator128, shuffled);

    // shuffled = [C+D C+D C+D C+D]
    shuffled = _mm_movehl_ps(accumulator128, accumulator128);

    // accumulator128 = [C+D C+D A+B A+B+C+D]
    accumulator128 = _mm_add_ss(accumulator128, shuffled);

    sum = _mm_cvtss_f32(accumulator128);

    #endif

    for (;index < count; index++) {
        sum += data[index];
    }

    return sum;
}

inline double vector_reduce_sum_d(const double *data, size_t count) {
    size_t index = 0;
    double sum = 0;

    #if defined(__AVX512F__)
    constexpr size_t num_doubles_per_512_vector = 256 / (8 * sizeof(double));
    __m512d accumulator512 = _mm512_set1_pd(0.0);
    #endif

    #if defined(__AVX__)
    constexpr size_t num_doubles_per_256_vector = 256 / (8 * sizeof(double));
    __m256d accumulator256 = _mm256_set1_pd(0.0);
    #endif

    #if defined(__SSE__)
    constexpr size_t num_doubles_per_128_vector = 128 / (8 * sizeof(double));
    __m128d accumulator128 = _mm_set1_pd(0.0);
    #endif

    #if defined(__AVX512F__)
    for (;index < count - num_doubles_per_512_vector + 1; index += num_doubles_per_512_vector) {
        __m512d input = _mm512_loadu_pd(data + index);
        accumulator512 = _mm512_add_pd(accumulator512, input);
    }

    // extract top and bottom halves of 512 bit accumulator and sum them
    // this has to be written this way because Intel
    __m256d upper256 = _mm256_castsi256_pd(_mm512_extracti64x4_epi64(_mm512_castpd_si512(accumulator512), 1));
    accumulator256 = _mm512_castpd512_pd256(accumulator512);

    accumulator256 = _mm256_add_pd(upper256, accumulator256);

    #endif

    #if defined(__AVX__)
    for (;index < count - num_doubles_per_256_vector + 1; index += num_doubles_per_256_vector) {
        __m256d input = _mm256_loadu_pd(data + index);
        accumulator256 = _mm256_add_pd(accumulator256, input);
    }

    // extract top and bottom halves of 256 bit accumulator and sum them
    __m128d upper128 = _mm256_extractf128_pd(accumulator256, 1);
    accumulator128 = _mm256_castpd256_pd128(accumulator256);

    accumulator128 = _mm_add_pd(upper128, accumulator128);

    #endif

    #if defined(__SSE__)
        
    for (;index < count - num_doubles_per_128_vector + 1; index += num_doubles_per_128_vector) {
        __m128d input = _mm_loadu_pd(data + index);
        accumulator128 = _mm_add_pd(accumulator128, input);
    }

    // compute horizontal sum
    __m128d high64 = _mm_unpackhi_pd(accumulator128, accumulator128);
    accumulator128 = _mm_add_pd(accumulator128, high64);
    sum = _mm_cvtsd_f64(accumulator128);

    #endif

    for (;index < count; index++) {
        sum += data[index];
    }

    return sum;
}

template<typename T>
inline T vector_reduce_sum(const T *data, size_t count) {
    if constexpr (std::is_same_v<double, T>) {
        return vector_reduce_sum_d(data, count);
    } else if constexpr (std::is_same_v<float, T>) {
        return vector_reduce_sum_f(data, count);
    } else {
        std::accumulate(data, data + count, (T)0);
    }
}

template<typename T>
inline void vector_range_default(T * data, size_t count, size_t inital_offset) {
    for (size_t i = 0; i < count; i++) {
        data[i] = (T)(i + inital_offset);
    }
}

inline void vector_range_float(float * data, size_t count, size_t inital_offset) {
    #if defined(__SSE__)
        constexpr size_t num_floats_per_128_vector = 128 / (8 * sizeof(float));
        __m128 offset128;
        __m128 step128;

        if (count >= num_floats_per_128_vector) {
            offset128 = _mm_set_ps(3.0, 2.0, 1.0, 0.0);
            step128 = _mm_set_ps1(num_floats_per_128_vector);
        }
    #endif

    #if defined(__AVX__)
        constexpr size_t num_floats_per_256_vector = 256 / (8 * sizeof(float));
        __m256 offset256;
        __m256 step256;

        if (count >= num_floats_per_256_vector) {
            offset256 = _mm256_set_ps(7.0, 6.0, 5.0, 4.0, 3.0, 2.0, 1.0, 0.0);
            step256 = _mm256_set1_ps(num_floats_per_256_vector);
        }
    #endif

    #if defined(__AVX512F__)
        constexpr size_t num_floats_per_512_vector = 512 / (8 * sizeof(float));
        __m512 offset512;
        __m512 step512;

        if (count >= num_floats_per_512_vector) {
            offset512 = _mm512_set_ps(
                15.0, 14.0, 13.0, 12.0, 11.0, 10.0, 9.0, 8.0, 
                7.0, 6.0, 5.0, 4.0, 3.0, 2.0, 1.0, 0.0
            );
            step512 = _mm512_set1_ps(num_floats_per_512_vector);
        }
    #endif

    float accumulator = float(inital_offset);
    size_t index = 0;

    #if defined(__AVX512F__)
    __m512 accumulator512 = _mm512_set1_ps(accumulator);

    for (;index < count - num_floats_per_512_vector + 1; index += num_floats_per_512_vector) {
        __m512 output = _mm512_add_ps(accumulator512, offset512);
        _mm512_storeu_ps(data + index, output);
        accumulator512 = _mm512_add_ps(accumulator512, step512);
    }
    #endif

    #if defined(__AVX__)

    #if defined(__AVX512F__)
        __m256 accumulator256 = _mm512_castps512_ps256(accumulator512);
    #else
        __m256 accumulator256 = _mm256_set1_ps(accumulator);
    #endif

    for (;index < count - num_floats_per_256_vector + 1; index += num_floats_per_256_vector) {
        __m256 output = _mm256_add_ps(accumulator256, offset256);
        _mm256_storeu_ps(data + index, output);
        accumulator256 = _mm256_add_ps(accumulator256, step256);
    }
    #endif

    #if defined(__SSE__)

    #if defined(__AVX__)
        __m128 accumulator128 = _mm256_castps256_ps128(accumulator256);
    #else
        __m128 accumulator128 = _mm_set1_ps(accumulator);
    #endif
        
    for (;index < count - num_floats_per_128_vector + 1; index += num_floats_per_128_vector) {
        __m128 output = _mm_add_ps(accumulator128, offset128);
        _mm_storeu_ps(data + index, output);
        accumulator128 = _mm_add_ps(accumulator128, step128);
    }

    accumulator = _mm_cvtss_f32(accumulator128);
    #endif

    for (; index < count; index++) {
        data[index] = accumulator;
        accumulator = accumulator + 1.0;
    }
}

template<typename T>
inline void vector_range(T * data, size_t count, size_t inital_offset = 0) {
    if constexpr (std::is_same_v<float, T>) {
        vector_range_float(data, count, inital_offset);
    } else {
        vector_range_default(data, count, inital_offset);
    }
}
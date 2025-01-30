#if defined(__AVX__) || defined(__AVX2__)
#include "avx_mathfun.h"
#include <immintrin.h>
#endif

#include <type_traits>
#include <math.h>

template<typename T>
void exp_inplace(T* data, size_t count) {
size_t index = 0;
#if defined(__AVX__) || defined(__AVX2__)
if constexpr (std::is_same_v<double, T>) {
    // constexpr size_t num_doubles_per_vector = 256 / (8 * sizeof(double)); 
    // for (;index < count - num_doubles_per_vector + 1; index += num_doubles_per_vector) {
        
    // }

} else if constexpr(std::is_same_v<float, T>){
    constexpr size_t num_floats_per_vector = 256 / (8 * sizeof(float)); 
    for (;index < count - num_floats_per_vector + 1; index += num_floats_per_vector) {
        __m256 input = _mm256_loadu_ps(data + index);
        __m256 result = exp256_ps(input);
        _mm256_storeu_ps(data + index, result);
    }
}

for (;index < count; index++) {
    data[index] = exp(data[index]);
}

#endif

}

#include <softmax.hpp>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <absl/random/random.h>

template <typename T>
std::vector<T> compute_exp_dp_probs(size_t k_total, size_t k_union, T epsilon) {
    std::vector<T> result(k_total);

    T neg_half_epsilon = -epsilon / 2.0;
    T k_union_float = k_union;

    vector_range(result.data(), k_total, 1);

    std::for_each(result.begin(), result.end(), [=] (T& value) {
        value = k_union_float - value;
        value = std::abs(value) * neg_half_epsilon;
    });

    softmax(result.data(), result.size());

    return result;
}


template <typename T>
size_t sample_exp_dp(T* probs, size_t k_total, absl::BitGen &bitgen) {
    T sample = absl::Uniform(bitgen, (T)0.0, (T)1.0);
    size_t result = k_total;
    for (size_t i = 0; i < k_total; i++) {
        bool do_update = sample > 0.0;
        sample -= probs[i];
        result = (do_update && sample < 0.0) ? i + 1 : result;
    }
    return result;
}


template <typename T>
size_t compute_and_sample_exp_dp(size_t k_total, size_t k_union, T epsilon, absl::BitGen &bitgen) {
    auto probs = compute_exp_dp_probs(k_total, k_union, epsilon);
    return sample_exp_dp(probs.data(), k_total, bitgen);
}
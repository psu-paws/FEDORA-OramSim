#include <cmath>
#include <algorithm>
#include <numeric>
#include <exp.h>
#include <vector_ops.hpp>
#include <execution>

template <typename T>
void softmax(T* array, size_t count) {
    // find largest value in input
    T max_value = *std::max_element(array, array + count);

    // offset input by that amount for better numeric properties
    std::for_each(array, array + count, [=](auto &input) {input = input - max_value;});

    // compute the exponencial function
    exp_inplace(array, count);

    // computer sum
    T sum = std::reduce(std::execution::unseq,array, array + count, (T)0.0);
    // T sum = vector_reduce_sum(array, count);

    // normalize
    std::for_each(array, array + count, [=](auto &input){input = input / sum;});
}
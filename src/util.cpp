#include <util.hpp>

#include <regex>
#include <iostream>
#include <absl/strings/str_format.h>
#include <absl/random/random.h>
#include <unordered_map>
#include <sstream>

bool check_address(uint64_t address, uint64_t valid_range_start, uint64_t valid_range_end) noexcept{
    return (address >= valid_range_start) && (address <= valid_range_end);
}

bool check_access_range(const MemoryRequest &request, uint64_t valid_range_start, uint64_t valid_range_end) noexcept{
    return check_address(request.address, valid_range_start, valid_range_end) && check_address(request.address + request.size - 1, valid_range_start, valid_range_end);
}

std::string remove_comment(const std::string &line) {
    auto comment_start = line.find('#');
    std::string result;
    if(comment_start == std::string::npos) {
        // no # found
        result = line;
    } else {
        result = line.substr(0, comment_start);
    }

    return result;
}

std::regex size_regex ("([0-9]+(\\.[0-9]*)?)\\s*(?:([MmKkGgTtPp])(i)?)?B?", std::regex_constants::ECMAScript);
const std::unordered_map<std::string, uint64_t> si_prefix_order = {
    {"K", 1},
    {"M", 2},
    {"G", 3},
    {"T", 4},
    {"P", 5}
};

const std::array<std::string, 5> order_to_si_prefix = {
    "K",
    "M",
    "G",
    "T",
    "P"
};

uint64_t parse_size(const std::string_view input) {
    std::cmatch match;
    if (!std::regex_search(input.begin(), input.end(), match, size_regex)) {
        throw std::runtime_error(absl::StrFormat("Can not parse input %s as size!", input));
    }
    
    auto number_submatch = match[1];
    auto decimal_submatch = match[2];
    auto si_submatch = match[3];
    auto binary_submatch = match[4];

    uint64_t multiplier = 1;
    if (si_submatch.matched) {
        // std::cout << absl::StrFormat("SI prefix is '%s'\n", si_submatch.str());
        uint64_t base = 1000;
        std::string si_prefix = si_submatch.str();
        std::transform(
            si_prefix.begin(),
            si_prefix.end(),
            si_prefix.begin(),
            [] (unsigned char c) {
                return std::toupper(c);
            }
        );
        auto si_order_iter = si_prefix_order.find(si_prefix);

        if (si_order_iter == si_prefix_order.end()) {
            throw std::runtime_error(absl::StrFormat("Unknown SI Prefix '%s'!", si_prefix));
        }

        if(binary_submatch.matched) {
            // std::cout << "Use 1024 instead of 1000\n";
            base = 1024;
        }

        for (uint64_t si_order = si_order_iter->second; si_order > 0; si_order--) {
            multiplier *= base;
        }
    }

    uint64_t size;
    if (decimal_submatch.matched) {
        // std::cout << "use double parser\n";
        double value = std::stod(number_submatch.str());
        size = (uint64_t)(value * multiplier);
    } else {
        // std::cout << "use unsigned long parser\n";
        uint64_t value = std::stoul(number_submatch.str());
        size = value * multiplier;
    }

    // std::cout << absl::StrFormat("Size parsed to be %lu\n", size);

    return size;
}

template<typename T, T base>
std::pair<uint64_t, T> max_power(T number) {
    if (number == 0) {
        return {0, 0};
    }
    uint64_t power = 0;
    while(number % base == 0) {
        number /= base;
        power++;
    }
    return {power, number};
}

std::string size_to_string(uint64_t size) {
    auto binary_pair = max_power<uint64_t, 1024>(size);
    auto decimal_pair = max_power<uint64_t, 1000>(size);

    bool use_binary = (binary_pair.first >= decimal_pair.first); // if its cleaner to write in binary (1024-based)

    uint64_t reminder;
    uint64_t order;
    if (use_binary) {
        reminder = binary_pair.second;
        order = binary_pair.first;
    } else {
        reminder = decimal_pair.second;
        order = decimal_pair.first;
    }

    // correct for too large of an order
    while (order > order_to_si_prefix.size()) {
        reminder *= (use_binary ? 1024 : 1000);
        order--;
    }

    std::string si_prefix = "";
    std::string binary_indicator = "";

    if (order != 0) {
        si_prefix = order_to_si_prefix[order - 1];
        if (use_binary) {
            binary_indicator = "i";
        }
    }

    // generate string
    return absl::StrFormat("%lu%s%s", reminder, si_prefix, binary_indicator);
}

uint64_t parse_size(const toml::node *node) {
    if (node->is_integer()) {
        return (uint64_t)(node->value<uint64_t>().value());
    } else if (node->is_string()){
        return parse_size(node->value<std::string_view>().value());
    } else {
        throw std::invalid_argument("Given node is neither integer nor string");
    }
}

uint64_t parse_size(const toml::node_view<const toml::node> &node) {
    if (node) {
        return parse_size(node.node());
    } else {
        throw std::invalid_argument("Given node does not exist");
    }
}

uint64_t parse_size_or(const toml::node_view<const toml::node> &node, uint64_t default_value) {
    if (node) {
        return parse_size(node.node());
    } else {
        return default_value;
    }
}

uint64_t parse_size(const toml::node &input) {
    if (input.is_integer()) {
        return (uint64_t)input.value<uint64_t>().value();
    } else if (input.is_string()){
        return parse_size(input.value<std::string_view>().value());
    } else {
        throw std::invalid_argument("Given node is neither integer nor string");
    }
}

char generate_random_character() {
    static absl::BitGen bit_gen;
    static const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

    return alphabet[absl::Uniform(bit_gen, 0u, alphabet.size())];
}

double half_normal_cdf(double x) {
    const double sqrt_2 = sqrt(2);
    return erf(x / sqrt_2);
}
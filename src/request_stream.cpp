#include "request_stream.hpp"
#include <absl/strings/str_format.h>
#include <algorithm>

std::unordered_map<std::string, std::unique_ptr<AccessPattern>> pattern_map;

void initialize_pattern_map() {
    pattern_map.insert(std::make_pair("Uniform", std::make_unique<ConcreteAccessPattern>(std::vector<uint64_t>{}, 1)));
    pattern_map.insert(std::make_pair("Kaggle", std::make_unique<ConcreteAccessPattern>(
        std::vector<uint64_t>{
            1559473UL,
            1141289UL, 
            495012UL, 
            453649UL, 
            449682UL, 
            397452UL, 
            378974UL, 
            361487UL, 
            351949UL, 
            334713UL,
            332996UL,
            322498UL,
            270222UL,
            236333UL,
            215100UL,
            180441UL,
            163030UL,
            159342UL,
            157998UL,
            152529UL
        }, 45840617UL))
    );
    pattern_map.insert(std::make_pair("Exp10", std::make_unique<ExponentialPattern>(10)));
    pattern_map.insert(std::make_pair("Exp5", std::make_unique<ExponentialPattern>(5)));
    pattern_map.insert(std::make_pair("Exp1.5", std::make_unique<ExponentialPattern>(1.0)));
    pattern_map.insert(std::make_pair("Exp1", std::make_unique<ExponentialPattern>(1.0)));
    pattern_map.insert(std::make_pair("Exp0.5", std::make_unique<ExponentialPattern>(0.5)));

}

const AccessPattern &get_pattern_by_name(std::string_view name) {

    if (pattern_map.size() == 0) {
        initialize_pattern_map();
    }

    auto iter = pattern_map.find(std::string(name));

    if (iter == pattern_map.end()) {
        throw std::runtime_error(absl::StrFormat("Unknown pattern \"%s\"", name));
    }

    return *(iter->second);
}

unique_request_stream_t 
TraceRequestStream::create(std::filesystem::path trace_file) {
    return unique_request_stream_t(
        new TraceRequestStream(std::ifstream(trace_file))
    );
}

TraceRequestStream::TraceRequestStream(std::ifstream &&stream) : stream(std::move(stream))
{}

bool 
TraceRequestStream::next() {
    std::string line;

    if(std::getline(stream, line)) {
        std::stringstream line_stream(line);
            
        char op_type;
        line_stream >> op_type;

        line_stream >> std::dec >> this->size;

        line_stream >> std::hex >> this->address;

        op_type = std::tolower(op_type);

        if (op_type == 'r') {
            this->type = MemoryRequestType::READ;
        } else if (op_type == 'w') {
            this->type = MemoryRequestType::WRITE;
        } else {
            throw new std::runtime_error("Unknown request type!");
        }
        return true;
    } else {
        return false;
    }
}

void 
TraceRequestStream::inplace_get(MemoryRequest &request) const {
    request.type = this->type;
    request.address = this->address;
    request.size = this->size;
    request.data.resize(this->size);
}

unique_request_stream_t 
RandomRequestStream::create(std::string_view pattern_type, uint64_t request_size, uint64_t memory_size) {
    return unique_request_stream_t(
        new RandomRequestStream(
            get_pattern_by_name(pattern_type),
            request_size,
            memory_size
        )
    );
}

RandomRequestStream::RandomRequestStream(const AccessPattern &pattern, uint64_t request_size, uint64_t memory_size) 
: pattern(pattern), request_size(request_size), memory_size(memory_size)
{}

bool 
RandomRequestStream::next() {
    this->address = this->pattern.get_address(this->memory_size / this->request_size, this->bit_gen) * this->request_size;
    return true;
}

void 
RandomRequestStream::inplace_get(MemoryRequest &request) const {
    request.type = MemoryRequestType::READ;
    request.address = this->address;
    request.size = this->request_size;
    request.data.resize(this->request_size);
}

ConcreteAccessPattern::ConcreteAccessPattern(std::vector<uint64_t> &&skew_values, uint64_t skew_limit) :
skew_limit(skew_limit), skew_values(std::move(skew_values))
{}

uint64_t 
ConcreteAccessPattern::get_address(uint64_t max, absl::BitGen &bit_gen) const {
    // roll skew
    uint64_t skew_roll = absl::Uniform(bit_gen, 0UL, this->skew_limit);

    for (uint64_t skew_value = 0; skew_value < this->skew_values.size(); skew_value++) {
        if (skew_roll < this->skew_values[skew_value]) {
            return skew_value;
        }

        skew_roll -= this->skew_values[skew_value];
    }

    // skew roll unsuccessful
    return absl::Uniform(bit_gen, 0UL, max);
}

ExponentialPattern::ExponentialPattern(double lambda) 
: lambda(lambda)
{}

uint64_t
ExponentialPattern::get_address(uint64_t max, absl::BitGen &bit_gen) const {
    double normalization_factor = (double)max / 10.0;
    double roll = absl::Exponential(bit_gen, this->lambda);
    uint64_t value = std::floor(roll * normalization_factor);
    if (value >= max) {
        value = max - 1;
    }

    return value;
}

ReuseStream::ReuseStream(double reuse_fraction, uint64_t request_size, uint64_t memory_size) :
index(0),
next_index(0),
reuse_fraction(reuse_fraction),
request_size(request_size),
memory_size(memory_size)
{
    std::uint64_t num_entries = memory_size / request_size;
    this->permutation.reserve(num_entries);
    for (std::uint64_t i = 0; i < num_entries; i++) {
        this->permutation.emplace_back(i);
    }

    std::cout << absl::StreamFormat("Shuffling %lu indicies...\n", num_entries);
    std::shuffle(this->permutation.begin(), this->permutation.end(), this->bit_gen);
    std::cout << absl::StreamFormat("Done\n");
}

bool 
ReuseStream::next() {
    double reuse_roll = absl::Uniform(this->bit_gen, 0.0, 1.0);

    if (reuse_roll < reuse_fraction && this->next_index != 0) {
        // roll succeeded
        this->index = absl::Uniform(this->bit_gen, 0UL, this->next_index);
    } else {
        this->index = this->next_index;
        this->next_index++;

        if (this->next_index >= this->permutation.size()) {
            throw std::runtime_error("End of Permutation");
        }
    }

    return true;
}

void 
ReuseStream::inplace_get(MemoryRequest &request) const {
    request.type = MemoryRequestType::READ;
    request.address = this->permutation[this->index];
    request.size = this->request_size;
    request.data.resize(this->request_size);
}
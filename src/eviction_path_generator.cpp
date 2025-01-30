#include <eviction_path_generator.hpp>
#include <util.hpp>

EvictionPathGenerator::EvictionPathGenerator(std::vector<int64_t> &&level_sizes) :
level_sizes(std::move(level_sizes)),
level_indices(this->level_sizes.size())
{}

EvictionPathGenerator::EvictionPathGenerator(const std::vector<int64_t> &level_sizes) :
EvictionPathGenerator(std::vector<int64_t>(level_sizes))
{}

EvictionPathGenerator::EvictionPathGenerator(const toml::table *table) :
level_sizes(vector_from_toml_array<int64_t>((*table)["level_sizes"].as_array())),
level_indices(vector_from_toml_array<int64_t>((*table)["level_indices"].as_array()))
{}

int64_t 
EvictionPathGenerator::next_path(){
    // generate path number
    int64_t path = 0;
    for (size_t i = 0; i < level_sizes.size(); i++) {
        path = path * this->level_sizes[i];
        path = path + this->level_indices[i];
    }

    // advance indices
    for (size_t i = 0; i < level_sizes.size(); i++) {
        this->level_indices[i] ++;
        if (this->level_indices[i] >= this->level_sizes[i]){
            this->level_indices[i] = 0;
        } else {
            break;
        }
    }

    return path;
}

toml::table 
EvictionPathGenerator::to_toml() const {
    toml::table table;
    table.emplace("level_sizes", toml_array_from_vector(this->level_sizes));
    table.emplace("level_indices", toml_array_from_vector(this->level_indices));
    return table;
}
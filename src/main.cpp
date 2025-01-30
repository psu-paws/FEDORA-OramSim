#include <iostream>
#include <ostream>
#include <fstream>
#include <memory_interface.hpp>
#include <simple_memory.hpp>
#include <memory_loader.hpp>
#include <disk_memory.hpp>
#include <util.hpp>
#include <oram.hpp>
#include <driver.hpp>
#include <oram_builders.hpp>
#include <filesystem>
#include <unordered_map>
#include <request_coalescer.hpp>
#include <memory_adapters.hpp>
#include <binary_tree_layout.hpp>
#include <page_optimized_raw_oram.hpp>
#include <eviction_path_generator.hpp>
#include <recsys_sim.hpp>

#include <cxxopts.hpp>

void print_help() {
    std::cout << "TODO: Add Help\n";
}

int scratch_pad(int argc, const char** argv);

std::unordered_map<std::string, int (*)(int, const char**)> subcommand_entry_points = {
    {"create", create_oram_entry_point},
    {"run_trace", trace_runner_entry_point},
    {"recsys_sim", recsys_sim_entry_point}
};

int main(int argc, char** argv) {

    cxxopts::Options crossroad_options("OramSimulator", "");
    crossroad_options.add_options()
    ("subcommand", "The subcommand to run", cxxopts::value<std::string>())
    ("h, help", "Prints help message");
    crossroad_options.parse_positional({"subcommand"});
    crossroad_options.allow_unrecognised_options();

    auto crossroad_results = crossroad_options.parse(argc, argv);

    if (crossroad_results.count("subcommand") != 1) {
        if (crossroad_results.count("help") > 0) {
            std::cout << crossroad_options.help();
            exit(0);
        }
        std::cout << "A subcommand is needed!\n";
        std::cout << crossroad_options.help();
        exit(1);
    }

    std::string subcommand = crossroad_results["subcommand"].as<std::string>();

    std::cout << "Subcommand is " << subcommand << "\n";

    // if (subcommand == "create") {
    //     create_oram_entry_point(argc, (const char**)argv);
    // }

    // find entry point for subcommand
    auto entry_point_iter = subcommand_entry_points.find(subcommand);

    if (entry_point_iter == subcommand_entry_points.end()) {
        std::cout << absl::StrFormat("Unable to find entry point for subcommand '%s'!\n", subcommand);
        exit(-1);
    }

    auto entry_point = entry_point_iter->second;

    return entry_point(argc, (const char**) argv);
}


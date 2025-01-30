#include <recsys_sim.hpp>

#include <cxxopts.hpp>
#include <iostream>
#include <filesystem>
#include <disk_memory.hpp>
#include <request_stream.hpp>
#include <absl/random/random.h>
#include <memory_loader.hpp>
#include <util.hpp>
#include <vector>
#include <recsys_buffer.hpp>
#include <algorithm>
#include <chrono>
#include <absl/strings/str_format.h>
#include <toml++/toml.h>

enum SimMode {
    PATTERN,
    REUSE,
    SAMPLE_FILE
};

int recsys_sim_entry_point(int argc, const char** argv) {
    cxxopts::Options recsys_sim_options("Runs trace file", "Read a trace file and execute it on given memory");

    recsys_sim_options.add_options()
    ("subcommand", "ignore", cxxopts::value<std::string>())
    ("m,memory", "Directory to load memory from", cxxopts::value<std::string>())
    ("b,buffer", "Which buffer to use", cxxopts::value<std::string>()->default_value("LinearScanBuffer"))
    // ("t,trace", "Trace file to run", cxxopts::value<std::string>())
    ("P,pattern", "Name of the pattern to use in random mode", cxxopts::value<std::string>())
    ("s,sample_file", "Use samples from a file", cxxopts::value<std::string>())
    ("R,reused_fraction", "Fraction of accesses to be reused", cxxopts::value<double>())
    ("r,rounds", "Number of rounds to simulate", cxxopts::value<std::string>()->default_value("1Ki"))
    ("S,samples_per_round", "Number of samples per round", cxxopts::value<std::string>()->default_value("5000"))
    ("p,post_memory", "Directory to save memory after running the trace", cxxopts::value<std::string>())
    ("C,additional_cache", "Bytes ", cxxopts::value<std::string>())
    ("U,k_union", "Number of request to process in each union", cxxopts::value<std::string>()->default_value("4Ki"))
    ("E,epsilon", "Epsilon paramter for DP modes", cxxopts::value<float>()->default_value("1.0"))
    // ("l,log", "Enable access logging", cxxopts::value<bool>()->default_value("false"))
    ("v,verbose", "Enable verbose output.", cxxopts::value<bool>()->default_value("false"))
    // ("V, verify", "Check contents.", cxxopts::value<bool>()->default_value("false"))
    ("d, temp_dir", "Change directory where temp files for disk memory are stored.", cxxopts::value<std::string>()->default_value("."))
    ("u, unsafe_optimization", "Enable unsafe optimizations", cxxopts::value<bool>()->default_value("false"))
    // ("T, threads", "Number of threads to run.", cxxopts::value<std::size_t>()->default_value("1"))
    // ("s, stat_file", "File dump stats.", cxxopts::value<std::string>())
    ("o, output_file", "Generate TOML output summarizing results.", cxxopts::value<std::string>())
    ("h,help", "show help text");

    recsys_sim_options.parse_positional("subcommand");

    auto result = recsys_sim_options.parse(argc, argv);

    if (result.count("subcommand") != 1 || result["subcommand"].as<std::string>() != "recsys_sim") {
        std::cout << "Incorrect sub command!\n";
        return -1;
    }

    if (result.count("help") > 0) {
        std::cout << recsys_sim_options.help();
        return 0;
    }

    if (result.count("additional_cache") > 0) {
        auto additional_cache_amount = parse_size(result["additional_cache"].as<std::string>());
        std::cout << absl::StreamFormat("Adding %lu bytes of additional cache\n", additional_cache_amount);
        set_additional_cache_amount(additional_cache_amount);
    }


    if (result.count("memory") != 1) {
        std::cout << "Need to specify a memory directory!\n";
        return -1;
    }

    

    const std::filesystem::path memory_directory(result["memory"].as<std::string>());
    const std::filesystem::path temp_dir(result["temp_dir"].as<std::string>());
    // bool enable_logging = result["log"].as<bool>();
    bool verbose = result["verbose"].as<bool>();
    bool unsafe_opt = result["unsafe_optimization"].as<bool>();
    // bool verify = result["verify"].as<bool>();
    auto samples_per_round = parse_size(result["samples_per_round"].as<std::string>());
    auto num_rounds = parse_size(result["rounds"].as<std::string>());

    set_disk_memory_temp_file_directory(temp_dir);

    std::unique_ptr<RecSysBuffer> buffer;

    std::string buffer_name = result["buffer"].as<std::string>();

    float epsilon = result["epsilon"].as<float>();
    uint64_t k_union = parse_size(result["k_union"].as<std::string>());

    bool use_reserve = false;

    if (buffer_name == "NoBuffer") {
        buffer = std::make_unique<NoBuffer>(
            MemoryLoader::load(memory_directory)
        );
    } else if (buffer_name == "LinearScanBuffer") {
        buffer = std::make_unique<LinearScanBuffer>(
            MemoryLoader::load(memory_directory),
            samples_per_round,
            unsafe_opt
        );
    } else if (buffer_name == "ORAMBuffer") {
        buffer = std::make_unique<OramBuffer>(
            MemoryLoader::load(memory_directory),
            samples_per_round,
            unsafe_opt
        );
    }  else if (buffer_name == "ORAMBufferPopNPush") {
        buffer = std::make_unique<OramBuffer>(
            MemoryLoader::load(memory_directory),
            samples_per_round,
            unsafe_opt,
            OramBuffer::UpdateMode::POP_N_PUSH
        );
    } else if (buffer_name == "ORAMBuffer3") {
        buffer = std::make_unique<OramBuffer3>(
            MemoryLoader::load(memory_directory),
            samples_per_round,
            unsafe_opt
        );
    } else if (buffer_name == "ORAMBuffer3RAW") {
        buffer = std::make_unique<OramBuffer3>(
            MemoryLoader::load(memory_directory),
            samples_per_round,
            unsafe_opt,
            OramBuffer3::BufferORAMType::PageOptimizedRAWORAM
        );
    } else if (buffer_name == "ORAMBufferDP") {
        buffer = std::make_unique<OramBufferDP>(
            MemoryLoader::load(memory_directory),
            samples_per_round,
            k_union,
            epsilon,
            OramBufferDP::BufferORAMType::PageOptimizedRAWORAM
        );
        use_reserve = true;
    } else if (buffer_name == "ORAMBufferDPLinearScanPosmap") {
        buffer = std::make_unique<OramBufferDPLinearScan>(
            MemoryLoader::load(memory_directory),
            samples_per_round,
            k_union,
            epsilon,
            OramBufferDPLinearScan::BufferORAMType::PageOptimizedRAWORAM
        );
        use_reserve = true;
    }
    else {
        std::cout << absl::StreamFormat("Unknown buffer type \"%s\"\n", buffer_name);
        exit(-1);
    }
    // const auto entry_size = buffer.entry_size();
    const auto num_entries = buffer->num_entries();
    
    auto bit_gen = absl::BitGen();
    std::string pattern_name;
    SimMode mode;

    // reuse mode stuff
    std::uint64_t next_index = 0;
    double reuse_fraction = 0.0;
    std::vector<std::uint64_t> permutation;
    std::ifstream sample_file;

    if (result.count("pattern") > 0) {
        pattern_name = result["pattern"].as<std::string>();
        mode = PATTERN;
    } else if (result.count("sample_file") > 0) {
        mode = SAMPLE_FILE;
        sample_file.open(result["sample_file"].as<std::string>());
        pattern_name = "Uniform";
    } else {
        mode = REUSE;
        reuse_fraction = result["reused_fraction"].as<double>();
        pattern_name = "Uniform";
        permutation.reserve(num_entries);
        for (std::uint64_t i = 0; i < num_entries; i++) {
            permutation.emplace_back(i);
        }
    }
     
    auto &pattern = get_pattern_by_name(pattern_name);

    std::vector<std::uint64_t> samples(samples_per_round);

    buffer->underlying_memory()->reset_statistics();

    for (std::uint64_t round = 0; round < num_rounds; round++) {
        if (mode == REUSE) {
            std::cout << absl::StreamFormat("Shuffling %lu indicies...\n", permutation.size());
            std::shuffle(permutation.begin(), permutation.end(), bit_gen);
            std::cout << absl::StreamFormat("Done\n");

            next_index = 0;
        }

        for (std::uint64_t i = 0; i < samples_per_round; i++) {
            if (mode == PATTERN) {
                samples[i] = pattern.get_address(num_entries, bit_gen);
            } if (mode == SAMPLE_FILE) {
                sample_file >> samples[i];
            } else {
                double reuse_roll = absl::Uniform(bit_gen, 0.0, 1.0);
                if (reuse_roll < reuse_fraction && next_index != 0) {
                    auto index = absl::Uniform(bit_gen, 0UL, next_index);
                    samples[i] = permutation[index];
                } else {
                    samples[i] = permutation[next_index];
                    next_index++;
                    if (next_index >= permutation.size()) {
                        throw std::runtime_error("Permutation end");
                    }
                }
            }
        }

        std::cout << absl::StreamFormat("Staring round %lu of %lu\n", round + 1, num_rounds);

        if (use_reserve) {
            std::cout << absl::StreamFormat("Staring Reservation Phase of round %lu\n", round + 1);

            for (std::uint64_t i = 0; i < samples_per_round; i++) {
                if (verbose) {
                    std::cout << absl::StreamFormat("Downloading entry %lu\n", samples[i]);
                }
                buffer->reserve(samples[i]);
            }

            std::cout << absl::StreamFormat("Staring Load Phase of round %lu\n", round + 1);

            buffer->load_entries();
        }


        std::cout << absl::StreamFormat("Staring Download Phase of round %lu\n", round + 1);

        for (std::uint64_t i = 0; i < samples_per_round; i++) {
            if (verbose) {
                std::cout << absl::StreamFormat("Downloading entry %lu\n", samples[i]);
            }
            buffer->download(samples[i]);
        }


        std::cout << absl::StreamFormat("Staring Aggregation Phase of round %lu\n", round + 1);

        std::shuffle(samples.begin(), samples.end(), bit_gen);

        for (std::uint64_t i = 0; i < samples_per_round; i++) {
            if (verbose) {
                std::cout << absl::StreamFormat("Aggregating entry %lu\n", samples[i]);
            }
            buffer->aggregate(samples[i]);
        }

        std::cout << absl::StreamFormat("Staring Update Phase of round %lu\n", round + 1);

        buffer->update_flush_buffer();

        std::cout << absl::StreamFormat("Completed round %lu of %lu\n", round + 1, num_rounds);
    }

    std::chrono::duration<double> oram_time_seconds(buffer->get_oram_time());
    std::chrono::duration<double> overall_time_seconds(buffer->get_overall_time());
    std::chrono::duration<double> buffer_time_seconds(overall_time_seconds - oram_time_seconds);

    std::size_t total_requests = buffer->get_total_requests();
    std::size_t k_union_sum = buffer->get_k_union_sum();
    std::size_t k_sum = buffer->get_k_sum();
    std::size_t num_dropped_entries = buffer->get_num_dropped_entries();
    std::size_t num_dropped_requests = buffer->get_num_dropped_requests();


    std::cout << absl::StreamFormat("%lu samples completed in %lf seconds\n", samples_per_round * num_rounds, overall_time_seconds.count());
    std::cout << absl::StreamFormat("Averaging %lf ms per sample.\n", overall_time_seconds.count() * 1000.0 / static_cast<double>(samples_per_round * num_rounds));
    std::cout << absl::StreamFormat("ORAM took %lf seconds\n", oram_time_seconds.count());
    std::cout << absl::StreamFormat("Averaging %lf ms ORAM time per sample.\n", oram_time_seconds.count() * 1000.0 / static_cast<double>(samples_per_round * num_rounds));
    std::cout << absl::StreamFormat("Buffer took %lf seconds\n", buffer_time_seconds.count());
    std::cout << absl::StreamFormat("Averaging %lf ms buffer time per sample.\n", buffer_time_seconds.count() * 1000.0 / static_cast<double>(samples_per_round * num_rounds));
    buffer->underlying_memory()->save_statistics();
    buffer->save_buffer_stats();

    if (use_reserve) {
        std::cout << absl::StreamFormat("%lu total requests\n", total_requests);
        std::cout << absl::StreamFormat("%lu k_union sum\n", k_union_sum);
        std::cout << absl::StreamFormat("%lu k sum\n", k_sum);
        std::cout << absl::StreamFormat("%lu dropped entries (%f%%)\n", num_dropped_entries, (double)num_dropped_entries / (double)k_union_sum * 100.0);
        std::cout << absl::StreamFormat("%lu dropped requests (%f%%)\n", num_dropped_requests, (double)num_dropped_requests / (double)total_requests * 100.0);
    }

    if (result.count("output_file") > 0) {
        // generate output file
        toml::table table;
        table.emplace("rounds", static_cast<int64_t>(num_rounds));
        table.emplace("samples_per_round", static_cast<int64_t>(samples_per_round));

        table.emplace("oram_time_ns", buffer->get_oram_time().count());
        table.emplace("overall_time_ns", buffer->get_overall_time().count());
        table.emplace("buffer_time_ns", (buffer->get_overall_time() - buffer->get_oram_time()).count());

        table.emplace("oram_time_seconds", oram_time_seconds.count());
        table.emplace("overall_time_seconds", overall_time_seconds.count());
        table.emplace("buffer_time_seconds", buffer_time_seconds.count());

        table.emplace("ms_per_sample", overall_time_seconds.count() * 1000.0 / static_cast<double>(samples_per_round * num_rounds));

        if (use_reserve) {
            table.emplace("total_requests", static_cast<int64_t>(total_requests));
            table.emplace("k_union_sum", static_cast<int64_t>(k_union_sum));
            table.emplace("k_sum", static_cast<int64_t>(k_sum));
            table.emplace("num_dropped_entries", static_cast<int64_t>(num_dropped_entries));
            table.emplace("num_dropped_requests", static_cast<int64_t>(num_dropped_requests));
        }

        std::ofstream out_file(result["output_file"].as<std::string>());

        out_file << table;
    }

    return 0;
}
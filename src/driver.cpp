#include <driver.hpp>
#include <sstream>
#include <iostream>
#include <fstream>
#include <cxxopts.hpp>
#include <filesystem>
#include <memory_loader.hpp>
#include <disk_memory.hpp>
#include <absl/strings/str_format.h>
#include <chrono>
#include <limits>
#include <util.hpp>
#include <absl/random/random.h>
#include <thread>
#include <algorithm>
#include <ranges>

typedef std::chrono::high_resolution_clock timer;

struct RunnerInfo {
    unique_memory_t memory;
    uint64_t count_limit;
    unique_request_stream_t request_stream;
    timer::duration duration;
    uint64_t actual_count;
    uint64_t correct_count;

    [[nodiscard]] static RunnerInfo count_based(unique_memory_t &&memory, uint64_t count_limit, unique_request_stream_t && request_stream) noexcept {
        RunnerInfo ret_value;
        ret_value.memory = std::move(memory);
        ret_value.count_limit = count_limit;
        ret_value.request_stream = std::move(request_stream);
        return ret_value;
    };

    [[nodiscard]] static RunnerInfo time_based(unique_memory_t &&memory, unique_request_stream_t && request_stream) noexcept {
        RunnerInfo ret_value;
        ret_value.memory = std::move(memory);
        ret_value.count_limit = std::numeric_limits<uint64_t>().max();
        ret_value.request_stream = std::move(request_stream);
        return ret_value;
    };
};

int trace_runner_entry_point(int argc, const char** argv){
    cxxopts::Options trace_runner_options("Runs trace file", "Read a trace file and execute it on given memory");

    trace_runner_options.add_options()
    ("subcommand", "ignore", cxxopts::value<std::string>())
    ("m,memory", "Directory to load memory from", cxxopts::value<std::string>())
    ("t,trace", "Trace file to run", cxxopts::value<std::string>())
    ("P,pattern", "Name of the pattern to use in random mode", cxxopts::value<std::string>())
    ("S,access_size", "Size of the block used in random accesses", cxxopts::value<std::string>()->default_value("64B"))
    ("c,count", "number of access to do in random mode", cxxopts::value<std::string>()->default_value("100Ki"))
    ("p,post_memory", "Directory to save memory after running the trace", cxxopts::value<std::string>())
    ("l,log", "Enable access logging", cxxopts::value<bool>()->default_value("false"))
    ("v,verbose", "Enable verbose output.", cxxopts::value<bool>()->default_value("false"))
    ("V, verify", "Check contents.", cxxopts::value<bool>()->default_value("false"))
    ("d, temp_dir", "Change directory where temp files for disk memory are stored.", cxxopts::value<std::string>()->default_value("."))
    ("T, threads", "Number of threads to run.", cxxopts::value<std::size_t>()->default_value("1"))
    ("s, stat_file", "File dump stats.", cxxopts::value<std::string>())
    ("h,help", "show help text");

    trace_runner_options.parse_positional("subcommand");

    auto result = trace_runner_options.parse(argc, argv);

    if (result.count("subcommand") != 1 || result["subcommand"].as<std::string>() != "run_trace") {
        std::cout << "Incorrect sub command!\n";
        return -1;
    }

    if (result.count("help") > 0) {
        std::cout << trace_runner_options.help();
        return 0;
    }

    if (result.count("memory") != 1) {
        std::cout << "Need to specify a memory directory!\n";
        return -1;
    }

    const std::filesystem::path memory_directory(result["memory"].as<std::string>());
    const std::filesystem::path temp_dir(result["temp_dir"].as<std::string>());
    bool enable_logging = result["log"].as<bool>();
    bool verbose = result["verbose"].as<bool>();
    bool verify = result["verify"].as<bool>();
    const std::size_t num_threads = result["threads"].as<std::size_t>();

    // std::cout << absl::StrFormat("Temp dir set to %s\n", temp_dir.c_str());
    
    set_disk_memory_temp_file_directory(temp_dir);

    // unique_memory_t memory = MemoryLoader::load(memory_directory);

    std::vector<RunnerInfo> runner_infos;
    uint64_t limit = parse_size(result["count"].as<std::string>());

    for (size_t i = 0; i < num_threads; i++) {
        unique_memory_t memory = MemoryLoader::load(memory_directory);
        unique_request_stream_t request_stream;
        if (result.count("trace") == 1) {
            std::filesystem::path trace_location(result["trace"].as<std::string>());
            request_stream = TraceRequestStream::create(trace_location);
            limit = UINT64_MAX;
        } else if (result.count("pattern") == 1) {
            request_stream = RandomRequestStream::create(result["pattern"].as<std::string>(), memory->page_size(), memory->size());
            // access_times.reserve(limit);
        } else {
            std::cout << "Need to specify one of --trace or --pattern!" << "\n";
            return -1;
        }

        if (enable_logging) {
            memory->start_logging();
        } else {
            memory->reset_statistics();
        }

        runner_infos.emplace_back(RunnerInfo::count_based(std::move(memory), limit, std::move(request_stream)));
    }

    std::cout << absl::StreamFormat("Using %lu threads.\n", num_threads);

    {
        std::vector<std::jthread> threads;
        for (std::size_t i = 0; i < num_threads; i++) {
            threads.emplace_back([&limit, &verify, &verbose](std::stop_token stop_token, RunnerInfo *info) {
                timer::time_point start, stop;
                timer::time_point end_last_access;

                info->actual_count = 0;
                info->correct_count = 0;

                start = timer::now();
                end_last_access = start;

                MemoryRequest request;
                
                for (info->actual_count = 0; info->actual_count < limit; info->actual_count++){
                    if (!(info->request_stream->next())) {
                        break;
                    }

                    info->request_stream->inplace_get(request);

                    info->memory->access(request);
                    
                    if (verify) {
                        uint64_t value;
                        if (request.size >= 8) {
                            value = *(uint64_t *)request.data.data();
                        } else if (request.size >= 4) {
                            value = *(uint32_t *)request.data.data();
                        } else if (request.size >= 2) {
                            value = *(uint16_t *)request.data.data();
                        } else {
                            value = *(uint8_t *)request.data.data();
                        }


                        if (value == (request.address / request.size)) {
                            info->correct_count++;
                        } else {
                            std::cout << absl::StrFormat("@ 0x%08lX, Expecting %lu got %lu\n", request.address, request.address / request.size, value);
                        }
                    }

                    timer::time_point current_time = timer::now();
                    // access_times.emplace_back(std::chrono::duration<int64_t, std::nano>(current_time - end_last_access).count());
                    end_last_access = current_time;

                    if (verbose) {
                        std::string_view long_op_type;
                        switch (request.type) {
                            case MemoryRequestType::READ:
                                long_op_type = "R";
                                break;
                            case MemoryRequestType::WRITE:
                                long_op_type = "W";
                                break;
                            case MemoryRequestType::READ_WRITE:
                                long_op_type = "RW";
                                break;
                            default:
                                long_op_type = "UNKNOWN";
                                break;
                        }
                        std::cout << absl::StrFormat("%lu %s 0x%08lX %lu bytes\n", info->actual_count, long_op_type, request.address, request.size);
                    }
                }
                
                stop = timer::now();
                info->duration = stop - start;
            }, &(runner_infos[i]));
        }

        // todo add cancel infomation
    }

    for (auto &runner_info : runner_infos) {
        if (enable_logging) {
            runner_info.memory->stop_logging();
        } else {
            runner_info.memory->save_statistics();
        }
    }

    timer::duration overall_duration = std::ranges::max_element(runner_infos, std::less{}, [](auto const &info) {return info.duration;})->duration;
    auto actual_count_projection = std::ranges::views::transform(runner_infos, [](auto const &info) {return info.actual_count;});
    uint64_t overall_count = std::accumulate(actual_count_projection.begin(), actual_count_projection.end(), 0);
    auto correct_count_projection = std::ranges::views::transform(runner_infos, [](auto const &info) {return info.correct_count;});
    uint64_t overall_correct_count = std::accumulate(correct_count_projection.begin(), correct_count_projection.end(), 0);

    double overall_duration_seconds = std::chrono::duration<double>(overall_duration).count();

    toml::table stat_table;
    stat_table.emplace("total_accesses", static_cast<int64_t>(overall_count));
    stat_table.emplace("overall_time", overall_duration_seconds);
    stat_table.emplace("overall_time_ns", std::chrono::nanoseconds(overall_duration).count());


    auto seconds = std::chrono::duration<double>(overall_duration).count();

    std::cout << absl::StreamFormat("%lu threads completed!\n", num_threads);
    std::cout << absl::StrFormat("%lu accesses completed in %.3lf seconds\n", overall_count, seconds);
    if (verify) {
        std::cout << absl::StrFormat("Verify: %lu out of %lu accesses correct\n", overall_correct_count, overall_count);
        // stat_table.emplace("accesses_correct", static_cast<int64_t>(correct_count));
    }
    std::cout << absl::StrFormat("%.3lfms per access on average.\n", (seconds / (double) overall_count) * 1000.0);
    stat_table.emplace("average_accesses_latency_ms",(seconds / (double) overall_count) * 1000.0);
    stat_table.emplace("average_accesses_latency_ns", static_cast<int64_t>(std::chrono::nanoseconds(overall_duration).count() / overall_count));

    // stat_table.emplace("access_latencies", toml_array_from_vector(access_times));
    
    if (result.count("stat_file") > 0) {
        std::ofstream stat_file(result["stat_file"].as<std::string>());
        stat_file << stat_table;
    }

    if (result.count("post_memory") > 0) {
        std::filesystem::path post_memory_dir(result["post_memory"].as<std::string>());
        if (std::filesystem::exists(post_memory_dir)) {
            if (!std::filesystem::is_directory(post_memory_dir)) {
                 std::cout << absl::StrFormat("Specified memory save directory \"%s\" is not a directory!\n", post_memory_dir);
                return -1;
            }
        } else {
            std::filesystem::create_directories(post_memory_dir);
        }

        runner_infos[0].memory->save_to_disk(post_memory_dir);
    }
    
    return 0;
}
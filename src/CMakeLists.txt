
# all the non-entry point files should be here.
add_library(
    OramLibrary OBJECT
    "disk_memory.cpp"
    "driver.cpp"
    "memory_adapter.cpp"
    "memory_loader.cpp"
    "oram.cpp"
    "oram_builders.cpp"
    "request_coalescer.cpp"
    "simple_memory.cpp"
    "util.cpp"
    "statistics.cpp"
    "request_logger.cpp"
    "binary_tree_layouts.cpp"
    "binary_tree_controller.cpp"
    "request_stream.cpp"
    "cache.cpp"
    "page_optimized_raw_oram.cpp"
    "eviction_path_generator.cpp"
    "stash.cpp"
    "valid_bit_tree.cpp"
    "recsys_buffer.cpp"
    "recsys_sim.cpp"
    "binary_path_oram_2.cpp"
    "conditional_memcpy.cpp"
)

target_link_libraries(OramLibrary -lrt)
target_link_libraries(OramLibrary -laio)
target_link_libraries(OramLibrary absl::strings)
target_link_libraries(OramLibrary absl::str_format)
target_link_libraries(OramLibrary absl::random_random)
target_link_libraries(OramLibrary cxxopts)
target_link_libraries(OramLibrary tomlplusplus::tomlplusplus)
target_link_libraries(OramLibrary -lsodium)

add_executable(OramSimulator "main.cpp")
target_link_libraries(OramSimulator OramLibrary)
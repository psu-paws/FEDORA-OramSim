#include <oram_builders.hpp>
#include <util.hpp>
#include <oram.hpp>
#include <simple_memory.hpp>
#include <memory>
#include <ostream>
#include <fstream>
#include <absl/strings/str_format.h>
#include <cxxopts.hpp>
#include <filesystem>
#include <binary_tree_layout.hpp>
#include <memory_adapters.hpp>
#include <page_optimized_raw_oram.hpp>
#include <disk_memory.hpp>
#include <valid_bit_tree.hpp>
#include <crypto_module.hpp>
#include <binary_path_oram_2.hpp>

int create_oram_entry_point(int argc, const char** argv) {
    cxxopts::Options oram_options("Create ORAM", "Sets up an oram");

    oram_options.add_options()
    ("subcommand", "ignore", cxxopts::value<std::string>())
    ("t,type", "The type of Oram to create", cxxopts::value<std::string>()->default_value("BinaryPathOram"))
    ("l,layout", "The layout to use for the unsafe memory tree", cxxopts::value<std::string>()->default_value("BasicHeapLayout"))
    ("s,size", "The size of Oram to create", cxxopts::value<std::string>()->default_value("100KiB"))
    ("b,block_size", "The size of blocks in Oram", cxxopts::value<std::string>()->default_value("64B"))
    ("P,page_size", "The size of the first level untrusted memory pages", cxxopts::value<std::string>()->default_value("4KiB"))
    ("z,blocks_per_bucket", "The number of blocks in each bucket", cxxopts::value<uint64_t>()->default_value("4"))
    ("p,position_map_size", "The size of secure storage available for position map", cxxopts::value<std::string>()->default_value("32KiB"))
    ("a,num_accesses_per_eviction", "Number of AO access per EO access, RAWOram only", cxxopts::value<uint64_t>()->default_value("4"))
    ("f,initializer_file", "content to initialize the ORAM with.", cxxopts::value<std::string>())
    ("o,output", "the location to store the ORAM.", cxxopts::value<std::string>())
    ("L,load_factor", "The maximum load factor the tree can have", cxxopts::value<double>()->default_value("0.75"))
    ("O,tree_order", "The order of the tree in PageOptimizedOram", cxxopts::value<uint64_t>()->default_value("2"))
    ("d, temp_dir", "Change directory where temp files for disk memory are stored.", cxxopts::value<std::string>()->default_value("."))
    ("F, fast_init", "Use fast init mode", cxxopts::value<bool>()->default_value("false"))
    ("S, stash_capacity", "Capacity of stash in blocks", cxxopts::value<std::string>()->default_value("200"))
    ("c, crypto_module", "Type of Crypto to use", cxxopts::value<std::string>()->default_value("PlainText"))
    ("e, levels_per_page", "How many levels of buckets to fit on each page, BinaryPathOram2 only", cxxopts::value<std::string>()->default_value("1"))
    ("h,help", "show help text");
    
    oram_options.parse_positional("subcommand");

    auto result = oram_options.parse(argc, argv);

    const std::filesystem::path temp_dir(result["temp_dir"].as<std::string>());
    set_disk_memory_temp_file_directory(temp_dir);

    if (result.count("subcommand") != 1 || result["subcommand"].as<std::string>() != "create") {
        std::cout << "Incorrect sub command!\n";
        exit(-1);
    }

    if (result.count("help") != 0) {
        std::cout << oram_options.help();
        exit(0);
    }

    uint64_t size = parse_size(result["size"].as<std::string>());
    uint64_t block_size = parse_size(result["block_size"].as<std::string>());
    uint64_t page_size = parse_size(result["page_size"].as<std::string>());
    uint64_t blocks_per_bucket = result["blocks_per_bucket"].as<uint64_t>();
    uint64_t max_position_map_size = parse_size(result["position_map_size"].as<std::string>());
    uint64_t num_accesses_per_eviction = result["num_accesses_per_eviction"].as<uint64_t>();
    uint64_t levels_per_page = parse_size(result["levels_per_page"].as<std::string>());
    bool fast_init = result["fast_init"].as<bool>();

    std::string type = result["type"].as<std::string>();
    std::string layout_type = result["layout"].as<std::string>();
    std::string crypto_module_type = result["crypto_module"].as<std::string>();

    double max_load_factor = result["load_factor"].as<double>();
    uint64_t tree_order = result["tree_order"].as<uint64_t>();

    std::filesystem::path oram_dir;
    if (result.count("output") != 1) {
        // std::cout << "Please specify output directory!\n";
        // exit(-1);
        oram_dir = absl::StrFormat("orams/%s-%lu-%lu-%lu", type, block_size, blocks_per_bucket, max_position_map_size);
    } else {
        oram_dir = result["output"].as<std::string>();
    }

    unique_memory_t oram;
    if (type == "BinaryPathOram") {
        oram = createBinaryPathOram(size, block_size, blocks_per_bucket, max_position_map_size, false, 0, layout_type, page_size);
    } else if (type == "HierarchicalOram") {
        oram = createBinaryPathOram(size, block_size, blocks_per_bucket, max_position_map_size, true, 0, layout_type, page_size);
    } else if (type == "RAWOram") {
        // oram = createRAWOram(size, block_size, blocks_per_bucket, num_accesses_per_eviction, max_position_map_size, true, layout_type, page_size);
    } else if (type == "PageOptimizedRAWOram") {
        oram = createPageOptimizedRAWOram(size, block_size, blocks_per_bucket, num_accesses_per_eviction, 4 * num_accesses_per_eviction, max_position_map_size, true, page_size, max_load_factor, tree_order, fast_init, crypto_module_type);
    } else if (type == "BinaryPathOram2") {
        oram = createBinaryPathOram2(
            size, block_size, page_size, true, max_position_map_size, true, 0, max_load_factor, fast_init, levels_per_page, crypto_module_type
        );
    } else if (type == "BinaryPathOram2L") {
        oram = createBinaryPathOram2(
            size, block_size, page_size, false, max_position_map_size, true, 0, max_load_factor, fast_init, levels_per_page, crypto_module_type
        );
    } else if (type == "LinearScannedMemory") {
        oram = LinearScannedMemory::create("linear_scanned_memory", size, block_size);
    }
    else {
        std::cerr << absl::StrFormat("Unknown Oram type '%s'!\n", type);
        std::cout.flush();
        exit(-1);
    }

    uint64_t num_valid_blocks = divide_round_up(size, block_size);

    auto toml_config = oram->to_toml();

    std::cout << "Constructing ORAM with the following parameters:\n";

    std::cout << "\n";

    std::cout << toml_config << "\n";

    if (fast_init) {
        if (type == "PageOptimizedRAWOram") {
            dynamic_cast<PageOptimizedRAWOram*>(oram.get())->fast_init();
        } else if (type == "BinaryPathOram2" || type=="BinaryPathOram2L") {
            dynamic_cast<BinaryPathOram2*>(oram.get())->fast_init();
        } else if (type == "LinearScannedMemory") {
            dynamic_cast<LinearScannedMemory*>(oram.get())->fast_init();
        }
        else {
            std::cerr << absl::StreamFormat("%s does not support fast initialization!\n", type) << std::endl;
            return -1;
        }
    } else {
        oram->init();

        MemoryRequest request = {MemoryRequestType::WRITE, 0, block_size, bytes_t(block_size, 0)};
        for (uint64_t block_address = 0; block_address < num_valid_blocks; block_address++) {
            if (block_size >= 8) {
                *((uint64_t*)request.data.data()) = block_address;
            } else if (block_size >= 4) {
                *((uint32_t*)request.data.data()) = block_address & 0xFFFFFFFFUL;
            } else if (block_size >= 2) {
                *((uint16_t*)request.data.data()) = block_address & 0xFFFFUL;
            } else if (block_size >= 1){
                *((uint8_t*)request.data.data()) = block_address & 0xFFUL;
            }
            request.address = block_address * block_size;

            if (block_address % 100000 == 0) {
                std::cout << absl::StrFormat("Writing Block %lu of %lu\n", block_address + 1, num_valid_blocks);
            }
            oram->access(request);
        }
    }

    if (std::filesystem::exists(oram_dir)) {
        std::filesystem::remove_all(oram_dir);
    }
    std::filesystem::create_directories(oram_dir);
    oram->save_to_disk(oram_dir.string());

    return 0;
}

unique_memory_t createBinaryPathOram(
    uint64_t size, uint64_t block_size, uint64_t blocks_per_bucket,
    uint64_t max_position_map_size, bool recursive,
    uint64_t recursive_level,
    std::string_view layout_type,
    uint64_t page_size
) {
    uint64_t num_blocks = divide_round_up(size, block_size);
    uint64_t num_buckets = divide_round_up(num_blocks, blocks_per_bucket);
    uint64_t levels = num_bits((num_buckets * 2) + 1);
    uint64_t position_map_size = num_blocks * sizeof(uint64_t);

    unique_tree_layout_t data_layout = BinaryTreeLayoutFactory::create(layout_type, levels, block_size * blocks_per_bucket, page_size, 0);
    unique_tree_layout_t metadata_layout = BinaryTreeLayoutFactory::create(layout_type, levels, block_metadata_size * blocks_per_bucket, page_size, data_layout->size());
    uint64_t untrusted_memory_size = data_layout->size() + metadata_layout->size();
    // std::shared_ptr<std::ostream> untrusted_memory_log = std::shared_ptr<std::ostream>(new std::ofstream(absl::StrFormat("level-%lu_untrusted_memory.log\n", recursive_level)));
    unique_memory_t untrusted_memory = BackedMemory::create(absl::StrFormat("level-%lu_untrusted_memory", recursive_level), untrusted_memory_size, page_size);

    unique_memory_t position_map;
    // std::shared_ptr<std::ostream> position_map_log = std::shared_ptr<std::ostream>(new std::ofstream(absl::StrFormat("level-%lu_position_map.log\n", recursive_level)));
    if (recursive && position_map_size > max_position_map_size) {
        position_map = createBinaryPathOram(position_map_size, block_size, blocks_per_bucket, max_position_map_size, recursive, recursive_level + 1);
    } else {
        position_map = BackedMemory::create(absl::StrFormat("level-%lu_position_map", recursive_level),position_map_size);
    }

    unique_memory_t oram = BinaryPathOram::create(absl::StrFormat("level-%lu_oram", recursive_level),
        std::move(position_map), std::move(untrusted_memory), block_size, levels,
        blocks_per_bucket, num_blocks,
        std::move(data_layout),
        std::move(metadata_layout)
    );


    std::cout << absl::StrFormat("level-%lu ORAM size %lu bytes, untrusted memory %lu bytes, postion map %lu bytes \n", recursive_level, size, untrusted_memory_size, position_map_size);

    return oram;
}

unique_memory_t createRAWOram(
    uint64_t size, uint64_t block_size, uint64_t blocks_per_bucket,
    uint64_t num_accesses_per_eviction,
    uint64_t max_position_map_size, bool recursive,
    std::string_view layout_type,
    uint64_t page_size
) {
//         uint64_t num_blocks = divide_round_up(size, block_size);
//         uint64_t num_buckets = divide_round_up(num_blocks, blocks_per_bucket);
//         uint64_t levels = num_bits(num_buckets + 1);
//         // uint64_t untrusted_memory_size = (1UL << levels) * blocks_per_bucket * (block_size + sizeof(BlockMetadata));
//         uint64_t position_map_size = num_blocks * sizeof(uint64_t);
//         // uint64_t bitfield_size = divide_round_up((1UL << levels) * blocks_per_bucket, 64UL * 8UL) * 64UL;

//         unique_tree_layout_t data_layout = BinaryTreeLayoutFactory::create(layout_type, levels, block_size * blocks_per_bucket, page_size, 0);
//         unique_tree_layout_t metadata_layout = BinaryTreeLayoutFactory::create(layout_type, levels, block_metadata_size * blocks_per_bucket, page_size, data_layout->size());
//         uint64_t untrusted_memory_size = data_layout->size() + metadata_layout->size();
//         unique_tree_layout_t bitfield_layout = BinaryTreeLayoutFactory::create(layout_type, levels, blocks_per_bucket, 64 * 8, 0);
//         uint64_t bitfield_size = divide_round_up(bitfield_layout->size(), 8UL);

//         // std::shared_ptr<std::ostream> untrusted_memory_log = std::shared_ptr<std::ostream>(new std::ofstream(absl::StrFormat("level-%lu_untrusted_memory.log\n", recursive_level)));
//         unique_memory_t untrusted_memory = BackedMemory::create(absl::StrFormat("level-%lu_untrusted_memory", 0), untrusted_memory_size, page_size);

//         unique_memory_t position_map;
//         // std::shared_ptr<std::ostream> position_map_log = std::shared_ptr<std::ostream>(new std::ofstream(absl::StrFormat("level-%lu_position_map.log\n", recursive_level)));
//         if (recursive && position_map_size > max_position_map_size) {
//             position_map = createBinaryPathOram(position_map_size, block_size, blocks_per_bucket, max_position_map_size, recursive, 1);
//         } else {
//             position_map = BackedMemory::create(absl::StrFormat("level-%lu_position_map", 0),position_map_size);
//         }

//         unique_memory_t bitfield = BitfieldAdapter::create("level-0_bitfield", BackedMemory::create("level-0_bitfield_memory", bitfield_size, 64));

//         unique_memory_t oram = RAWOram::create(
//             "RAWOram",
//             std::move(position_map), std::move(untrusted_memory), std::move(bitfield),
//             std::move(data_layout), std::move(metadata_layout), std::move(bitfield_layout),
//             block_size, levels,
//             blocks_per_bucket, num_blocks, num_accesses_per_eviction
//         );

//         std::cout << absl::StrFormat("level-%lu ORAM size %lu bytes, untrusted memory %lu bytes, postion map %lu bytes, bitfield %lu bytes \n", 0, size, untrusted_memory_size, position_map_size, bitfield_size);

//         return oram;
        return nullptr;
}


unique_memory_t createPageOptimizedRAWOram(
    uint64_t size, uint64_t block_size, uint64_t blocks_per_bucket,
    uint64_t num_accesses_per_eviction,
    uint64_t stash_capacity,
    uint64_t max_position_map_size, bool recursive,
    uint64_t page_size,
    double max_load_factor,
    uint64_t tree_order,
    bool fast_init,
    std::string_view crypto_module_name
) {
    uint64_t num_blocks = divide_round_up(size, block_size);
    // TODO: change to not hardcoded crypto module
    std::unique_ptr<CryptoModule> crypto_module = get_crypto_module_by_name(crypto_module_name);
    auto parameters = PageOptimizedRAWOram::compute_parameters(page_size, block_size, num_blocks, tree_order, crypto_module.get(), max_load_factor);
    auto valid_bit_tree_parameters = ParentCounterValidBitTreeController::compute_parameters(
        crypto_module.get(), 
        parameters.levels, 
        512, 
        parameters.blocks_per_bucket
    );
    unique_memory_t valid_bit_tree_memory = BackedMemory::create("Valid bit tree memory", valid_bit_tree_parameters.required_memory_size);
    std::unique_ptr<ValidBitTreeController> valid_bit_tree_controller = std::make_unique<ParentCounterValidBitTreeController>(valid_bit_tree_parameters, crypto_module.get(), valid_bit_tree_memory.get());
    // uint64_t untrusted_memory_size = (1UL << levels) * blocks_per_bucket * (block_size + sizeof(BlockMetadata));
    std::uint64_t position_map_page_size = 64;
    std::uint64_t num_position_map_entires_per_page = position_map_page_size / parameters.path_index_size;
    position_map_page_size = num_position_map_entires_per_page * parameters.path_index_size;
    std::cout << absl::StreamFormat("Each position map page of %lu bytes can hold %lu position map entry of %lu bytes\n", position_map_page_size, num_position_map_entires_per_page, parameters.path_index_size);
    std::uint64_t num_position_map_pages = divide_round_up(num_blocks, num_position_map_entires_per_page);
    uint64_t position_map_size = num_position_map_pages * position_map_page_size;
    std::cout << absl::StreamFormat("Position map needs %lu pages totaling %lu bytes to hold %lu entries\n", num_position_map_pages, position_map_size, num_blocks);
    // uint64_t bitfield_size = divide_round_up((1UL << levels) * blocks_per_bucket, 64UL * 8UL) * 64UL;

    // std::shared_ptr<std::ostream> untrusted_memory_log = std::shared_ptr<std::ostream>(new std::ofstream(absl::StrFormat("level-%lu_untrusted_memory.log\n", recursive_level)));

    unique_memory_t untrusted_memory;
    if (fast_init) {
        untrusted_memory = BlockDiskMemoryLibAIO::create(absl::StrFormat("level-%lu_untrusted_memory", 0), parameters.untrusted_memory_size, page_size);
    } else {
        untrusted_memory = BackedMemory::create(absl::StrFormat("level-%lu_untrusted_memory", 0), parameters.untrusted_memory_size, page_size);
    }

    unique_memory_t position_map;
    // std::shared_ptr<std::ostream> position_map_log = std::shared_ptr<std::ostream>(new std::ofstream(absl::StrFormat("level-%lu_position_map.log\n", recursive_level)));
    if (recursive && position_map_size > max_position_map_size) {
        // position_map = createBinaryPathOram(position_map_size, block_size, blocks_per_bucket, max_position_map_size, recursive, 1);
        position_map = createBinaryPathOram2(
            position_map_size, position_map_page_size, 512, false, max_position_map_size, true, 1, max_load_factor, false, 1, crypto_module_name
        );
    } else {
        // position_map = BackedMemory::create(absl::StrFormat("level-%lu_position_map", 0), position_map_size, position_map_page_size);
        position_map = LinearScannedMemory::create(absl::StrFormat("level-%lu_position_map", 0), position_map_size, parameters.path_index_size);
    }

    // unique_memory_t bitfield = BitfieldAdapter::create("level-0_bitfield", BackedMemory::create("level-0_bitfield_memory", divide_round_up(parameters.valid_bitfield_size, 8UL), 64));

    unique_memory_t oram = PageOptimizedRAWOram::create(
        "page_optimized_raw_oram",
        std::move(position_map), std::move(untrusted_memory), 
        std::move(valid_bit_tree_controller), std::move(valid_bit_tree_memory),
        std::move(crypto_module),
        block_size, num_blocks, num_accesses_per_eviction, tree_order,
        stash_capacity,
        max_load_factor
    );

    std::cout << absl::StrFormat("level-%lu ORAM size %lu bytes, untrusted memory %lu bytes, postion map %lu bytes \n", 0, size, parameters.untrusted_memory_size, position_map_size);

    return oram;
    // return nullptr;
}

unique_memory_t createBinaryPathOram2(
    uint64_t size, uint64_t block_size, uint64_t page_size,
    bool is_page_size_strict,
    uint64_t max_position_map_size, bool recursive,
    uint64_t recursive_level, 
    double max_load_factor,
    bool fast_init,
    uint64_t levels_per_page,
    std::string_view crypto_module_name
) {
    // uint64_t num_blocks = divide_round_up(size, block_size);
    // uint64_t num_buckets = divide_round_up(num_blocks, blocks_per_bucket);
    // uint64_t levels = num_bits((num_buckets * 2) + 1);
    // uint64_t position_map_size = num_blocks * sizeof(uint64_t);

    std::unique_ptr<CryptoModule> crypto_module = get_crypto_module_by_name(crypto_module_name);

    auto parameters = BinaryPathOram2::compute_parameters(
        block_size, page_size, levels_per_page, divide_round_up(size, block_size),
        crypto_module.get(), max_load_factor, is_page_size_strict
    );

    uint64_t untrusted_memory_size = parameters.untrusted_memory_size;
    // std::shared_ptr<std::ostream> untrusted_memory_log = std::shared_ptr<std::ostream>(new std::ofstream(absl::StrFormat("level-%lu_untrusted_memory.log\n", recursive_level)));
    unique_memory_t untrusted_memory;

    if (fast_init) {
        untrusted_memory = BlockDiskMemoryLibAIO::create(absl::StrFormat("level-%lu_untrusted_memory", recursive_level), untrusted_memory_size, page_size);
    } else {
        untrusted_memory = BackedMemory::create(absl::StrFormat("level-%lu_untrusted_memory", recursive_level), untrusted_memory_size, page_size);
    }

    std::uint64_t position_map_page_size = 64;
    std::uint64_t num_position_map_entires_per_page = position_map_page_size / parameters.path_index_size;
    position_map_page_size = num_position_map_entires_per_page * parameters.path_index_size;
    std::cout << absl::StreamFormat("Each position map page of %lu bytes can hold %lu position map entry of %lu bytes\n", position_map_page_size, num_position_map_entires_per_page, parameters.path_index_size);
    std::uint64_t num_position_map_pages = divide_round_up(parameters.num_blocks, num_position_map_entires_per_page);
    uint64_t position_map_size = num_position_map_pages * position_map_page_size;
    std::cout << absl::StreamFormat("Position map needs %lu pages totaling %lu bytes to hold %lu entries\n", num_position_map_pages, position_map_size, parameters.num_blocks);
    

    unique_memory_t position_map;
    // std::shared_ptr<std::ostream> position_map_log = std::shared_ptr<std::ostream>(new std::ofstream(absl::StrFormat("level-%lu_position_map.log\n", recursive_level)));
    if (recursive && position_map_size > max_position_map_size) {
        position_map = createBinaryPathOram2(
            position_map_size,
            position_map_page_size,
            512, false,
            max_position_map_size, recursive,
            recursive_level + 1,
            0.75, false, 1, crypto_module_name);
    } else {
        // position_map = BackedMemory::create(absl::StrFormat("level-%lu_position_map", recursive_level), position_map_size, position_map_page_size);
        position_map = LinearScannedMemory::create(absl::StrFormat("level-%lu_position_map", 0), position_map_size, parameters.path_index_size);
    }

    std::string oram_name = absl::StrFormat("level-%lu_binary_path_oram_2", recursive_level);
    unique_memory_t oram = BinaryPathOram2::create(
        oram_name,
        std::move(position_map), std::move(untrusted_memory), 
        std::move(crypto_module),
        parameters,
        parameters.levels * 4,
        false
    );


    std::cout << absl::StrFormat("level-%lu ORAM size %lu bytes, untrusted memory %lu bytes, postion map %lu bytes \n", recursive_level, size, untrusted_memory_size, position_map_size);

    return oram;

}
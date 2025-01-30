from pathlib import Path
import itertools
from parse_size_str import parse_size_str
import pytomlpp

EXECUTABLE_LOCATION = Path("build/src/OramSimulator").absolute()
EXPERIMENT_FOLDER = Path("experiments").absolute()
ORAM_FOLDER = Path("orams").absolute()
TRACE_FOLDER = Path("input-traces").absolute()

def main():
    
    data_entries = []
    
    for oram_result_folder in itertools.chain(EXPERIMENT_FOLDER.glob("Recsys_Sim-*-*-*-*-*-*-*-*-*-*-*-*-*")):
        _, size_name, buffer_type, dataset, samples_per_round_str, num_rounds_str, oram_type, num_blocks_str, block_size_str, page_size_str, base_posmap_size_str, encryption_type, buffer_config, epsilon_str = oram_result_folder.parts[-1].split("-")
        num_blocks = parse_size_str(num_blocks_str)
        block_size = parse_size_str(block_size_str)
        page_size = parse_size_str(page_size_str)
        base_posmap_size = parse_size_str(base_posmap_size_str)
        samples_per_round = parse_size_str(samples_per_round_str)
        num_rounds = parse_size_str(num_rounds_str)
        epsilon = float(epsilon_str)
        
        # if num_blocks == 10_000_000:
        #     oram_config_name = "Small"
        # elif num_blocks == 50_000_000:
        #     oram_config_name = "Medium"
        # else:
        #     oram_config_name = "Large"
        
        # typo fix
        encryption_type.replace("ageis", "aegis")
            
        setup_string = f"{size_name}-{samples_per_round}-{num_rounds}-{buffer_type}"
        oram_display_name = f"{oram_type}-{num_blocks_str}-{block_size_str}-{page_size_str}-{base_posmap_size_str}-{encryption_type}"
    
        print()
        print(setup_string)
            
        main_stat_file = oram_result_folder / "recsys_sim-stat.toml"
        if not main_stat_file.exists():
            print(f"Skipping {oram_display_name}, {main_stat_file} does not exist.")
            continue
        main_stats = pytomlpp.load(main_stat_file)
            
        main_oram_stat_file = oram_result_folder / "page_optimized_raw_oram_stat.toml"
        
        if not main_oram_stat_file.exists():
            main_oram_stat_file = oram_result_folder / "level-0_binary_path_oram_2_stat.toml"
        main_oram_stats = pytomlpp.load(main_oram_stat_file)
        
        disk_read_time = main_oram_stats["path_read_ns"] / 1_000_000_000
        disk_write_time = main_oram_stats["path_write_ns"] / 1_000_000_000
        
        total_disk_time = disk_read_time + disk_write_time
            
        disk_memory_stat_file = oram_result_folder / "level-0_untrusted_memory_stat.toml"
        disk_memory_stats = pytomlpp.load(disk_memory_stat_file)
        
        oram_folder = ORAM_FOLDER / oram_display_name
        oram_config = pytomlpp.load(oram_folder / "config.toml")
        untrusted_memory_config = pytomlpp.load(oram_folder / "untrusted_memory/config.toml")

        num_rounds = main_stats["rounds"]
        print(f"{num_rounds=}")
        
        samples_per_round = main_stats["samples_per_round"]
        print(f"{samples_per_round=}")
            
        total_time = main_stats["overall_time_seconds"]
        print(f"{total_time=}")
        
        non_disk_time = total_time - total_disk_time
        
        time_per_round = total_time / num_rounds
        print(f"{time_per_round=}")
        
        buffer_time = main_stats["buffer_time_seconds"]
        print(f"{buffer_time=}")
        
        buffer_time_per_round = buffer_time / num_rounds
        print(f"{buffer_time_per_round=}")
        
        oram_time = main_stats["oram_time_seconds"]
        print(f"{oram_time=}")
        
        oram_time_per_round = oram_time / num_rounds
        print(f"{oram_time_per_round=}")
            
        bytes_written = disk_memory_stats["bytes_wrote"]
        bytes_written_per_round = bytes_written / num_rounds
            
        print(f"{bytes_written_per_round=}")
            
        bytes_read = disk_memory_stats["bytes_read"]
        bytes_read_per_round = bytes_read / num_rounds
            
        print(f"{bytes_read_per_round=}")

        if "k_sum" in main_stats:
            k_sum = main_stats["k_sum"]
            k_union_sum = main_stats["k_union_sum"]
            reuse_rate = 1 - k_union_sum / (samples_per_round * num_rounds)
        else:
            k_sum = 0
            k_union_sum = 0
            reuse_rate = 0
        
        if oram_type == "BinaryPathOram2":
            levels = parse_size_str(oram_config["levels"])
            total_buckets = 2**levels - 1
            levels_per_page = parse_size_str(oram_config["levels_per_page"])
            buckets_per_page = 2**levels_per_page - 1
            
            num_pages = (total_buckets + buckets_per_page - 1) // buckets_per_page
            
            enc_page_size = parse_size_str(oram_config["page_size"])
            
            main_tree_size = num_pages * enc_page_size
            
        else:
            main_tree_size = parse_size_str(untrusted_memory_config["size"])
        
        print(f"{main_tree_size=}")
        
            
        data_entries.append(
            (
                samples_per_round, size_name, buffer_type, dataset, num_rounds, 
                oram_type, num_blocks, block_size, page_size, base_posmap_size, encryption_type, 
                total_time, buffer_time, oram_time, bytes_read, bytes_written,
                time_per_round, buffer_time_per_round, oram_time_per_round, bytes_read_per_round, bytes_written_per_round, main_tree_size,
                disk_read_time, disk_write_time, total_disk_time, non_disk_time,
                k_sum, k_union_sum, reuse_rate
            )
        )

    with open("recsys_sim_results.csv", "w") as results:
        results.write(
            ", ".join((
                "Samples per Round", "ORAM Config", "Buffer Config", "Dataset", "Rounds",
                "Type", "Num Entires", "Entry Size", "Bucket Size", "Base Position Map Size", "Encryption",
                "Total Time", "Buffer Time", "ORAM Time","Total Bytes Read", "Total Bytes Wrote",
                "Time per Round", "Buffer Time per Round", "ORAM Time per Round", "Bytes Read per Access", "Bytes Wrote per Access", "Main Tree Size",
                "Disk Read Time", "Disk Write Time", "Disk Time", "Non-disk Time",
                "k sum", "k_union sum", "reuse rate"
            )) + "\n"
        )
        
        data_entries.sort(key=lambda e: (e[0], e[5], e[2]))
        
        for entry in data_entries:
            results.write(
                ", ".join(
                    map(str, entry)
                ) + "\n"
            )

if __name__ == "__main__":
    main()

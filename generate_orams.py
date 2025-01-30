import multiprocessing
import subprocess
import itertools
from pathlib import Path
import numpy as np
import math
import scipy
import sys
from typing import Tuple
import os

def overflow_condition_function(A, Z):
    return Z * np.log((2 * Z) / A) + A / 2 - Z - np.log(4)

def compute_max_a(Z):
    max_A = scipy.optimize.newton(func=overflow_condition_function, x0=1, args=(Z,))
    return math.floor(max_A)

BUILD_DIR = Path("build")

EXECUTABLE_LOCATION = BUILD_DIR / "src" / "OramSimulator"

ORAM_OUTPUT_DIR = Path("orams")

MAX_POSITION_MAP_SIZE = (32768,)

RETRY_LIMIT = 3

# place to run the ORAM from
ORAM_WORKING_DIR = Path(os.getenv("ORAM_WORKING_DIR", "."))


def generate_oram(type: str, num_blocks_block_size: Tuple[int, int], page_size: int,  max_position_map_size: int = 4 * 1024, levels_per_page: int = 1):
    num_blocks, block_size = num_blocks_block_size
    if levels_per_page <= 1:
        oram_display_name = f"{type}-{num_blocks // 1_000_000}M-{block_size}B-{page_size // 1024}Ki-{max_position_map_size // 1024}Ki"
    else:
        oram_display_name = f"{type}-{num_blocks // 1_000_000}M-{block_size}B-{page_size // (2**levels_per_page)}B-{max_position_map_size // 1024}Ki"
    Z_estimate = (page_size - 32) // (block_size + 16)
    a = compute_max_a(Z_estimate)
    print(f"A is set to {a}")
    oram_file_name = f"{oram_display_name}-aegis256"
    print(f"Generating {oram_file_name}")
    print(ORAM_OUTPUT_DIR / oram_file_name)
    size = num_blocks * block_size
    success = False
    
    if (ORAM_OUTPUT_DIR / oram_file_name).is_dir():
        print(f"{ORAM_OUTPUT_DIR / oram_file_name} already exists, skipping.")
        return
    
    for _ in range(RETRY_LIMIT):
        s = subprocess.run(
            [
                EXECUTABLE_LOCATION, 
                "create", 
                "--type", type,
                "--size", str(size),
                "--block_size", str(block_size),
                # "--blocks_per_bucket", str(blocks_per_bucket),
                "--page_size", str(page_size),
                "--position_map_size", str(max_position_map_size),
                "--output", ORAM_OUTPUT_DIR / oram_file_name,
                "--tree_order", str(2),
                "--num_accesses_per_eviction", str(a),
                "--load_factor", str(0.75),
                "--temp_dir", ORAM_WORKING_DIR,
                "--levels_per_page", str(levels_per_page),
                "--fast_init",
                "--crypto_module", "AEGIS256"
            ],
            # check=True,
            capture_output=False,
            encoding="utf-8"
        )
        print(" ".join(map(str, s.args)))
        
        if not s.returncode:
            success = True
            break
        else:
            print("OramSimulator Failed Retrying")
    
    if success:
        print(f"Finished {oram_file_name}")
    else:
        print(f"{oram_file_name} FAILED after {RETRY_LIMIT} retries!")


ORAM_CONFIGS = (
    # (ORAM Type, (Number of entries, Entry Size), Page Size, Base Pos Map Size, Levels Per Page)
    ("PageOptimizedRAWOram", (10_000_000, 64), 4096, 4096, 1),
    ("BinaryPathOram2", (10_000_000, 64), 4096, 4096, 4),
    ("PageOptimizedRAWOram", (50_000_000, 128), 4096, 4096, 1),
    ("BinaryPathOram2", (50_000_000, 128), 4096, 4096, 3),
    
    # uncomment the following line to include the large setup
    # this requires about 250GB of additional disk space
    # ("PageOptimizedRAWOram", (250_000_000, 256), 4096, 4096, 1),
    # ("BinaryPathOram2", (250_000_000, 256), 4096, 4096, 2),
)

def main():
    configs = ORAM_CONFIGS
    
    for config in configs:
        print(config)
    
    # make output dir if not exists
    if not ORAM_OUTPUT_DIR.is_dir():
        ORAM_OUTPUT_DIR.mkdir()
    

    with multiprocessing.Pool(processes=4) as pool:
        pool.starmap(generate_oram, configs)
    
    # for config in configs:
    #     generate_oram(*config)


if __name__ == "__main__":
    main()
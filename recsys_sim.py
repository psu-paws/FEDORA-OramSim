from pathlib import Path
import sys
import subprocess
import itertools
import shutil
import time
from typing import Optional
import os

SCRIPT_LOCATION = Path(sys.path[0])

BASE_DIR = Path(".")

EXECUTABLE_LOCATION = Path("build/src/OramSimulator").absolute()
EXPERIMENT_FOLDER = Path("experiments").absolute()
ORAM_FOLDER = Path("orams").absolute()
TRACE_FOLDER = Path("input-traces").absolute()

# place to run the ORAM from
ORAM_WORKING_DIR = Path(os.getenv("ORAM_WORKING_DIR", "."))

# shortened from 10M to 1M to reduce runtime of the experiment
NUM_ACCESSES = 1_000_000

ORAMS = {
    "Small": (
        "PageOptimizedRAWOram-10M-64B-4Ki-4Ki-aegis256",
        "BinaryPathOram2-10M-64B-256B-4Ki-aegis256",
        10_000,
        "distance_trace_10M_long.csv",
        "10M"
    ),
    "Medium": (
        "PageOptimizedRAWOram-50M-128B-4Ki-4Ki-aegis256",
        "BinaryPathOram2-50M-128B-512B-4Ki-aegis256",
        100_000,
        "distance_trace_50M_long.csv",
        "50M"
    ),
    "Large": (
        "PageOptimizedRAWOram-250M-256B-4Ki-4Ki-aegis256",
        "BinaryPathOram2-250M-256B-1024B-4Ki-aegis256",
        1_000_000,
        "distance_trace_250M_long.csv",
        "250M"
    )
}

RUN_CONFIGS = {
    "PathORAMWithBuffer" : (1, "ORAMBuffer3RAW", 0.0, False),
    "StrawmanSafe": (0, "ORAMBuffer3RAW", 0.0, False),
    "StrawmanUnsafe": (0, "ORAMBuffer3RAW", 0.0, True),
    "PosMap": (0, "ORAMBufferDP", 1, True),
    "LinearScannedPosMap": (0, "ORAMBufferDPLinearScanPosmap", 1, False)
}

def find_oram(oram_name: str) -> Optional[Path]:
    oram_location = ORAM_FOLDER / oram_name
    if not oram_location.exists():
        return None
    else:
        return oram_location


def main():
    configs = []

    for size in ("Small", "Medium", "Large"):
        for requests_per_round in (10_000, 100_000, 1_000_000):
            for config_name in ("PathORAMWithBuffer", "StrawmanSafe"):
                configs.append((size, requests_per_round, "kaggle", config_name))

            for dataset in "kaggle", "movielens_unpadded", "movielens_padded", "taobao_padded", "taobao_unpadded":
                configs.append((size, requests_per_round, dataset, "PosMap"))
        
                if requests_per_round <= 250_000:
                    configs.append((size, requests_per_round, dataset, "LinearScannedPosMap"))
        
    for config in configs:
        print(config)

    for size, requests_per_round, dataset, config_name in configs:
        oram_index, buffer_type, epsilon, enable_unsafe = RUN_CONFIGS[config_name]

        if "padded" in dataset:
            epsilon = epsilon / 100
        
        trace_file_name = f"{dataset}_synthetic_15M_{ORAMS[size][4]}.txt"
        oram_name =ORAMS[size][oram_index]
        oram_location = find_oram(oram_name)
        
        num_rounds = NUM_ACCESSES // requests_per_round
        display_name = f"{size}-{config_name}-{dataset}-{requests_per_round}-{num_rounds}-{oram_name}-{buffer_type}-{epsilon}"
        
        if oram_location is None:
            print(f"Skipping {display_name}: ORAM not found")
            continue

        trace_file = TRACE_FOLDER / trace_file_name

        assert(trace_file.is_file())

        
        
        
        run_folder = EXPERIMENT_FOLDER / f"Recsys_Sim-{display_name}"
            
        if run_folder.exists():
            if (run_folder / "recsys_sim-stat.toml").is_file():
                continue
            else:
                shutil.rmtree(run_folder)
        
        print(f"Starting sim of {display_name}")
            
        run_folder.mkdir(parents=True)

        cmd_arguments = (
                EXECUTABLE_LOCATION,
                "recsys_sim",
                "--memory", oram_location,
                "--rounds", str(num_rounds),
                "--samples_per_round", str(requests_per_round),
                "--sample_file", str(trace_file),
                "--buffer", buffer_type,
                f"--unsafe_optimization={enable_unsafe}",
                "--temp_dir", "/data2",
                "--output_file", run_folder/ f"recsys_sim-stat.toml",
                "--epsilon", str(epsilon),
                "--k_union", "16Ki"
            )
        
        with open(run_folder / "cmdargs", "w") as cmd_file:
            cmd_file.write(" ".join(map(str, cmd_arguments)))

            
        subprocess.run(
            cmd_arguments,
            cwd=run_folder
        )
            
        print(f"Completed sim of {display_name}")

if __name__ == "__main__":
    main()

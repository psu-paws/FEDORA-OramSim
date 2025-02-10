# Practical Federated Recommendation Model Learning Using ORAM with Controlled Privacy

FEDORA is a novel system for federated learning of recommendation models.sys allows each user to only download, train, and upload a small subset of the large tables based on their private data, while hiding the access pattern using oblivious memory (ORAM).
FEDORA reduces the ORAM's prohibitive latency and memory overheads by (1) introducing ε-FI, a formal way to balance the ORAM's privacy with performance, and (2) placing the large ORAM in a power- and cost-efficient SSD with SSD-friendly optimizations. Additionally, FEDORA is carefully designed to support (3) modern operation modes of FL.

## Hardware Requirements
Running FEDORA requires a modern x86-64 CPU with AVX2 support and an NVME SSD.

## Software Dependencies
 - GCC with C++20 support and CMake
 - Python 3.10
 - LibSodium
 - LibAIO

The program was tested in the following environment:
 - Ubuntu 22.04.5 LTS
 - GCC 11.4
 - CMake 3.4
 - Python 3.10.12

## Installation and Building

Clone the project from github, make sure to use the `--recurse-submodules` flag to pull down external dependencies.

Install libSodium, refer to instructions at https://doc.libsodium.org/installation

The program is built using cmake. A quick build script is provided at `./build.sh`. Otherwise the project can be built using the standard cmake process.

```bash
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo ..
make -j
```

Executables are located under the `build/src` directory after compilation.

Python dependencies can be installed using the `requirements.txt` by running `pip install -r requirement.txt`.

## Examples

### Creating ORAMs
The python script `generate_orams.py` generates a set of ORAM to use for testing. Please see `build/src/OramSimulator create --help` for description of options.

### Simple Access Simulation

```
build/src/OramSimulator run_trace --memory <path to ORAM Folder> --pattern Uniform --count 1M --verbose
```

This command will run 1M uniformly random accesses on the specified ORAM. Toml files containing statistics will be created in the current working directory. Please see `build/src/OramSimulator run_trace --help` for description of options.
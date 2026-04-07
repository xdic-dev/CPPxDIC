# CPPxDIC on HPC Clusters (Singularity/Apptainer + SLURM)

## Overview

This directory contains everything needed to build and run CPPxDIC on HPC clusters managed by SLURM, using Singularity/Apptainer containers to avoid dependency issues with EasyBuild modules.

**Key principle**: The binary is baked into the container image once. Configuration files are bind-mounted from the host at runtime — you never need to rebuild the container to change parameters.

## Directory Structure

```
cluster/
├── cppxdic.def          # Singularity/Apptainer definition file
├── submit_job.sh        # SLURM submission script
├── monitor_job.sh       # Job monitoring tool
├── configs/             # Host-side config files (edit freely, no rebuild)
│   ├── dic_params.txt
│   ├── ncorr_params.txt
│   └── visualization_params.txt
├── logs/                # Created automatically by SLURM (stdout/stderr)
└── README.md            # This file
```

## Quick Start

### 1. Build the container image (once)

Transfer the source code to the cluster, then build:

```bash
cd /path/to/CPPxDIC
apptainer build cluster/cppxdic.sif cluster/cppxdic.def
```

> **Note**: Building requires internet access (to pull the Ubuntu base image and install packages). If your cluster nodes have no internet, build on a login node or locally and `scp` the `.sif` file.
>
> Some clusters use `singularity` instead of `apptainer` — the commands are identical.

### 2. Edit configuration files

Edit the files in `cluster/configs/` to match your experiment. **Important path convention**:

| Host path | Container mount | Used in config as |
|-----------|----------------|-------------------|
| `DATA_DIR` (your input data) | `/data` | `base_path = /data` |
| `OUTPUT_DIR` (results) | `/output` | `dic_path = /output` |
| `CONFIG_DIR` (configs/) | `/configs` | (automatic) |

### 3. Submit a job

```bash
cd /path/to/CPPxDIC/cluster

# Basic submission
sbatch submit_job.sh

# With overrides
SUBJECT=S10 REFTRIAL=3 sbatch submit_job.sh

# With custom paths
DATA_DIR=/scratch/mydata OUTPUT_DIR=/scratch/myresults sbatch submit_job.sh

# With custom SLURM resources
sbatch --cpus-per-task=16 --mem=64G --time=48:00:00 submit_job.sh
```

### 4. Monitor execution

```bash
# List all your cppxdic jobs
./monitor_job.sh

# Detailed status of a specific job
./monitor_job.sh 123456

# Live-tail stdout (like tail -f)
./monitor_job.sh --tail 123456

# Continuous auto-refresh of job list
./monitor_job.sh --watch

# Summary of all completed jobs
./monitor_job.sh --summary
```

## Dependencies Included in Container

All dependencies from the project CMakeLists are installed in the container:

| Dependency | CMake usage | Ubuntu package |
|------------|------------|----------------|
| **OpenCV** (core, imgproc, imgcodecs, highgui, videoio) | `find_package(OpenCV)` | `libopencv-dev` |
| **FFTW3** | `find_library(fftw3)` | `libfftw3-dev` |
| **MATIO** (MATLAB .mat I/O) | `find_library(matio)` | `libmatio-dev` |
| **HDF5** (MAT v7.3) | `find_package(HDF5)` | `libhdf5-dev` |
| **CGAL** (Delaunay triangulation) | `find_package(CGAL)` | `libcgal-dev` |
| **Eigen3** (linear algebra) | `find_package(Eigen3)` | `libeigen3-dev` |
| **SuiteSparse** (spqr, cholmod, amd, colamd) | `find_library(spqr/cholmod/...)` | `libsuitesparse-dev` |
| **LAPACK** | `find_library(lapack)` | `liblapack-dev` |
| **BLAS / OpenBLAS** | `find_library(blas)` | `libblas-dev`, `libopenblas-dev` |
| **OpenMP** | `find_package(OpenMP)` | `libomp-dev` |
| **nlohmann/json** | `find_package(nlohmann_json)` | `nlohmann-json3-dev` |
| **pthreads** | `find_package(Threads)` | (built-in) |
| **CMake ≥ 3.16** | build system | `cmake` |
| **g++ (C++17)** | compiler | `g++` / `build-essential` |

## Advanced Usage

### Batch submissions (parameter sweeps)

```bash
#!/bin/bash
# submit_sweep.sh — submit multiple subjects
for SUBJ in S08 S09 S10 S11; do
    for TRIAL in 3 5 7; do
        SUBJECT=${SUBJ} REFTRIAL=${TRIAL} sbatch submit_job.sh
    done
done
```

### Using different config sets

```bash
# Create a config variant
cp -r configs configs_highres
# Edit configs_highres/dic_params.txt with different parameters

CONFIG_DIR=configs_highres sbatch submit_job.sh
```

### Interactive testing inside the container

```bash
apptainer shell \
    --bind /scratch/mydata:/data:ro \
    --bind /scratch/myresults:/output:rw \
    cppxdic.sif

# Now inside the container:
cppxdic --help
cppxdic --dic-params /data/my_params.txt
```

### Rebuilding the container after code changes

```bash
# Force rebuild (overwrites existing .sif)
apptainer build --force cppxdic.sif cppxdic.def
```

### Cluster-specific SLURM adjustments

Edit the `#SBATCH` directives in `submit_job.sh`:

```bash
#SBATCH --partition=gpu          # Use GPU partition
#SBATCH --gres=gpu:1             # Request 1 GPU
#SBATCH --account=my_project     # Billing account
#SBATCH --mail-type=END,FAIL     # Email notifications
#SBATCH --mail-user=me@univ.edu
```

## Troubleshooting

| Problem | Solution |
|---------|----------|
| `apptainer: command not found` | Try `singularity` or `module load apptainer` |
| Build fails with no internet | Build on login node or locally, then `scp *.sif` |
| `FATAL: container creation failed` | Check disk quota; `.sif` files can be 1-2 GB |
| Permission denied on `/output` | Ensure `OUTPUT_DIR` exists and is writable |
| OpenCV GUI errors | Set `showvisu = false` in `visualization_params.txt` |
| OOM kill | Increase `--mem` in SLURM or reduce frame range |
| Job pending forever | Check `scontrol show partition` for limits; try smaller resources |

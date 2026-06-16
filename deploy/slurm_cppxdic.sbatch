#!/bin/bash
# =============================================================================
# CPPxDIC — SLURM batch script for the camera-pairs pipeline.
#
# Submit from the repository root:
#   sbatch deploy/slurm_cppxdic.sbatch
#
# Adjust the SBATCH directives (partition, account, time, resources) to your cluster.
# CPPxDIC is shared-memory parallel (OpenMP via CppNCorr) — request cores on a SINGLE
# node, not MPI ranks. The thread count is bound to the allocated cores below.
# =============================================================================
#SBATCH --job-name=cppxdic
#SBATCH --output=cppxdic_%j.out
#SBATCH --error=cppxdic_%j.err
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=16
#SBATCH --mem=32G
#SBATCH --time=08:00:00
# #SBATCH --partition=compute      # uncomment + set for your cluster
# #SBATCH --account=your_account   # uncomment + set for your cluster

set -euo pipefail

# --- OpenMP threading ---------------------------------------------------------
# Match OpenMP threads to the cores SLURM allocated to this task.
export OMP_NUM_THREADS="${SLURM_CPUS_PER_TASK:-1}"
# Keep behaviour consistent with the repository default (see recent OMP fixes):
# spread threads across cores and do not nest parallel regions.
export OMP_PROC_BIND=spread
export OMP_PLACES=cores
export OMP_MAX_ACTIVE_LEVELS=1

# --- Build (skip if already built) -------------------------------------------
if [[ ! -x build/cppxdic ]]; then
    echo "[slurm] building cppxdic (camera-pairs) ..."
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DXDIC_MODE=camerapairs -DBUILD_TESTING=OFF
    cmake --build build -j"${SLURM_CPUS_PER_TASK:-4}" --target cppxdic
fi

# --- Run ----------------------------------------------------------------------
# Pass subject / trial / config as needed; defaults read config/default.cfg.
echo "[slurm] running cppxdic on ${OMP_NUM_THREADS} threads ..."
srun ./build/cppxdic --config config/default.cfg "$@"

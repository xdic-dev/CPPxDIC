#!/bin/bash
# =============================================================================
# Build the GPU-capable CPPxDIC image (cppxdic_gpu.sif) on MANNEBACK.
# CUDA compilation for 5 archs is heavy -> generous cpus/time.
# No GPU is needed at BUILD time (nvcc comes from the docker base image).
#
# Submit (from the repo root on Manneback):
#   sbatch deploy/cluster/cpugpu/build_sif.sbatch
# =============================================================================
#SBATCH --job-name=xdicgpu-build
#SBATCH --output=/home/ucl/inma/jaoga/devlab/MultiDIC/CPPxDIC/deploy/cluster/cpugpu/logs/%x_%j.out
#SBATCH --error=/home/ucl/inma/jaoga/devlab/MultiDIC/CPPxDIC/deploy/cluster/cpugpu/logs/%x_%j.err
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=16
#SBATCH --mem=48G
#SBATCH --time=02:00:00
set -euo pipefail

source /home/ucl/inma/jaoga/devlab/MultiDIC/CPPxDIC/deploy/cluster/cpugpu/env.sh
cd "$REPO"

echo "=== xdicgpu-build on $(hostname) @ $(date) ==="
echo "REPO=$REPO"
echo "DEF =$DEF"
echo "SIF =$SIF"
apptainer --version

# Node /tmp is too small to squash the ~10G nvidia/cuda rootfs (job 7862400
# died with ENOSPC in mksquashfs) -> stage the build on globalscratch.
export APPTAINER_TMPDIR="${BASE}/.apptainer_tmp_${SLURM_JOB_ID}"
mkdir -p "$APPTAINER_TMPDIR"
trap 'rm -rf "$APPTAINER_TMPDIR"' EXIT
echo "APPTAINER_TMPDIR=$APPTAINER_TMPDIR"
echo "APPTAINER_CACHEDIR=$APPTAINER_CACHEDIR"

# Sanity: the cuNCorr submodule must be present (the def copies the working tree).
[ -f "$REPO/Tools/cuNCorr/CMakeLists.txt" ] || { echo "ERROR: Tools/cuNCorr submodule missing"; exit 1; }

echo "=== building (fakeroot) ... this is the slow step ==="
apptainer build --fakeroot --force "$SIF" "$DEF"

echo "=== build done; container self-checks ==="
apptainer exec "$SIF" /opt/cppxdic/bin/cppxdic --help | head -5 || true
apptainer exec "$SIF" ldd /opt/cppxdic/bin/cppxdic | grep -i "cudart\|cuncorr" || true
apptainer exec "$SIF" which h5diff
ls -lh "$SIF"
echo "=== xdicgpu-build COMPLETE @ $(date) ==="

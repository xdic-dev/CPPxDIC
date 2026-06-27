#!/bin/bash
# =============================================================================
# Build the instrumented CPPxDIC profiling image (cppxdic_prof.sif).
# Runs on a compute node (NOT the login node). Compute nodes here have internet,
# which apt-get / the docker base pull require.
#
# Submit:  sbatch deploy/cluster/study/build_sif.sbatch
# =============================================================================
#SBATCH --job-name=xdic-build
#SBATCH --output=deploy/cluster/study/logs/%x_%j.out
#SBATCH --error=deploy/cluster/study/logs/%x_%j.err
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=8
#SBATCH --mem=32G
#SBATCH --time=00:30:00
set -euo pipefail

source /home/users/j/a/jaoga/devlab/MultiDIC/CPPxDIC/deploy/cluster/study/env.sh
cd "$REPO"

echo "=== xdic-build on $(hostname) @ $(date) ==="
echo "REPO=$REPO"
echo "DEF =$DEF"
echo "SIF =$SIF"
apptainer --version

# Build scratch: prefer fast node-local tmp, fall back to /tmp.
export APPTAINER_TMPDIR="${SLURM_TMPDIR:-/tmp}/apptainer_build_${SLURM_JOB_ID}"
mkdir -p "$APPTAINER_TMPDIR"
echo "APPTAINER_TMPDIR=$APPTAINER_TMPDIR"
echo "APPTAINER_CACHEDIR=$APPTAINER_CACHEDIR"

# Build from the repo root so the def's `%files . /opt/cppxdic/src` copies the
# local working tree (ja/profiling + force-frames patch).
echo "=== building (fakeroot) ... this is the slow step ==="
apptainer build --fakeroot --force "$SIF" "$DEF"

echo "=== build done; running container self-checks ==="
apptainer exec "$SIF" /opt/cppxdic/bin/cppxdic --help | head -5 || true
ls -lh "$SIF"
echo "=== xdic-build COMPLETE @ $(date) ==="

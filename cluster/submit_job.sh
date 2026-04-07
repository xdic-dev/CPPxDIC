#!/bin/bash
# =============================================================================
# SLURM submission script for CPPxDIC via Singularity/Apptainer
#
# Usage:
#   sbatch submit_job.sh
#   sbatch submit_job.sh --export=SUBJECT=S10,REFTRIAL=3
#   SUBJECT=S10 REFTRIAL=3 sbatch submit_job.sh
#
# The container image (cppxdic.sif) must be pre-built. Config files are
# bind-mounted from the host — no rebuild needed when changing parameters.
# =============================================================================

#SBATCH --job-name=cppxdic
#SBATCH --output=logs/cppxdic_%j.out
#SBATCH --error=logs/cppxdic_%j.err
#SBATCH --time=24:00:00
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=8
#SBATCH --mem=32G
#SBATCH --partition=batch
# Uncomment for GPU if needed:
# #SBATCH --gres=gpu:1

# =============================================================================
# USER-CONFIGURABLE PATHS — Edit these to match your cluster layout
# =============================================================================

# Path to the built container image
SIF_IMAGE="${SIF_IMAGE:-$(dirname "$0")/cppxdic.sif}"

# Project config files on the host (mounted read-only into the container)
CONFIG_DIR="${CONFIG_DIR:-$(dirname "$0")/configs}"

# Data directory on the host (where input images/videos live)
DATA_DIR="${DATA_DIR:-/scratch/${USER}/MultiDIC/Data}"

# Output directory on the host (where results are written)
OUTPUT_DIR="${OUTPUT_DIR:-/scratch/${USER}/MultiDIC/DIC_Output}"

# Optional overrides via environment or sbatch --export
SUBJECT="${SUBJECT:-}"
REFTRIAL="${REFTRIAL:-}"

# Config file names (inside CONFIG_DIR)
DIC_PARAMS="${DIC_PARAMS:-dic_params.txt}"
NCORR_PARAMS="${NCORR_PARAMS:-ncorr_params.txt}"
VIZ_PARAMS="${VIZ_PARAMS:-visualization_params.txt}"

# =============================================================================
# SETUP
# =============================================================================
echo "=============================================="
echo "CPPxDIC SLURM Job"
echo "=============================================="
echo "Job ID:        ${SLURM_JOB_ID}"
echo "Job Name:      ${SLURM_JOB_NAME}"
echo "Node:          $(hostname)"
echo "CPUs:          ${SLURM_CPUS_PER_TASK}"
echo "Memory:        ${SLURM_MEM_PER_NODE:-unknown} MB"
echo "Start time:    $(date '+%Y-%m-%d %H:%M:%S')"
echo "Container:     ${SIF_IMAGE}"
echo "Config dir:    ${CONFIG_DIR}"
echo "Data dir:      ${DATA_DIR}"
echo "Output dir:    ${OUTPUT_DIR}"
echo "=============================================="

# Create log and output directories
mkdir -p logs
mkdir -p "${OUTPUT_DIR}"

# Validate inputs
if [ ! -f "${SIF_IMAGE}" ]; then
    echo "ERROR: Container image not found: ${SIF_IMAGE}"
    echo "Build it first:  apptainer build cppxdic.sif cppxdic.def"
    exit 1
fi

if [ ! -d "${CONFIG_DIR}" ]; then
    echo "ERROR: Config directory not found: ${CONFIG_DIR}"
    echo "Create it and place dic_params.txt, ncorr_params.txt, visualization_params.txt inside."
    exit 1
fi

if [ ! -f "${CONFIG_DIR}/${DIC_PARAMS}" ]; then
    echo "ERROR: DIC params file not found: ${CONFIG_DIR}/${DIC_PARAMS}"
    exit 1
fi

# =============================================================================
# BUILD COMMAND
# =============================================================================

# Bind mounts:
#   /configs   -> config files (read-only)
#   /data      -> input data (read-only)
#   /output    -> output directory (read-write)
#   /tmp       -> scratch space

BIND_OPTS=""
BIND_OPTS="${BIND_OPTS} --bind ${CONFIG_DIR}:/configs:ro"
BIND_OPTS="${BIND_OPTS} --bind ${DATA_DIR}:/data:ro"
BIND_OPTS="${BIND_OPTS} --bind ${OUTPUT_DIR}:/output:rw"

# Use local scratch for tmp if available (common on HPC)
if [ -d "/local/scratch/${SLURM_JOB_ID}" ]; then
    BIND_OPTS="${BIND_OPTS} --bind /local/scratch/${SLURM_JOB_ID}:/tmp"
elif [ -d "/tmp" ]; then
    BIND_OPTS="${BIND_OPTS} --bind /tmp:/tmp"
fi

# Build cppxdic arguments
CPPXDIC_ARGS=""
CPPXDIC_ARGS="${CPPXDIC_ARGS} --dic-params /configs/${DIC_PARAMS}"
CPPXDIC_ARGS="${CPPXDIC_ARGS} --ncorr-params /configs/${NCORR_PARAMS}"
CPPXDIC_ARGS="${CPPXDIC_ARGS} --viz-params /configs/${VIZ_PARAMS}"

if [ -n "${SUBJECT}" ]; then
    CPPXDIC_ARGS="${CPPXDIC_ARGS} --subject ${SUBJECT}"
    echo "Subject override: ${SUBJECT}"
fi

if [ -n "${REFTRIAL}" ]; then
    CPPXDIC_ARGS="${CPPXDIC_ARGS} --reftrial ${REFTRIAL}"
    echo "Reftrial override: ${REFTRIAL}"
fi

# Set OpenMP threads to match SLURM allocation
export OMP_NUM_THREADS=${SLURM_CPUS_PER_TASK:-4}
export OMP_PROC_BIND=close
export OMP_PLACES=cores

echo ""
echo "Running: apptainer run ${BIND_OPTS} ${SIF_IMAGE} ${CPPXDIC_ARGS}"
echo "OMP_NUM_THREADS=${OMP_NUM_THREADS}"
echo "=============================================="
echo ""

# =============================================================================
# RUN
# =============================================================================
START_TS=$(date +%s)

apptainer run \
    --cleanenv \
    --env OMP_NUM_THREADS=${OMP_NUM_THREADS} \
    --env OMP_PROC_BIND=${OMP_PROC_BIND} \
    --env OMP_PLACES=${OMP_PLACES} \
    ${BIND_OPTS} \
    "${SIF_IMAGE}" \
    ${CPPXDIC_ARGS}

EXIT_CODE=$?
END_TS=$(date +%s)
ELAPSED=$((END_TS - START_TS))

echo ""
echo "=============================================="
echo "Job finished:  $(date '+%Y-%m-%d %H:%M:%S')"
echo "Exit code:     ${EXIT_CODE}"
echo "Elapsed time:  $((ELAPSED / 3600))h $(( (ELAPSED % 3600) / 60 ))m $((ELAPSED % 60))s"
echo "=============================================="

exit ${EXIT_CODE}

#!/bin/bash
# =============================================================================
# SLURM submission script for CPPxDIC via Singularity/Apptainer
# — variant for cppxdic_mirrored.sif (built from deploy/cluster/cppxdic_mirrored.def) —
#
# cppxdic_mirrored.sif's cppxdic binary was compiled with -DXDIC_MODE=mirrored
# instead of the default camerapairs, so `cppxdic` here does mirrored-camera
# 3D-DIC reconstruction. XDIC_MODE is a CMake compile-time cache variable
# (one binary = one mode), so this is a SEPARATE image from cppxdic.sif, not a
# runtime flag. Only `cppxdic` is mode-sensitive: `singledic` hardcodes
# XDIC_MODE_CAMERAPAIRS regardless (see CMakeLists.txt), so APP=singledic on
# this image behaves exactly like on cppxdic.sif.
#
# This image DOES use the entrypoint.sh app-dispatcher (unlike
# cppxdic_prof.sif), so `apptainer run <image> <app> ...` works normally here
# — this script mirrors submit_job.sh's structure. See submit_job_prof.sh for
# the apptainer-exec variant needed by the profiling image.
#
# Usage:
#   sbatch submit_job_mirrored.sh
#   sbatch submit_job_mirrored.sh --export=SUBJECT=S10,REFTRIAL=3
#   SUBJECT=S10 REFTRIAL=3 sbatch submit_job_mirrored.sh
#
# The container image (cppxdic_mirrored.sif) must be pre-built:
#   apptainer build --fakeroot cppxdic_mirrored.sif deploy/cluster/cppxdic_mirrored.def
# Config files are bind-mounted from the host — no rebuild needed when
# changing parameters.
# =============================================================================

#SBATCH --job-name=cppxdic-mirrored
#SBATCH --output=logs/cppxdic_mirrored_%j.out
#SBATCH --error=logs/cppxdic_mirrored_%j.err
#SBATCH --time=24:00:00
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=8
#SBATCH --mem=32G
#SBATCH --partition=batch

# =============================================================================
# USER-CONFIGURABLE PATHS — Edit these to match your cluster layout
# =============================================================================

# Path to the built container image
SIF_IMAGE="${SIF_IMAGE:-$(dirname "$0")/cppxdic_mirrored.sif}"

# Project config files on the host (mounted read-only into the container)
CONFIG_DIR="${CONFIG_DIR:-$(dirname "$0")/configs}"

# Data directory on the host (where input images/videos live)
DATA_DIR="${DATA_DIR:-/scratch/${USER}/MultiDIC/Data}"

# Output directory on the host (where results are written)
OUTPUT_DIR="${OUTPUT_DIR:-/scratch/${USER}/MultiDIC/DIC_Output}"

# Which app in the image to run (default cppxdic, i.e. mirrored-mode
# reconstruction). One of:
#   cppxdic singledic proxyncorr gen_subject_trial
# NOTE: only "cppxdic" is mirrored here; singledic/proxyncorr are unaffected
# by XDIC_MODE and behave the same as on cppxdic.sif.
APP="${APP:-cppxdic}"

# Extra arguments for non-cppxdic apps (word-split; cppxdic ignores this and uses
# the config/SUBJECT/REFTRIAL knobs below). Example:
#   APP=singledic APP_ARGS="--data-path /data --dic-path /output --subject S17 \
#       --bloc bloc1 --trial vid_23 --reftrial vid_5 --dic-params /configs/dic_params.txt"
APP_ARGS="${APP_ARGS:-}"

# Optional cppxdic overrides via environment or sbatch --export
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
echo "CPPxDIC SLURM Job (mirrored-mode image)"
echo "=============================================="
echo "Job ID:        ${SLURM_JOB_ID}"
echo "Job Name:      ${SLURM_JOB_NAME}"
echo "Node:          $(hostname)"
echo "CPUs:          ${SLURM_CPUS_PER_TASK}"
echo "Memory:        ${SLURM_MEM_PER_NODE:-unknown} MB"
echo "Start time:    $(date '+%Y-%m-%d %H:%M:%S')"
echo "App:           ${APP}"
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
    echo "Build it first:  apptainer build --fakeroot cppxdic_mirrored.sif deploy/cluster/cppxdic_mirrored.def"
    exit 1
fi

if [ ! -d "${CONFIG_DIR}" ]; then
    echo "ERROR: Config directory not found: ${CONFIG_DIR}"
    echo "Create it and place dic_params.txt, ncorr_params.txt, visualization_params.txt inside."
    exit 1
fi

# cppxdic requires the DIC params file; other apps take their own args via APP_ARGS.
if [ "${APP}" = "cppxdic" ] && [ ! -f "${CONFIG_DIR}/${DIC_PARAMS}" ]; then
    echo "ERROR: DIC params file not found: ${CONFIG_DIR}/${DIC_PARAMS}"
    exit 1
fi

if [ "${APP}" != "cppxdic" ] && [ -z "${APP_ARGS}" ]; then
    echo "WARN: APP=${APP} but APP_ARGS is empty; the app will run with no arguments."
fi

if [ "${APP}" != "cppxdic" ]; then
    echo "NOTE: APP=${APP} does not honor mirrored mode (XDIC_MODE only affects"
    echo "      the cppxdic binary) -- this run is equivalent to cppxdic.sif."
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

# Build the in-container argument list. The first token is the app name, which
# the image runscript (entrypoint.sh dispatcher) uses to select the binary.
CPPXDIC_ARGS="${APP}"

if [ "${APP}" = "cppxdic" ]; then
    # cppxdic: inject the bind-mounted config files + optional overrides.
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
else
    # Other apps: forward APP_ARGS verbatim (word-split). They reference the
    # /configs, /data and /output mounts directly via their own flags.
    CPPXDIC_ARGS="${CPPXDIC_ARGS} ${APP_ARGS}"
fi

# Set OpenMP threads to match SLURM allocation
export OMP_NUM_THREADS=${SLURM_CPUS_PER_TASK:-4}
# Spread threads across cores/sockets to avoid piling onto a single L3/NUMA node.
export OMP_PROC_BIND=spread
export OMP_PLACES=cores
# Disable nested OpenMP: the per-frame DIC kernels already use #pragma omp
# parallel internally; nested would oversubscribe N*N threads.
export OMP_NESTED=false
export OMP_MAX_ACTIVE_LEVELS=1
export OMP_DYNAMIC=false

echo ""
echo "Running: apptainer run ${BIND_OPTS} ${SIF_IMAGE} ${CPPXDIC_ARGS}"
echo "OMP_NUM_THREADS=${OMP_NUM_THREADS}"
echo "OMP_PROC_BIND=${OMP_PROC_BIND}  OMP_PLACES=${OMP_PLACES}"
echo "OMP_NESTED=${OMP_NESTED}  OMP_MAX_ACTIVE_LEVELS=${OMP_MAX_ACTIVE_LEVELS}"
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
    --env OMP_NESTED=${OMP_NESTED} \
    --env OMP_MAX_ACTIVE_LEVELS=${OMP_MAX_ACTIVE_LEVELS} \
    --env OMP_DYNAMIC=${OMP_DYNAMIC} \
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

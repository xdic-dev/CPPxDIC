#!/bin/bash
# =============================================================================
# Helper script to run CPPxDIC via Docker
#
# Usage:
#   ./docker/run.sh
#   ./docker/run.sh --subject S10 --reftrial 3
#
#   Environment variables (override defaults):
#     CONFIG_DIR   Path to config files on the host  (default: docker/configs)
#     DATA_DIR     Path to input data on the host
#     OUTPUT_DIR   Path to output directory on the host
#     OMP_NUM_THREADS  Number of OpenMP threads       (default: 4)
#     IMAGE        Docker image name                  (default: cppxdic:latest)
# =============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# Defaults
IMAGE="${IMAGE:-cppxdic:latest}"
CONFIG_DIR="${CONFIG_DIR:-${SCRIPT_DIR}/configs}"
DATA_DIR="${DATA_DIR:-}"
OUTPUT_DIR="${OUTPUT_DIR:-${SCRIPT_DIR}/_output}"
OMP_NUM_THREADS="${OMP_NUM_THREADS:-4}"
SUBJECT="${SUBJECT:-}"
REFTRIAL="${REFTRIAL:-}"

# Build image if not present
if ! docker image inspect "${IMAGE}" >/dev/null 2>&1; then
    echo "Image '${IMAGE}' not found. Building..."
    docker build -t "${IMAGE}" -f "${SCRIPT_DIR}/Dockerfile" "${PROJECT_ROOT}"
fi

# Validate
if [ ! -d "${CONFIG_DIR}" ]; then
    echo "ERROR: Config directory not found: ${CONFIG_DIR}"
    echo "Create it and place dic_params.txt, ncorr_params.txt, visualization_params.txt inside."
    exit 1
fi

mkdir -p "${OUTPUT_DIR}"

# Build docker run arguments
DOCKER_ARGS=(
    --rm
    -v "${CONFIG_DIR}:/configs:ro"
    -v "${OUTPUT_DIR}:/output"
    -e "OMP_NUM_THREADS=${OMP_NUM_THREADS}"
    -e "OMP_PROC_BIND=close"
    -e "OMP_PLACES=cores"
)

if [ -n "${DATA_DIR}" ]; then
    DOCKER_ARGS+=(-v "${DATA_DIR}:/data:ro")
fi

# Build cppxdic arguments
CPPXDIC_ARGS=(
    --dic-params /configs/dic_params.txt
    --ncorr-params /configs/ncorr_params.txt
    --viz-params /configs/visualization_params.txt
)

if [ -n "${SUBJECT}" ]; then
    CPPXDIC_ARGS+=(--subject "${SUBJECT}")
fi

if [ -n "${REFTRIAL}" ]; then
    CPPXDIC_ARGS+=(--reftrial "${REFTRIAL}")
fi

# Append any extra arguments passed to this script
CPPXDIC_ARGS+=("$@")

echo "=============================================="
echo "CPPxDIC Docker Run"
echo "=============================================="
echo "Image:         ${IMAGE}"
echo "Config dir:    ${CONFIG_DIR}"
echo "Data dir:      ${DATA_DIR:-<not set>}"
echo "Output dir:    ${OUTPUT_DIR}"
echo "OMP_THREADS:   ${OMP_NUM_THREADS}"
echo "Extra args:    $*"
echo "=============================================="
echo ""

docker run "${DOCKER_ARGS[@]}" "${IMAGE}" "${CPPXDIC_ARGS[@]}"

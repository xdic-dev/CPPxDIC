#!/bin/bash
# =============================================================================
# Helper script to run CPPxDIC via Docker
#
# Usage:
#   ./docker/run.sh
#   ./docker/run.sh --subject S10 --reftrial 3
#
#   Run a different app baked into the image (default cppxdic):
#     APP=singledic ./docker/run.sh --subject S17 --bloc bloc1 --trial vid_23 ...
#     APP=proxyncorr ./docker/run.sh --folder /data/imgs --output /output/run1
#     APP=gen_subject_trial ./docker/run.sh --subjects S08,S09 -o /output/st.csv
#     APP=xdic_stepsABC ./docker/run.sh stepc --object /data/obj.txt ...
#   List the apps in the image:
#     ./docker/run.sh --list-apps
#
#   Environment variables (override defaults):
#     APP          App to run: cppxdic (default), singledic, proxyncorr,
#                  gen_subject_trial, xdic_stepsABC
#     CONFIG_DIR   Path to config files on the host  (default: docker/configs)
#     DATA_DIR     Path to input data on the host
#     OUTPUT_DIR   Path to output directory on the host
#     OMP_NUM_THREADS  Number of OpenMP threads       (default: 4)
#     IMAGE        Docker image name                  (default: cppxdic:latest)
#     SUBJECT / REFTRIAL  cppxdic-only convenience overrides (ignored for other apps)
# =============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

# Defaults
APP="${APP:-cppxdic}"
IMAGE="${IMAGE:-cppxdic:latest}"
CONFIG_DIR="${CONFIG_DIR:-${SCRIPT_DIR}/configs}"
DATA_DIR="${DATA_DIR:-}"
OUTPUT_DIR="${OUTPUT_DIR:-${SCRIPT_DIR}/_output}"
OMP_NUM_THREADS="${OMP_NUM_THREADS:-4}"
SUBJECT="${SUBJECT:-}"
REFTRIAL="${REFTRIAL:-}"

# `./docker/run.sh --list-apps` just asks the image what it contains.
if [ "${1:-}" = "--list-apps" ]; then
    docker run --rm "${IMAGE}" --list-apps
    exit 0
fi

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

# Build the in-container argument list. The first token is the app name, which
# the image entrypoint uses to select the binary.
APP_ARGS=("${APP}")

if [ "${APP}" = "cppxdic" ]; then
    # cppxdic: auto-inject the bind-mounted config files + optional overrides.
    APP_ARGS+=(
        --dic-params /configs/dic_params.txt
        --ncorr-params /configs/ncorr_params.txt
        --viz-params /configs/visualization_params.txt
    )
    [ -n "${SUBJECT}" ]  && APP_ARGS+=(--subject "${SUBJECT}")
    [ -n "${REFTRIAL}" ] && APP_ARGS+=(--reftrial "${REFTRIAL}")
fi
# For every app, append whatever the caller passed. Other apps take their own
# flags (e.g. --data-path, --folder, --object) which reference /configs, /data,
# /output — the same mounts set up below.
APP_ARGS+=("$@")

echo "=============================================="
echo "CPPxDIC Docker Run"
echo "=============================================="
echo "App:           ${APP}"
echo "Image:         ${IMAGE}"
echo "Config dir:    ${CONFIG_DIR}"
echo "Data dir:      ${DATA_DIR:-<not set>}"
echo "Output dir:    ${OUTPUT_DIR}"
echo "OMP_THREADS:   ${OMP_NUM_THREADS}"
echo "Extra args:    $*"
echo "=============================================="
echo ""

docker run "${DOCKER_ARGS[@]}" "${IMAGE}" "${APP_ARGS[@]}"

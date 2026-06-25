#!/usr/bin/env bash
# =============================================================================
# CPPxDIC container entrypoint / app dispatcher
#
# The deploy image bakes in several binaries under /opt/cppxdic/bin:
#   cppxdic            default camera-pairs 3D-DIC pipeline
#   singledic          single-camera 2D DIC (no stereo / no 3D)
#   proxyncorr         pass-through DIC driver over an image folder
#   gen_subject_trial  generate subject_trial.csv for SLURM array jobs
#   xdic_stepsABC      MASK+Seed setup (headless) and StepC DLT calibration
#
# Selecting the app:
#   - If the FIRST argument is one of the known app names, that binary is run
#     with the REMAINING arguments:   <entrypoint> singledic --subject S17 ...
#   - Otherwise the arguments are forwarded to the default app (cppxdic), so
#     legacy invocations keep working: <entrypoint> --dic-params /configs/...
#
# This script is installed as the Docker ENTRYPOINT and invoked by the
# Apptainer %runscript, so both deployment paths share one dispatch contract.
# =============================================================================
set -euo pipefail

BIN_DIR="${CPPXDIC_BIN_DIR:-/opt/cppxdic/bin}"
DEFAULT_APP="cppxdic"
KNOWN_APPS="cppxdic singledic proxyncorr gen_subject_trial xdic_stepsABC generate_ncorr_bin"

if [ "$#" -gt 0 ]; then
    # Convenience: list the apps actually built into this image.
    case "$1" in
        --list-apps|list-apps)
            echo "Apps available in this image (under ${BIN_DIR}):"
            for app in ${KNOWN_APPS}; do
                [ -x "${BIN_DIR}/${app}" ] && echo "  ${app}"
            done
            exit 0
            ;;
    esac

    for app in ${KNOWN_APPS}; do
        if [ "$1" = "${app}" ]; then
            if [ ! -x "${BIN_DIR}/${app}" ]; then
                echo "ERROR: '${app}' is not built into this image." >&2
                echo "Rebuild with -DBUILD_<APP>=ON (see deploy/README.md)." >&2
                exit 127
            fi
            shift
            exec "${BIN_DIR}/${app}" "$@"
        fi
    done
fi

# No app name given -> default app, forwarding all arguments unchanged.
exec "${BIN_DIR}/${DEFAULT_APP}" "$@"

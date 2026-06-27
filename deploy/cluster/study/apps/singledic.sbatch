#!/bin/bash
# =============================================================================
# TEST: singledic — single-camera 2D DIC on ONE S09 camera video.
# singledic expects a video at <data_path>/vid/<subject>/<bloc>/<trial>[.mp4],
# which differs from cppxdic's rawdata/speckles layout, so we ADAPT the S09 data
# by symlinking one camera's trial video into that shape.
# =============================================================================
#SBATCH --job-name=xdic-single
#SBATCH --output=deploy/cluster/study/apps/logs/single_%j.out
#SBATCH --error=deploy/cluster/study/apps/logs/single_%j.err
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=32
#SBATCH --mem=64G
#SBATCH --time=06:00:00
set -uo pipefail
source /home/users/j/a/jaoga/devlab/MultiDIC/CPPxDIC/deploy/cluster/study/env.sh
cd "$REPO"
mkdir -p deploy/cluster/study/apps/logs

# 141-frame window (10-150) to match the other app tests for comparison.
TRIAL="${TRIAL:-7}"; CAM="${CAM:-cam_1}"; START="${START:-10}"; END="${END:-150}"
ROOT="${RUNS_DIR}/apps/singledic"
SD_DATA="$ROOT/data"; OUT="$ROOT/out"
rm -rf "$ROOT"; mkdir -p "$SD_DATA/vid/$SUBJECT/$MATERIAL_DIR" "$OUT" "$ROOT/logs"

# Adapt S09: symlink the chosen camera's trial video as <trial>.mp4.
SRC=$(ls "$DATA_PARENT/rawdata/$SUBJECT/speckles/$MATERIAL_DIR/vid/"*_$(printf '%03d' "$TRIAL")_*_${CAM}.mp4 2>/dev/null | head -1)
[ -n "$SRC" ] || { echo "ERROR: no video for trial=$TRIAL cam=$CAM"; exit 1; }
ln -sf "$SRC" "$SD_DATA/vid/$SUBJECT/$MATERIAL_DIR/${TRIAL}.mp4"
echo "singledic input: $SD_DATA/vid/$SUBJECT/$MATERIAL_DIR/${TRIAL}.mp4 -> $(basename "$SRC")"

# Config with 32-thread parallel DIC.
cp "$CONFIGS/dic_params.txt" "$ROOT/dic_params.txt"
cat >> "$ROOT/dic_params.txt" <<CFG

parallel_processing = true
step_d_total_threads = 32
idx_frame_start = $START
idx_frame_end = $END
frame_jump = 1
CFG

START_T=$(date +%s)
apptainer exec --cleanenv \
    --env XDIC_PARALLEL_SEEDOPT=1 \
    --env OMP_NUM_THREADS="$SLURM_CPUS_PER_TASK" \
    --env OMP_PROC_BIND=spread --env OMP_PLACES=cores --env OMP_MAX_ACTIVE_LEVELS=1 \
    -B "$BIND_ROOT":"$BIND_ROOT" \
    "$SIF" \
    /opt/cppxdic/bin/singledic \
        --data-path "$SD_DATA" --dic-path "$OUT" \
        --subject "$SUBJECT" --bloc "$MATERIAL_DIR" --trial "$TRIAL" --reftrial "$TRIAL" \
        --start "$START" --end "$END" --jump 1 --dir forward --ncorr-number 1 \
        --dic-params "$ROOT/dic_params.txt" \
    > "$ROOT/logs/singledic.log" 2>&1
RC=$?
WALL=$(( $(date +%s) - START_T ))
echo "[singledic] rc=$RC wall=${WALL}s"
echo "--- log tail ---"; tail -20 "$ROOT/logs/singledic.log"
echo "--- outputs ---"; find "$OUT" -name "ncorr*.csv" -o -name "ncorr*.mat" 2>/dev/null | sed 's/^/  /'
exit $RC

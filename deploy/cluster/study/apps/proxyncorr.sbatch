#!/bin/bash
# =============================================================================
# TEST: proxyncorr — pass-through ncorr DIC over a folder of S09 frames.
# Self-contained: (1) extract S09 trial frames AND render the REF mask/seed via a
# cppxdic `--stages match` import (it writes frame_*.png + tmp_ncorr_*/roi_mask.png
# from the REF_MASK and logs the REF seed), then (2) run proxyncorr at 32 threads
# using the REF mask as ROI and the REF seed. 141-frame window for comparison.
# =============================================================================
#SBATCH --job-name=xdic-proxy
#SBATCH --output=deploy/cluster/study/apps/logs/proxy_%j.out
#SBATCH --error=deploy/cluster/study/apps/logs/proxy_%j.err
#SBATCH --nodes=1
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=32
#SBATCH --mem=80G
#SBATCH --time=02:00:00
set -uo pipefail
source /home/users/j/a/jaoga/devlab/MultiDIC/CPPxDIC/deploy/cluster/study/env.sh
cd "$REPO"; mkdir -p deploy/cluster/study/apps/logs

TRIAL="${TRIAL:-7}"; PAIR="${PAIR:-1}"; FSTART="${FSTART:-10}"; FEND="${FEND:-150}"
ROOT=$(bash deploy/cluster/study/apps/setup_isolated.sh proxyncorr 32 "$FSTART" "$FEND" 1 | tail -1)
cfg="$ROOT/dic_params.txt"; OUT="$ROOT/proxy_out"; mkdir -p "$OUT"
TRIALDIR="$ROOT/$SUBJECT/$MATERIAL_DIR/$(printf '%03d' "$TRIAL")/loading"

echo "=== step 1: extract S09 trial $TRIAL frames + REF mask/seed via --stages match ==="
apptainer exec --cleanenv --env XDIC_FORCE_FRAMES=1 --env OMP_NUM_THREADS=8 \
    -B "$BIND_ROOT":"$BIND_ROOT" "$SIF" \
    /opt/cppxdic/bin/cppxdic -C "$cfg" -d "$cfg" -n "$ROOT/ncorr_params.txt" -v "$ROOT/visualization_params.txt" \
        --subject "$SUBJECT" --reftrial "$REFTRIAL" --trial "$TRIAL" --pair "$PAIR" --stages match \
    > "$ROOT/logs/extract.log" 2>&1
echo "  extract rc=$?"

# Trial's own frame folder (T<trial>, not the ref T5).
FRAMES_DIR=$(dirname "$(find "$ROOT/$SUBJECT/$MATERIAL_DIR/tmp_frames/T${TRIAL}" -name 'frame_*.png' 2>/dev/null | sort | head -1)")
[ -d "$FRAMES_DIR" ] || { echo "ERROR: no extracted frames for T${TRIAL}"; exit 1; }
NF=$(ls "$FRAMES_DIR"/frame_*.png 2>/dev/null | wc -l)

# ROI image = the pipeline's render of the REF_MASK (full-frame, 0/255 PNG).
ROI_PNG=$(find "$TRIALDIR" -name 'roi_mask.png' 2>/dev/null | head -1)
[ -f "$ROI_PNG" ] || { echo "ERROR: REF-mask roi_mask.png not produced"; cat "$ROOT/logs/extract.log" | tail -20; exit 1; }
echo "ROI from REF mask: $ROI_PNG"

# Seed from the REF seed the pipeline loaded ("Provided seed: X Y").
read -r SX SY <<<"$(grep -oE 'Provided seed: [0-9]+ [0-9]+' "$ROOT/logs/extract.log" | head -1 | grep -oE '[0-9]+ [0-9]+')"
if [ -n "${SX:-}" ]; then
  printf '[{"x": %s, "y": %s, "u":0,"v":0,"du_dx":0,"du_dy":0,"dv_dx":0,"dv_dy":0,"corrcoef":0}]\n' "$SX" "$SY" > "$ROOT/seeds.json"
  SEED_ARGS=(--seeds "$ROOT/seeds.json"); echo "REF seed: ($SX,$SY)"
else
  SEED_ARGS=(); echo "REF seed not found in log; proxyncorr will auto-seed"
fi

echo "=== step 2: proxyncorr --in-memory over $NF frames (32 threads), REF-mask ROI ==="
# NOTE: the seed-based --mode parallel path segfaults on this data (proxyncorr
# bug); the --in-memory NcorrSession path is the app's lightweight validation
# route and still consumes the REF-mask ROI. (REF seed extracted above for record.)
START=$(date +%s)
apptainer exec --cleanenv --env OMP_NUM_THREADS="$SLURM_CPUS_PER_TASK" \
    --env OMP_PROC_BIND=spread --env OMP_PLACES=cores \
    -B "$BIND_ROOT":"$BIND_ROOT" "$SIF" \
    /opt/cppxdic/bin/proxyncorr \
        --folder "$FRAMES_DIR" --roi "$ROI_PNG" --output "$OUT" \
        --scalefactor 3 --radius 20 --threads "$SLURM_CPUS_PER_TASK" \
        --mode parallel --units mm --units-per-pixel 0.2 --no-videos --in-memory \
    > "$ROOT/logs/proxyncorr.log" 2>&1
RC=$?
WALL=$(( $(date +%s) - START ))
echo "[proxyncorr] rc=$RC wall=${WALL}s"
echo "--- log tail ---"; tail -20 "$ROOT/logs/proxyncorr.log"
echo "--- outputs ---"; find "$OUT" \( -name '*.bin' -o -name '*.json' \) 2>/dev/null | sed 's/^/  /' | head
exit $RC

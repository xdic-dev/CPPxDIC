#!/bin/bash
# =============================================================================
# Drive the staged pipeline (match -> track -> format,e,f) over S09 trials in an
# ISOLATED dic_path, as 3 dependent SLURM arrays. Tracking runs at 32 threads.
#   usage: TRIALS="7 12 25" PAIRS="1 2" ./staged_run.sh
# =============================================================================
set -euo pipefail
cd "$(dirname "$0")/../../../.."        # repo root
source deploy/cluster/study/env.sh
SD=deploy/cluster/study/apps

TRIALS="${TRIALS:-7 12 25}"; PAIRS="${PAIRS:-1 2}"
# cameras per pair (S09 coating): pair1 -> 1,2 ; pair2 -> 4,3
declare -A CAMS=( [1]="1 2" [2]="4 3" )

# 141-frame window (10-150 j1) to match the other app tests for comparison.
ROOT=$(bash "$SD/setup_isolated.sh" staged 32 10 150 1 | tail -1)
echo "ROOT=$ROOT"
M="$ROOT"
: > "$M/manifest_match.txt"; : > "$M/manifest_track.txt"; : > "$M/manifest_ef.txt"
for t in $TRIALS; do
  echo "$t" >> "$M/manifest_ef.txt"
  for p in $PAIRS; do
    echo "$t $p" >> "$M/manifest_match.txt"
    for c in ${CAMS[$p]}; do echo "$t $p $c" >> "$M/manifest_track.txt"; done
  done
done
N1=$(wc -l < "$M/manifest_match.txt"); N2=$(wc -l < "$M/manifest_track.txt"); N3=$(wc -l < "$M/manifest_ef.txt")
echo "manifests: match=$N1 (trial,pair)  track=$N2 (trial,pair,cam)  ef=$N3 (trial)"

j1=$(STAGE=match ROOT="$ROOT" MANIFEST="$M/manifest_match.txt" sbatch --parsable \
      --job-name=stage-match --array=1-$N1 --cpus-per-task=8 --mem=44G --time=01:00:00 "$SD/stage.sbatch")
echo "stage1 match : $j1 ($N1 tasks: trial,pair)"
j2=$(STAGE=track ROOT="$ROOT" MANIFEST="$M/manifest_track.txt" sbatch --parsable --dependency=afterok:$j1 \
      --job-name=stage-track --array=1-$N2 --cpus-per-task=32 --mem=80G --time=02:30:00 "$SD/stage.sbatch")
echo "stage2 track : $j2 ($N2 tasks: trial,pair,cam @ 32 threads, after $j1)"
j3=$(STAGE=ef ROOT="$ROOT" MANIFEST="$M/manifest_ef.txt" sbatch --parsable --dependency=afterok:$j2 \
      --job-name=stage-ef --array=1-$N3 --cpus-per-task=4 --mem=32G --time=00:40:00 "$SD/stage.sbatch")
echo "stage3 ef    : $j3 ($N3 tasks: trial, after $j2)"
echo "$j1 $j2 $j3" > "$SD/.staged_jids"
echo "ROOT=$ROOT" > "$SD/.staged_root"

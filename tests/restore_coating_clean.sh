#!/usr/bin/env bash
# Restore /Users/jaoga/devlab/MultiDIC/analysis/S09/coating to its clean state
# (only the 4 REF_MASK_/REF_SEED_ files). Removes all pipeline-generated
# trial dirs (005/, 007/, ...), tmp_frames/, and any other artifacts so the
# next run executes every step from scratch (no checkpoint skipping).
set -euo pipefail
COATING="/Users/jaoga/devlab/MultiDIC/analysis/S09/coating"
shopt -s extglob nullglob
echo "Before:"; ls -1 "$COATING"
# Delete everything that is NOT one of the 4 reference checkpoint files.
for entry in "$COATING"/!(REF_MASK_*|REF_SEED_*); do
  echo "  removing $entry"
  rm -rf "$entry"
done
echo "After:"; ls -1 "$COATING"
# Sanity: must be exactly 4 REF_ files
count=$(find "$COATING" -mindepth 1 | wc -l | tr -d ' ')
if [ "$count" != "4" ]; then
  echo "WARNING: expected 4 files, found $count" >&2
fi

#!/bin/bash
# =============================================================================
# Recover meta.txt for completed experiments whose run_one post-processing died
# before writing it (e.g. the 2026-06-23 pipefail bug). Reconstructs every field
# from the on-disk run.log + xprof_phases.csv — no recompute needed.
#   usage:  ./regen_meta.sh            (all results dirs)
#           ./regen_meta.sh <dir> ...  (specific results dirs)
# =============================================================================
set -uo pipefail
cd "$(dirname "$0")/../../.."
source deploy/cluster/study/env.sh

elapsed_to_s(){ # "h:mm:ss.xx" or "m:ss.xx" -> integer seconds
  awk -F: '{ if (NF==3) printf "%d\n", $1*3600+$2*60+$3; else if (NF==2) printf "%d\n", $1*60+$2; else printf "%d\n", $1 }'
}

regen_one(){
  local d="$1" nm; nm=$(basename "$d")
  local log="$d/run.log" ph="$d/xprof_phases.csv"
  [ -f "$log" ] || { echo "skip $nm (no run.log)"; return; }

  # params from name: t<trial>_f<fstart>-<fend>j<fjump>_thr<threads>_par<par>_r<rep>
  local trial fstart fend fjump threads par rep
  trial=$(sed -E 's/^t([0-9]+)_.*/\1/' <<<"$nm")
  fstart=$(sed -E 's/.*_f([0-9]+)-.*/\1/' <<<"$nm")
  fend=$(sed -E 's/.*_f[0-9]+-([0-9]+)j.*/\1/' <<<"$nm")
  fjump=$(sed -E 's/.*j([0-9]+)_thr.*/\1/' <<<"$nm")
  threads=$(sed -E 's/.*_thr([0-9]+)_.*/\1/' <<<"$nm")
  par=$(sed -E 's/.*_par([0-9]+)_.*/\1/' <<<"$nm")
  rep=$(sed -E 's/.*_r([0-9]+)(_[a-z0-9]+)?$/\1/' <<<"$nm")

  local stepd_ok=0; grep -qi "All 2D DIC Analysis completed" "$log" && stepd_ok=1
  local xprof_ok=0; [ -f "$ph" ] && grep -q STEP_D_total "$ph" && xprof_ok=1
  local crashed=0; grep -qE "terminated by signal 11|emergency dump on signal" "$log" && crashed=1

  local dispatch_n; dispatch_n=$(grep -cE "Parallel dispatch" "$log" 2>/dev/null || true); dispatch_n=${dispatch_n:-0}
  local threads_obs; threads_obs=$(grep -oE "Requesting[ =]+[0-9]+ threads" "$log" 2>/dev/null | grep -oE "[0-9]+" | sort -rn | head -1 || true)
  local speedup;     speedup=$(grep -oE "speedup = [0-9.]+" "$log" 2>/dev/null | grep -oE "[0-9.]+" | sort -rn | head -1 || true)
  local parallel_ok=0; [ "${dispatch_n:-0}" -gt 0 ] && parallel_ok=1

  # The label contains colons ("(h:mm:ss or m:ss):"), so take the last field.
  local wall_s; wall_s=$(awk '/Elapsed \(wall clock\)/{print $NF}' "$log" 2>/dev/null | tail -1 | elapsed_to_s)
  local maxrss; maxrss=$(awk '/Maximum resident set size/{print $NF}' "$log" 2>/dev/null | tail -1)

  local status="failed"
  if [ "$crashed" = 1 ] && [ "$stepd_ok" = 1 ] && [ "$xprof_ok" = 1 ]; then
    status="stepD_ok_downstream_crash(rc=139)"
  elif [ "$stepd_ok" = 1 ] && [ "$xprof_ok" = 1 ]; then
    status="ok"
  fi
  [ "$par" = 1 ] && [ "$stepd_ok" = 1 ] && [ "$parallel_ok" = 0 ] && status="${status}__WARN_RAN_SEQUENTIAL"

  {
    echo "name=$nm"
    echo "status=$status"
    echo "stepd_ok=$stepd_ok"
    echo "parallel_ok=$parallel_ok"
    echo "threads_requested=$threads"
    echo "threads_observed=${threads_obs:-NA}"
    echo "speedup=${speedup:-NA}"
    echo "wall_s=${wall_s:-NA}"
    echo "params: trial=$trial fstart=$fstart fend=$fend fjump=$fjump threads=$threads par=$par rep=$rep"
    awk -F, '/D.matching_cams/{printf "matching_cams_s=%.1f\n",$3/($2>0?$2:1)/1000}' "$ph" 2>/dev/null
    [ -n "${maxrss:-}" ] && echo "maxrss_kb=$maxrss"
    echo "recovered_by=regen_meta.sh"
  } > "$d/meta.txt"
  echo "regen $nm -> status=$status par=$parallel_ok thr=$threads_obs spd=$speedup wall=${wall_s}s"
}

if [ "$#" -gt 0 ]; then for d in "$@"; do regen_one "$d"; done
else for d in "$RESULTS_DIR"/*/; do [ -d "$d" ] && regen_one "$d"; done; fi

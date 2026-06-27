#!/bin/bash
# =============================================================================
# CPPxDIC profiling study — status dashboard.
#   ./status.sh            one-shot overview (build, queue, per-experiment)
#   ./status.sh --watch    auto-refresh every 15s
#   ./status.sh --tail     live-tail the most recent log
#   ./status.sh --csv      dump aggregated results as CSV (stdout)
# =============================================================================
cd "$(dirname "$0")/../../.."
source deploy/cluster/study/env.sh

C_G='\033[32m'; C_R='\033[31m'; C_Y='\033[33m'; C_B='\033[34m'; C_0='\033[0m'
hr(){ printf '%.0s-' {1..72}; echo; }

show_build(){
  echo -e "${C_B}== IMAGE ==${C_0}"
  if [ -f "$SIF" ]; then
    echo -e "  ${C_G}built${C_0}  $SIF  ($(du -h "$SIF" 2>/dev/null|cut -f1), $(date -r "$SIF" '+%Y-%m-%d %H:%M'))"
  else
    echo -e "  ${C_Y}not built yet${C_0}  ($SIF)"
  fi
}

show_queue(){
  echo -e "${C_B}== QUEUE (xdic-*) ==${C_0}"
  local q; q=$(squeue -u "$USER" -n xdic-build,xdic-smoke,xdic-run,xdic-study \
        -o "%.10i %.12j %.2t %.10M %.10L %.6D %R" 2>/dev/null)
  if [ "$(echo "$q" | wc -l)" -le 1 ]; then echo "  (none queued/running)"; else echo "$q" | sed 's/^/  /'; fi
}

show_runs(){
  echo -e "${C_B}== EXPERIMENTS (full outputs: $RUNS_DIR) ==${C_0}"
  shopt -s nullglob
  local any=0
  for d in "$RESULTS_DIR"/*/; do
    any=1; local n; n=$(basename "$d")
    local rc st wall mc rss
    rc=$(awk -F= '/^rc=/{print $2}' "$d/meta.txt" 2>/dev/null)
    st=$(awk -F= '/^status=/{print $2}' "$d/meta.txt" 2>/dev/null)
    wall=$(awk -F= '/^wall_s=/{print $2}' "$d/meta.txt" 2>/dev/null)
    mc=$(awk -F= '/^matching_cams_s=/{print $2}' "$d/meta.txt" 2>/dev/null)
    rss=$(awk -F= '/^maxrss_kb=/{printf "%.1fG",$2/1048576}' "$d/meta.txt" 2>/dev/null)
    local pok thr spd
    pok=$(awk -F= '/^parallel_ok=/{print $2}' "$d/meta.txt" 2>/dev/null)
    thr=$(awk -F= '/^threads_observed=/{print $2}' "$d/meta.txt" 2>/dev/null)
    spd=$(awk -F= '/^speedup=/{print $2}' "$d/meta.txt" 2>/dev/null)
    local tag
    if [ -z "$rc" ]; then tag="${C_Y}running/incomplete${C_0}"
    elif [[ "$st" == *WARN_RAN_SEQUENTIAL* ]]; then tag="${C_R}WARN: RAN SEQUENTIAL${C_0}"
    elif [ "$st" = "ok" ]; then tag="${C_G}done${C_0}"
    elif [ -n "$st" ] && [ "$st" != "failed" ]; then tag="${C_Y}Step-D OK (downstream crash)${C_0}"
    elif [ "$rc" = 0 ]; then tag="${C_G}done${C_0}"
    else tag="${C_R}FAILED rc=$rc${C_0}"; fi
    local par=""
    if [ "${pok:-0}" = 1 ]; then par="${C_G} par:${thr}thr x${spd}${C_0}"; fi
    local sentinel=""
    if [ -n "$mc" ] && awk "BEGIN{exit !($mc>150)}"; then sentinel="${C_R} [CONTENDED mc=${mc}s]${C_0}"; fi
    printf "  %-36s " "$n"; echo -e "$tag  wall=${wall:-?}s  rss=${rss:-?}$par$sentinel"
  done
  [ "$any" = 0 ] && echo "  (no experiments started yet)"
}

agg_csv(){
  echo "name,rc,status,parallel_ok,threads_requested,threads_observed,speedup,wall_s,matching_cams_s,maxrss_kb"
  for d in "$RESULTS_DIR"/*/; do
    [ -f "$d/meta.txt" ] || continue
    local n; n=$(basename "$d")
    # split on the FIRST '=' only: status values contain '=' e.g. "(rc=139)"
    awk -v n="$n" '
      {k=$0; sub(/=.*/,"",k); v=$0; sub(/^[^=]*=/,"",v)}
      k=="rc"{rc=v} k=="status"{st=v} k=="parallel_ok"{pok=v}
      k=="threads_requested"{treq=v} k=="threads_observed"{tobs=v} k=="speedup"{spd=v}
      k=="wall_s"{w=v} k=="matching_cams_s"{mc=v} k=="maxrss_kb"{rss=v}
      END{printf "%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n",n,rc,st,pok,treq,tobs,spd,w,mc,rss}' "$d/meta.txt"
  done
}

latest_log(){ ls -t deploy/cluster/study/logs/*.out 2>/dev/null | head -1; }

case "${1:-}" in
  --watch) while true; do clear; date; hr; show_build; hr; show_queue; hr; show_runs; sleep 15; done ;;
  --tail)  f=$(latest_log); echo "tailing: $f"; tail -f "$f" ;;
  --csv)   agg_csv ;;
  *)       date; hr; show_build; hr; show_queue; hr; show_runs; hr
           echo "logs: deploy/cluster/study/logs/   |   ./status.sh --watch | --tail | --csv" ;;
esac

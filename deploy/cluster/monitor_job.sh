#!/bin/bash
# =============================================================================
# Monitor CPPxDIC SLURM jobs
#
# Usage:
#   ./monitor_job.sh                  # Show all your cppxdic jobs
#   ./monitor_job.sh <job_id>         # Monitor a specific job
#   ./monitor_job.sh --tail <job_id>  # Live-tail stdout of a running job
#   ./monitor_job.sh --summary        # Summary of all completed jobs in logs/
# =============================================================================

set -euo pipefail

LOGS_DIR="${LOGS_DIR:-$(dirname "$0")/logs}"
REFRESH_INTERVAL=10

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# -----------------------------------------------------------------------------
# Functions
# -----------------------------------------------------------------------------

show_usage() {
    echo "Usage: $(basename "$0") [OPTIONS] [JOB_ID]"
    echo ""
    echo "Options:"
    echo "  (no args)          List all your cppxdic jobs"
    echo "  <job_id>           Show detailed status of a specific job"
    echo "  --tail <job_id>    Live-tail the stdout log of a running job"
    echo "  --summary          Show summary table of all completed jobs in logs/"
    echo "  --watch            Continuously refresh job list (every ${REFRESH_INTERVAL}s)"
    echo "  --help             Show this help"
}

# List all cppxdic jobs for the current user
list_jobs() {
    echo -e "${CYAN}=== CPPxDIC Jobs for ${USER} ===${NC}"
    echo ""
    
    # Header
    printf "%-12s %-10s %-8s %-20s %-12s %-10s\n" \
        "JOB_ID" "STATE" "CPUS" "NODE" "ELAPSED" "MEM_USED"
    printf "%s\n" "------------------------------------------------------------------------"
    
    # Query SLURM
    squeue -u "${USER}" -n cppxdic -o "%.12i %.10T %.8C %.20R %.12M %.10m" --noheader 2>/dev/null \
    || echo "(No running cppxdic jobs found)"
    
    echo ""
    
    # Also show recently completed jobs (last 24h)
    echo -e "${CYAN}--- Recently completed (last 24h) ---${NC}"
    sacct -u "${USER}" -n cppxdic \
        --starttime=$(date -d '24 hours ago' '+%Y-%m-%dT%H:%M:%S' 2>/dev/null || date -v-24H '+%Y-%m-%dT%H:%M:%S' 2>/dev/null || echo "2024-01-01") \
        --format="JobID%-12,State%-12,ExitCode%-8,Elapsed%-12,MaxRSS%-12,NodeList%-20" \
        --noheader 2>/dev/null \
    || echo "(sacct not available or no recent jobs)"
    echo ""
}

# Detailed status for a specific job
job_detail() {
    local JOB_ID="$1"
    
    echo -e "${CYAN}=== Job ${JOB_ID} Detail ===${NC}"
    echo ""
    
    # Check if running
    JOB_STATE=$(squeue -j "${JOB_ID}" -o "%T" --noheader 2>/dev/null || echo "UNKNOWN")
    
    if [ "${JOB_STATE}" = "UNKNOWN" ] || [ -z "${JOB_STATE}" ]; then
        # Job finished — use sacct
        echo -e "${YELLOW}Job is no longer running. Querying accounting data...${NC}"
        echo ""
        sacct -j "${JOB_ID}" \
            --format="JobID%-15,JobName%-12,State%-15,ExitCode%-8,Start%-20,End%-20,Elapsed%-12,MaxRSS%-12,MaxVMSize%-12,NCPUS%-6,NodeList%-20" \
            2>/dev/null || echo "sacct unavailable"
    else
        echo -e "State: ${GREEN}${JOB_STATE}${NC}"
        scontrol show job "${JOB_ID}" 2>/dev/null | grep -E "JobId|JobName|UserId|Partition|NumCPUs|MinMemory|TimeLimit|RunTime|NodeList|WorkDir|StdOut|StdErr"
    fi
    
    echo ""
    
    # Show last 20 lines of stdout if log exists
    local STDOUT_LOG="${LOGS_DIR}/cppxdic_${JOB_ID}.out"
    local STDERR_LOG="${LOGS_DIR}/cppxdic_${JOB_ID}.err"
    
    if [ -f "${STDOUT_LOG}" ]; then
        echo -e "${CYAN}--- Last 30 lines of stdout ---${NC}"
        tail -30 "${STDOUT_LOG}"
        echo ""
        
        # Parse progress: look for step indicators
        echo -e "${CYAN}--- Progress Detection ---${NC}"
        STEP_D=$(grep -c "Step D\|StepD\|step_d\|2D DIC\|tracking" "${STDOUT_LOG}" 2>/dev/null || echo 0)
        STEP_E=$(grep -c "Step E\|StepE\|step_e\|3D Reconstruction\|dic3DReconstruction" "${STDOUT_LOG}" 2>/dev/null || echo 0)
        STEP_F=$(grep -c "Step F\|StepF\|step_f\|Deformation\|dicDeformation" "${STDOUT_LOG}" 2>/dev/null || echo 0)
        COMPLETED=$(grep -c "completed successfully\|End of script" "${STDOUT_LOG}" 2>/dev/null || echo 0)
        
        if [ "${COMPLETED}" -gt 0 ]; then
            echo -e "  Pipeline: ${GREEN}COMPLETED${NC}"
        elif [ "${STEP_F}" -gt 0 ]; then
            echo -e "  Pipeline: ${YELLOW}Step F (Deformation)${NC}"
        elif [ "${STEP_E}" -gt 0 ]; then
            echo -e "  Pipeline: ${YELLOW}Step E (3D Reconstruction)${NC}"
        elif [ "${STEP_D}" -gt 0 ]; then
            echo -e "  Pipeline: ${YELLOW}Step D (2D DIC)${NC}"
        else
            echo -e "  Pipeline: ${YELLOW}Initializing...${NC}"
        fi
        
        # Extract timing info
        LAST_TIME=$(grep -oE "\[[0-9]+\.[0-9]+s\]|Elapsed time:.*" "${STDOUT_LOG}" 2>/dev/null | tail -1)
        if [ -n "${LAST_TIME}" ]; then
            echo "  Last timing: ${LAST_TIME}"
        fi
        echo ""
    else
        echo "(stdout log not yet available: ${STDOUT_LOG})"
    fi
    
    if [ -f "${STDERR_LOG}" ]; then
        local ERR_LINES
        ERR_LINES=$(wc -l < "${STDERR_LOG}" 2>/dev/null || echo 0)
        if [ "${ERR_LINES}" -gt 0 ]; then
            echo -e "${RED}--- Last 10 lines of stderr (${ERR_LINES} total lines) ---${NC}"
            tail -10 "${STDERR_LOG}"
            echo ""
        fi
    fi
}

# Live-tail a running job's stdout
tail_job() {
    local JOB_ID="$1"
    local STDOUT_LOG="${LOGS_DIR}/cppxdic_${JOB_ID}.out"
    
    if [ ! -f "${STDOUT_LOG}" ]; then
        echo "Waiting for log file: ${STDOUT_LOG}"
        while [ ! -f "${STDOUT_LOG}" ]; do
            sleep 2
        done
    fi
    
    echo -e "${CYAN}=== Live tail: ${STDOUT_LOG} (Ctrl+C to stop) ===${NC}"
    tail -f "${STDOUT_LOG}"
}

# Summary of all completed jobs
summary_jobs() {
    echo -e "${CYAN}=== CPPxDIC Job Summary ===${NC}"
    echo ""
    
    if [ ! -d "${LOGS_DIR}" ]; then
        echo "No logs directory found: ${LOGS_DIR}"
        exit 0
    fi
    
    printf "%-12s %-10s %-15s %-15s %-8s\n" \
        "JOB_ID" "STATUS" "START" "DURATION" "EXIT"
    printf "%s\n" "------------------------------------------------------------"
    
    for logfile in "${LOGS_DIR}"/cppxdic_*.out; do
        [ -f "${logfile}" ] || continue
        
        local JOB_ID
        JOB_ID=$(basename "${logfile}" | sed 's/cppxdic_//;s/\.out//')
        
        local STATUS="UNKNOWN"
        local DURATION=""
        local EXIT_CODE=""
        local START_TIME=""
        
        # Parse from log content
        if grep -q "completed successfully\|End of script" "${logfile}" 2>/dev/null; then
            STATUS="${GREEN}SUCCESS${NC}"
        elif grep -q "FAIL\|ERROR\|Error:" "${logfile}" 2>/dev/null; then
            STATUS="${RED}FAILED${NC}"
        elif [ -s "${logfile}" ]; then
            STATUS="${YELLOW}PARTIAL${NC}"
        fi
        
        START_TIME=$(grep "Start time:" "${logfile}" 2>/dev/null | head -1 | sed 's/.*Start time: *//' || echo "?")
        DURATION=$(grep "Elapsed time:" "${logfile}" 2>/dev/null | tail -1 | sed 's/.*Elapsed time: *//' || echo "?")
        EXIT_CODE=$(grep "Exit code:" "${logfile}" 2>/dev/null | tail -1 | sed 's/.*Exit code: *//' || echo "?")
        
        printf "%-12s " "${JOB_ID}"
        echo -ne "${STATUS}"
        printf "%-5s %-15s %-15s %-8s\n" "" "${START_TIME:0:15}" "${DURATION}" "${EXIT_CODE}"
    done
    echo ""
}

# Continuous watch mode
watch_jobs() {
    echo "Refreshing every ${REFRESH_INTERVAL}s (Ctrl+C to stop)"
    while true; do
        clear
        list_jobs
        sleep "${REFRESH_INTERVAL}"
    done
}

# -----------------------------------------------------------------------------
# Main
# -----------------------------------------------------------------------------

case "${1:-}" in
    --help|-h)
        show_usage
        ;;
    --tail)
        if [ -z "${2:-}" ]; then
            echo "ERROR: --tail requires a job ID"
            exit 1
        fi
        tail_job "$2"
        ;;
    --summary)
        summary_jobs
        ;;
    --watch)
        watch_jobs
        ;;
    "")
        list_jobs
        ;;
    *)
        # Assume it's a job ID
        job_detail "$1"
        ;;
esac

#!/usr/bin/env bash
# Cheat-sheet for the CPPxDIC HPC / Singularity (Apptainer) + SLURM workflow.
# Run these steps from the repository root. See deploy/cluster/README.md for details.

# 1. Build the container image once (on a login node with internet)
apptainer build deploy/cluster/cppxdic.sif deploy/cluster/cppxdic.def

# 2. Edit configs (no rebuild needed to change parameters)
vim deploy/cluster/configs/dic_params.txt

# 3. Submit a job
sbatch deploy/cluster/submit_job.sh

# 4. Monitor (replace JOB_ID with the id printed by sbatch)
./deploy/cluster/monitor_job.sh --tail JOB_ID

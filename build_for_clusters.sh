# 1. Build once (on login node with internet)
apptainer build cluster/cppxdic.sif cluster/cppxdic.def

# 2. Edit configs (no rebuild)
vim cluster/configs/dic_params.txt

# 3. Submit
sbatch cluster/submit_job.sh

# 4. Monitor
./cluster/monitor_job.sh --tail <job_id>
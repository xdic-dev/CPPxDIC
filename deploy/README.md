# CPPxDIC deployment

Container and HPC deployment assets for CPPxDIC. Everything here builds and runs the
default **camera-pairs** reconstruction mode (`cppxdic`) and assumes the git submodules
under `Tools/` are checked out (`git submodule update --init --recursive`).

CPPxDIC is **shared-memory parallel** (OpenMP, via the CppNCorr engine): it scales across
the cores of a single node, not across MPI ranks. Both deployment paths therefore request
cores on one node and bind `OMP_NUM_THREADS` to the allocation.

There are two deployment paths — pick by where you run:

| Path | Use when | Entry point |
|------|----------|-------------|
| [`docker/`](docker/README.md) | local development on Linux / macOS / Windows | `deploy/docker/Dockerfile` (+ `run.sh`, `docker-compose.yml`) |
| [`cluster/`](cluster/README.md) | HPC clusters managed by **SLURM** (via Singularity/Apptainer) | `deploy/cluster/cppxdic.def` (+ `submit_job.sh`, `monitor_job.sh`) |

In both cases the binary is baked into the image **once**; configuration files are
bind-mounted from the host at runtime, so changing parameters never requires a rebuild.

---

## Layout

```
deploy/
├── README.md                 # this file
├── build.sh                  # local CMake build helper (run from repo root)
├── build_for_clusters.sh     # cheat-sheet: build .sif, edit configs, sbatch, monitor
├── docker/                   # Docker / Podman (local & desktop)
│   ├── Dockerfile            # multi-stage build of the cppxdic camerapairs binary
│   ├── docker-compose.yml    # compose service (context = repo root)
│   ├── run.sh                # convenience wrapper around `docker run`
│   ├── README.md             # Docker-specific guide
│   └── configs/              # template dic/ncorr/visualization params
└── cluster/                  # Singularity/Apptainer + SLURM (HPC)
    ├── cppxdic.def           # Apptainer/Singularity definition
    ├── submit_job.sh         # SLURM submission script
    ├── monitor_job.sh        # job monitoring / log tailing
    ├── README.md             # cluster-specific guide
    └── configs/              # template dic/ncorr/visualization params
```

All build commands below are run from the repository **root** so the `Tools/` submodules
are inside the build context.

---

## Docker (local / desktop)

```bash
git submodule update --init --recursive
docker build -t cppxdic -f deploy/docker/Dockerfile .
./deploy/docker/run.sh --subject S10 --reftrial 3
```

See [`docker/README.md`](docker/README.md) for the full guide (bind mounts, compose,
thread control, troubleshooting).

### Image-tag convention

| Tag pattern | Meaning |
|-------------|---------|
| `cppxdic:latest` | most recent build of the default branch |
| `cppxdic:<branch>` | branch build (e.g. `cppxdic:newversion`) |
| `cppxdic:<short-sha>` | immutable build pinned to a commit (e.g. `cppxdic:1f2d57e`) |
| `cppxdic:<version>` | tagged release (e.g. `cppxdic:1.0.0`), once releases are cut |

---

## HPC cluster (Singularity/Apptainer + SLURM)

```bash
# build the image once (login node with internet)
apptainer build deploy/cluster/cppxdic.sif deploy/cluster/cppxdic.def

# edit configs, then submit
sbatch deploy/cluster/submit_job.sh
DATA_DIR=/scratch/$USER/data OUTPUT_DIR=/scratch/$USER/out sbatch deploy/cluster/submit_job.sh

# monitor
./deploy/cluster/monitor_job.sh --tail <job_id>
```

`submit_job.sh` binds `OMP_NUM_THREADS` to `--cpus-per-task` and sets
`OMP_PROC_BIND=spread` / `OMP_PLACES=cores` / `OMP_MAX_ACTIVE_LEVELS=1` (matching the
repository's OpenMP tuning for multi-frame DIC scaling). `build_for_clusters.sh` is a
quick cheat-sheet of this flow. See [`cluster/README.md`](cluster/README.md) for the full
guide (path conventions, parameter sweeps, SLURM resource tuning, troubleshooting).

> **Cluster target:** SLURM only. To port to **PBS/Torque** or **LSF**, translate the
> `#SBATCH` directives in `submit_job.sh` to `#PBS` / `#BSUB`, keep the OpenMP environment
> block, and request cores on a single node (e.g. PBS `-l select=1:ncpus=16`). Some sites
> ship `singularity` instead of `apptainer`; the commands are identical.

---

## Local build (no container)

```bash
deploy/build.sh          # run from the repository root
# equivalent to:
cmake -S . -B build && cmake --build build -j
```

See [docs/quickstart.md](../docs/quickstart.md) for prerequisites and the per-target build
options.

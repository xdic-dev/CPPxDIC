# CPPxDIC deployment

Parallel-execution, cluster, and container artifacts for CPPxDIC. All scripts target the
default **camera-pairs** reconstruction mode and assume the git submodules under `Tools/`
are checked out (`git submodule update --init --recursive`).

CPPxDIC is **shared-memory parallel** (OpenMP, via the CppNCorr engine) — it scales across
cores on a single node, not across MPI ranks.

---

## Contents

| File | Target | Purpose |
|------|--------|---------|
| `Dockerfile` | container (Docker / Podman) | multi-stage build of the `cppxdic` camera-pairs binary on Ubuntu 24.04, then a slim runtime image. |
| `slurm_cppxdic.sbatch` | **SLURM** clusters | single-node, OpenMP-threaded batch job that builds (if needed) and runs `cppxdic`. |
| `build.sh` | any | local convenience build script (CMake configure + make, with a manual-compile fallback). Run from the repository root: `deploy/build.sh`. |

> No PBS/Torque or LSF script is shipped yet. To port the SLURM job: translate the
> `#SBATCH` directives to `#PBS` / `#BSUB`, keep the OpenMP environment block, and request
> cores on a single node (e.g. PBS `-l select=1:ncpus=16`).

---

## Docker

Build context is the repository **root** (so `Tools/` submodules are in scope):

```bash
git submodule update --init --recursive
docker build -f deploy/Dockerfile -t cppxdic:latest .
```

Run (mount your data directory):

```bash
docker run --rm -v "$PWD/data:/work/data" cppxdic:latest \
    --config /opt/cppxdic/config/default.cfg --subject S09 --reftrial 5
docker run --rm cppxdic:latest --help
```

### Image-tag convention

| Tag pattern | Meaning |
|-------------|---------|
| `cppxdic:latest` | most recent build of the default branch. |
| `cppxdic:<branch>` | branch build (e.g. `cppxdic:newversion`). |
| `cppxdic:<short-sha>` | immutable build pinned to a commit (e.g. `cppxdic:1f2d57e`). |
| `cppxdic:<version>` | tagged release (e.g. `cppxdic:1.0.0`), once releases are cut. |

The `Dockerfile` always builds `XDIC_MODE=camerapairs` with tests and the optional
`proxyncorr`/`singledic` apps disabled, for a lean runtime image.

---

## SLURM

```bash
# from the repository root
sbatch deploy/slurm_cppxdic.sbatch --subject S09 --reftrial 5
```

Edit the `#SBATCH` directives (partition, account, time, `--cpus-per-task`, `--mem`) for your
site. The script binds `OMP_NUM_THREADS` to the allocated `--cpus-per-task` and sets
`OMP_PROC_BIND=spread` / `OMP_PLACES=cores` / `OMP_MAX_ACTIVE_LEVELS=1` (matching the
repository's OpenMP configuration), then builds `cppxdic` if `build/cppxdic` is absent and
runs it via `srun`.

---

## Local build

```bash
deploy/build.sh          # run from the repository root
```

Equivalent to the manual CMake flow in [docs/quickstart.md](../docs/quickstart.md):

```bash
cmake -S . -B build && cmake --build build -j
```

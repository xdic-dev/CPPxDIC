# CPPxDIC — Docker Build & Run

## Prerequisites

- [Docker](https://docs.docker.com/get-docker/) (Desktop or Engine)
- Works on **Linux**, **macOS**, and **Windows** (via Docker Desktop / WSL2)

## Quick Start

### 1. Build the image

From the **project root** (`CPPxDIC/`):

```bash
docker build -t cppxdic -f deploy/docker/Dockerfile .
```

### 2. Edit configuration

Copy and edit the template configs in `deploy/docker/configs/`:

```
deploy/docker/configs/dic_params.txt
deploy/docker/configs/ncorr_params.txt
deploy/docker/configs/visualization_params.txt
```

Key paths inside the container:
| Host path | Container mount | Access |
|-----------|----------------|--------|
| Your config dir | `/configs` | read-only |
| Your input data | `/data` | read-only |
| Your output dir | `/output` | read-write |

Update `base_path`, `data_path`, `dic_path` in `dic_params.txt` to use the container paths (e.g. `/data`, `/output`).

### 3. Run

**Using the helper script:**

```bash
# Basic
./deploy/docker/run.sh

# With overrides
SUBJECT=S10 REFTRIAL=3 DATA_DIR=/path/to/data OUTPUT_DIR=/path/to/output ./deploy/docker/run.sh

# With extra arguments
./deploy/docker/run.sh --subject S10 --reftrial 3
```

**Using docker directly:**

```bash
docker run --rm \
  -v /path/to/configs:/configs:ro \
  -v /path/to/data:/data:ro \
  -v /path/to/output:/output \
  -e OMP_NUM_THREADS=8 \
  cppxdic \
  --dic-params /configs/dic_params.txt \
  --ncorr-params /configs/ncorr_params.txt \
  --viz-params /configs/visualization_params.txt \
  --subject S10 --reftrial 3
```

**Using docker compose:**

```bash
# Edit deploy/docker/docker-compose.yml or set env vars
DATA_DIR=/path/to/data OUTPUT_DIR=/path/to/output \
  docker compose -f deploy/docker/docker-compose.yml up
```

### 4. OpenMP threads

Set the number of threads via the `OMP_NUM_THREADS` environment variable:

```bash
docker run --rm -e OMP_NUM_THREADS=16 ... cppxdic ...
```

Default is 4.

## Configuration Files

Configuration files live **outside** the image (bind-mounted). Change them freely without rebuilding.

| File | Purpose |
|------|---------|
| `dic_params.txt` | DIC parameters: paths, subject, pairs, steps |
| `ncorr_params.txt` | NCorr algorithm parameters |
| `visualization_params.txt` | Visualization / export settings |

## Rebuilding

Only rebuild when the **source code** changes:

```bash
docker build -t cppxdic -f deploy/docker/Dockerfile .
```

Config changes never require a rebuild.

## Comparison with Singularity/Apptainer

| Feature | Docker | Singularity |
|---------|--------|-------------|
| Platform | Linux, macOS, Windows | Linux (HPC) |
| Root needed to run | No (Docker Desktop) | No |
| HPC / SLURM | Not typical | Native |
| Bind mounts | `-v host:container` | `--bind host:container` |
| Image format | OCI layers | Single `.sif` file |

Use **Docker** for local development and Windows/macOS. Use **Singularity** (`deploy/cluster/cppxdic.def`) for HPC/SLURM clusters.

## Troubleshooting

- **Build fails with OOM**: Limit parallel jobs: edit Dockerfile, change `make -j$(nproc)` to `make -j2`
- **Permission errors on output**: Ensure the output directory exists and is writable, or run with `--user $(id -u):$(id -g)`
- **Shared library errors at runtime**: The multi-stage build copies binaries but may miss a library. Fall back to the single-stage approach by removing the second `FROM` stage in the Dockerfile.

# xdic_webgui

A lightweight, **independently runnable** web GUI for CPPxDIC. Like
`apps/xdic_stepsABC` it is its own self-contained unit, but it is a **Python
(FastAPI) + vanilla HTML/JS/Plotly** app rather than a C++/CMake target — it does
**not** touch the C++ build and adds no CMake changes. It reads and writes the
same INI-style parameter files the C++ `Config` class consumes
(`include/config.h`, `src/config.cpp`) and reads the same `.mat`/`.csv`/`.png`/`.mp4`
artifacts the pipeline produces.

It covers three things that are otherwise awkward today:

1. **Parameter forms** — create/edit `dic_params.txt`, `ncorr_params.txt`,
   `visualization_params.txt`, and the unified `config/default.cfg` through typed
   forms, with per-field help text lifted from the `config.h` comments. No more
   hand-editing with no validation.
2. **Media viewer** — browse videos, scrub frames, overlay the reference MASK and
   seed point(s), and preview the image-filtering result — without running MATLAB
   or the full pipeline.
3. **Interactive plots** — zoom/hover/scrub `ncorr<N>` displacement & correlation
   grids (2D heatmaps) and `DIC3D*` 3D strain surfaces, replacing the static
   MATLAB animations from `plot_singleTrial_map` / `saveAnimationFunc`.

## Install & run

```sh
cd apps/xdic_webgui
python -m venv .venv && . .venv/bin/activate
pip install -r requirements.txt
./run.sh                       # http://localhost:8021
```

Options:

```sh
PORT=9000 ./run.sh             # custom port
XDIC_REPO_ROOT=/path/to/repo ./run.sh   # resolve params/data against another checkout
```

The repo root (used to resolve param files and the default `data_path`/`dic_path`)
defaults to two directories up from this app. Override it with `XDIC_REPO_ROOT`.

## How it maps to the codebase

| GUI feature        | Backend module           | Reads / writes                                                   |
| ------------------ | ------------------------ | ---------------------------------------------------------------- |
| Parameter forms    | `config_schema.py`, `params_io.py` | `dic_params.txt`, `ncorr_params.txt`, `visualization_params.txt`, `config/default.cfg` |
| Media viewer       | `media.py`               | `.mp4` videos, `REF_MASK_*.mat`/`_mask.png`/`.poly`, `REF_SEED_*.mat`/`.seed` |
| Interactive plots  | `results.py`             | `ncorr<N>.mat`/`.csv`, `DIC3D*Pairs_*.mat`, `DIC3DPPresults_*.mat` |

- **Parameter schema** (`config_schema.py`) mirrors `include/config.h` field-for-field;
  `help` strings come from the inline comments there.
- **Param I/O** (`params_io.py`) round-trips the INI format exactly as
  `Config::parseConfigValue/parseBool/parse*List` in `src/config.cpp`: `key = value`,
  `#` comments, comma-separated lists, `true/false` bools. Saving updates values **in
  place**, preserving comments and ordering for clean diffs.
- **`.mat` loading** tries `scipy.io.loadmat` and falls back to `h5py` for v7.3/HDF5
  files written by matio.

## API (for reference)

```
GET  /api/config                         repo root + default data/dic paths
GET  /api/schema?file=...                form schema for a target file
GET  /api/params?file=...                current values (file ∪ defaults)
POST /api/params?file=...                write values  (body: {"values": {...}})

GET  /api/media/videos?data_path=...     list videos
GET  /api/media/info?path=...            frame count / fps / size
GET  /api/media/frame?path=&index=&filter=&limit_grayscale=   PNG
GET  /api/media/overlay?path=&index=&mask=&seed=              PNG with overlay

GET  /api/results/list?root=...          discover ncorr*/DIC3D* files
GET  /api/results/ncorr?path=            available U/V/C fields + frame counts
GET  /api/results/ncorr_field?path=&field=&frame=            2D grid JSON
GET  /api/results/dic3d?path=            points/faces/scalar-field names
GET  /api/results/dic3d_scalar?path=&field=                  scalar values JSON
```

## Notes / limitations (thin v1)

- **Plotly** is loaded from `frontend/vendor/plotly.min.js` if present, otherwise
  falls back to the Plotly CDN at runtime. To bundle it offline:
  `curl -L https://cdn.plot.ly/plotly-2.30.0.min.js -o frontend/vendor/plotly.min.js`.
- The **filter preview** in `media.py` is a numpy approximation of
  `src/image_processor.cpp` (grayscale limit + percentile stretch), not a bit-exact
  port; it is meant for visual inspection and can be tightened later.
- `.mat`/`DIC3D*` layouts vary across pipeline versions; the loaders are best-effort
  and silently omit pieces they can't recognise. The 3D viewer reports the available
  variable keys when it can't find points, to aid debugging.
- Path inputs are constrained to stay under the configured repo/data/dic roots.

# CPPxDIC Developer Guide

Audience: contributors extending CPPxDIC. For building/running, start with the
[Quick-start](quickstart.md); for end-user parameters/outputs, see the
[User Guide](user_guide.md).

---

## 1. Repository layout

| Path | Contents |
|------|----------|
| `CMakeLists.txt` | top-level build: `cppxdic` (default), `generate_ncorr_bin`, optional `proxyncorr` / `singledic`, and the test suite under `BUILD_TESTING`. |
| `include/` | public headers (`config.h`, `dic_structures.h`, `delaunay_triangulation.h`, `surface_stitching.h`, `temporal_filter.h`, `face_isotropy.h`, …). |
| `include/xdic/` | compile-time reconstruction-mode selection (`xdic_mode.h`) and mode skeletons (`mirrored/`). |
| `include/input/` | input-reader interfaces (`image_folder_reader.h`). |
| `src/` | implementation: `main.cpp` (entry point), `config.cpp`, `dic_analysis.cpp`, `step_d_workflow.cpp`, `strain_computation.cpp`, `mat_reader.cpp` / `mat_writer.cpp`, `delaunay_triangulation.cpp`, `surface_stitching.cpp`, `temporal_filter.cpp`, `face_isotropy.cpp`, `visualization.cpp`, `data_serializer.cpp`, `utils.cpp`, `image_processor.cpp`, `roi_manager.cpp`, `matlab_functions.cpp`. |
| `src/xdic/` | reconstruction modes: `mirrored/` (stub), `multi/` (placeholder + design note). |
| `src/input/` | input readers: `image_folder_reader.cpp` (directory listing implemented; decode is a TODO). |
| `apps/proxyncorr/` | pass-through 2D DIC driver around CppNCorr (optional target). |
| `apps/singledic/` | single-camera DIC stub + MATLAB reference pointer. |
| `config/` | `default.cfg` — the unified config-file tier of the override chain. |
| `docs/` | this guide, the quick-start, and the user guide. |
| `deploy/` | Docker + cluster (SLURM) deployment artifacts and their README. |
| `tests/cppxdic_suite/` | the in-tree test harness + unit/integration/e2e tests (kept separate from the pre-existing `tests/` infrastructure). |
| `Tools/CppNCorr/` | **submodule** — the vendored 2D DIC engine, built in-tree. Do not edit here. |
| `Tools/MultiDIC/` | **submodule** — the MATLAB xDIC reference project (read-only). |
| `dic_params.txt`, `ncorr_params.txt`, `visualization_params.txt` | per-domain parameter files (the specific-file tier of the override chain). |

---

## 2. Pipeline overview

The default camera-pairs pipeline (MATLAB step naming preserved):

1. **Input** — videos (`Utils::importRawVid`) or image folders (`ImageFolderReader`) →
   per-camera, single-channel frames.
2. **Step D** (`step_d_workflow.cpp`) — initial 2D DIC tracking per stereo pair via CppNCorr.
3. **Step E** (`dic_analysis.cpp`) — 2D matching + DLT-based 3D reconstruction
   (`dic3DReconstruction`), Delaunay meshing (`delaunay_triangulation.cpp`).
4. **Surface stitching** (`surface_stitching.cpp`) — combine stereo pairs into one surface.
5. **Step F** (`dic_analysis.cpp`, `strain_computation.cpp`) — deformation/strain analysis,
   optional temporal filtering (`temporal_filter.cpp`).
6. **Output** — `.mat` / `.bin` / `.json` results + VTK/PLY/CSV exports + overlay videos
   (`data_serializer.cpp`, `visualization.cpp`).

---

## 3. How to add a new xDIC mode

Modes are selected at **compile time** so the working camera-pairs path is never
restructured to add a new one.

1. **Pick a mode name** and wire it in `CMakeLists.txt`: the `XDIC_MODE` cache variable maps
   `camerapairs|mirrored|multi` to the macros `XDIC_MODE_CAMERAPAIRS|_MIRRORED|_MULTI`. Add
   your name + macro there, and add a `target_sources(...)` block guarded by
   `if(XDIC_MODE STREQUAL "yourmode")`.
2. **Extend the enum** in `include/xdic/xdic_mode.h`: add a `Mode::YourMode` value and a
   branch in both `active_mode()` and `active_mode_name()` for your macro.
3. **Add the implementation** under `src/xdic/yourmode/`, guarded so it is only compiled for
   your mode (follow `src/xdic/mirrored/mirrored_mode.cpp`). A placeholder may use
   `#error "not implemented"` (see `src/xdic/multi/multi_mode.cpp`).
4. **Branch in `main.cpp`** on `xdic::active_mode()` where the pipeline diverges.
5. **Test** the guard: `tests/cppxdic_suite/unit/test_xdic_mode.cpp` asserts the active mode
   for the default build; extend it as needed.

---

## 4. How to add a new input reader

The video reader (`Utils::importRawVid`) and `ImageFolderReader` share a contract: given an
input location, produce sorted, per-camera, single-channel frames for the DIC engine.

1. Add a header under `include/input/` with `#pragma once` and Doxygen comments on every
   public symbol; mirror `image_folder_reader.h` (a `listFrames()`-style discovery method and
   a documented `decodeFrames()` TODO returning the same single-channel representation the
   video reader emits — Red channel for RGB, matching MATLAB `iloc(:,:,1)`).
2. Implement under `src/input/`; add the `.cpp` to the `cppxdic` target sources in
   `CMakeLists.txt` (and to `cppxdic_core` in `tests/cppxdic_suite/CMakeLists.txt` if you want
   unit coverage).
3. Add unit tests under `tests/cppxdic_suite/unit/` (see `test_image_folder_reader.cpp` for
   the temp-directory fixture pattern).

---

## 5. Config-file schema reference

Parameters resolve through a **three-tier override chain** (lowest → highest priority):

1. **Compiled defaults** — member initialisers in `include/config.h`.
2. **Unified config file** — `config/default.cfg` (`--config` / `-C`), loaded by
   `Config::loadFromConfigFile`.
3. **Specific param files** — `dic_params.txt`, `ncorr_params.txt`,
   `visualization_params.txt` (`-d` / `-n` / `-v`), which override the unified file.
4. **CLI arguments** — e.g. `--subject`, `--reftrial`, which override everything.

**Format:** INI-style `key = value`, one per line; `#` comments and blank lines ignored.
Lists are comma-separated (e.g. `nfcond_set = 5,6`); booleans accept `true/false`, `1/0`,
`yes` (case-insensitive). Whitespace around keys/values is trimmed; malformed list tokens
are warned about and skipped.

Every key in `config/default.cfg` mirrors a field in `include/config.h` and is documented
with a one-line comment there. Categories: global (`num_pair`, `frictional_conditions`,
sampling frequencies), paths (`base_path`, `data_path`, `dic_path`), processing flags
(`automatic_process`, `parallel_processing`, `debug_mode`), DIC selection (`subject_id`,
`phase_id`, `material_id`, frame range), step-level params (`step_d_*`, `step_e_*`,
`step_f_*`), NCorr params (`scalefactor`, `interp`, `cutoff_*`, …), and visualization params
(export formats, colormap, video, stats). See the [User Guide](user_guide.md#parameter-reference)
for the user-facing parameter table.

Adding a new key: declare the field + default in `config.h`, document it in
`config/default.cfg`, and add a `parseConfigValue(line, "your_key")` branch in the relevant
loader in `config.cpp` (`loadFromDicParamsFile` / `loadFromNcorrParamsFile` /
`loadFromVisualizationParamsFile`).

---

## 6. Running tests locally

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build -j
cd build && ctest --output-on-failure
```

- **Framework:** a tiny in-tree assertion harness, `tests/cppxdic_suite/framework/
  test_harness.h` (macros `TEST`, `CHECK_EQ/NE/TRUE/FALSE/NEAR`, `REQUIRE_TRUE`,
  `SKIP_TEST`). No GoogleTest/Catch2 — zero new dependencies.
- **Build guard:** the suite is configured only when `BUILD_TESTING=ON` (default) **and**
  CPPxDIC is the top-level project, so it never affects production or sub-directory consumers.
- **Layout:** `unit/`, `integration/`, `e2e/`. A `cppxdic_core` static library reuses the
  production sources/dependencies; each test is a small executable registered with `add_test`.
- **Skipped tests:** stereo-dependent integration/e2e tests are registered as SKIPPED
  placeholders (no in-repo stereo fixture). Two `filterTime()` invariants are SKIPPED because
  the production temporal filter is numerically unstable (a separately tracked bug). Skips keep
  the suite green while making coverage gaps explicit.

### Adding a test

Create `tests/cppxdic_suite/<layer>/test_<thing>.cpp` including
`../framework/test_harness.h`, write `TEST(group, name) { ... }` bodies ending with
`TEST_MAIN()`, then register it in `tests/cppxdic_suite/CMakeLists.txt` via
`cppxdic_add_test(<name> <layer>/test_<thing>.cpp)`. If it needs production code beyond the
files already in `cppxdic_core`, add the source to that library.

---

## 7. Conventions

- New public headers: `#pragma once` (or include guards) + Doxygen comments on every public
  symbol.
- Commits: conventional-commit style (`feat:`, `fix:`, `refactor:`, `test:`, `docs:`, `ci:`,
  `chore:`); one logical unit per commit.
- Never break the camera-pairs pipeline; new modes/readers stay default-OFF or gated.
- Submodules (`Tools/CppNCorr`, `Tools/MultiDIC`) are off-limits for edits.

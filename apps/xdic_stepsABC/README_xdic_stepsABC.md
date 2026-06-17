# xdic_stepsABC

Standalone tool covering the in-scope parts of the MATLAB MultiDIC pipeline:

- **(A) MASK + Seed setup** — define the reference ROI MASK (polygon) and the
  DIC Seed point(s), either **interactively** (OpenCV highgui, local machine) or
  **headlessly from files** (cluster, no display).
- **(C) StepC stereovision / DLT calibration** — generate the cylindrical
  calibration-object 3D coordinates, fit the 11-parameter DLT per camera, and
  compute the 3D reconstruction-error metric.

StepA (kinematics) and StepB (friction plots) are intentionally **out of scope**.

The same binary supports GUI and non-GUI workflows. GUI drawing code is compiled
only when the CMake option `XDIC_STEPSABC_GUI` is `ON`; with `OFF` the binary
builds and links on a headless node and `--gui` fails cleanly with a helpful
message.

It reuses existing `cppxdic` code:

- Mask/seed data structures are the same `cppxdic::SeedPoint` and uint8 `cv::Mat`
  mask consumed by `cppxdic::ROIManager`, so the output is drop-in compatible
  with the main pipeline.
- The DLT 11-parameter fit reuses `Utils::DLT11Calibration` (`src/utils.cpp`);
  stereo triangulation uses Eigen, consistent with the rest of the repo.

## Build

The target is **additive and OFF by default**, gated by `BUILD_XDIC_STEPSABC`
(mirrors how other optional apps are gated). The existing `cppxdic` target is
unchanged.

### Local build (with GUI)

```sh
cmake -S . -B build_stepsABC -DBUILD_XDIC_STEPSABC=ON -DXDIC_STEPSABC_GUI=ON
cmake --build build_stepsABC --target xdic_stepsABC
```

### Headless / cluster build (no GUI)

```sh
cmake -S . -B build_stepsABC_headless -DBUILD_XDIC_STEPSABC=ON -DXDIC_STEPSABC_GUI=OFF
cmake --build build_stepsABC_headless --target xdic_stepsABC
```

When `XDIC_STEPSABC_GUI=OFF`, all OpenCV highgui drawing code is `#ifdef`-guarded
out, so the binary needs no display libraries at run time.

## File formats

All mask/seed files are plain text with a `#`-prefixed header line; blank lines
and `#` lines are ignored.

| File              | Format                                                            |
| ----------------- | ----------------------------------------------------------------- |
| `<prefix>.poly`   | `# xdic mask polygon v1`, then one `x y` (pixel) per vertex.       |
| `<prefix>_mask.png` | 8-bit single-channel raster mask (0 outside, 255 inside).       |
| `<prefix>.seed`   | `# xdic seed points v1`, then one `x y` (pixel) per seed.          |

StepC files:

| File           | Format                                                       |
| -------------- | ------------------------------------------------------------ |
| object file    | `# ...`, then one `x y z` (mm) per calibration dot.          |
| image-points   | `# ...`, then one `u v` (pixel) per calibration dot, ordered to match the object file row-for-row. |
| result report  | Human-readable RMS/recon-error report plus the 22 DLT params. |

For the **mask input** in `--no-gui` mode, either a `.poly`/`.txt` polygon file
**or** a raster mask image (e.g. `.png`) is accepted; a polygon is rasterised
against the reference-image size, an image is thresholded to strict 0/255.

## CLI

```
xdic_stepsABC <command> [OPTIONS]

Commands:
  mask-seed     Set up the MASK (polygon ROI) and Seed point(s).
  stepc         Run StepC stereovision / DLT calibration.
  gen-object    Generate cylindrical calibration object 3D coordinates.

Common:
  --gui / --no-gui      Select interactive vs headless mode.
                        (Defaults to GUI when compiled with GUI support, else headless.)
  -h, --help            Show help.

mask-seed:
  --image <path>        Reference image (required for --gui; size source for --no-gui polygon).
  --mask <path>         Mask polygon (.poly) or raster mask (.png) — input for --no-gui.
  --seed <path>         Seed file (.seed) — input for --no-gui.
  --num-seeds <n>       Seeds to collect in GUI mode (default 1).
  --out <prefix>        Output prefix (writes .poly, _mask.png, .seed).

gen-object:
  --radius <mm> --columns <n> --rows <n> --dz <mm>
  --object-out <path>   Output 3D coordinate file.

stepc:
  --object <path>       3D calibration object coordinates.
  --cam1 <id> --cam2 <id>
  --img1 <path> --img2 <path>   2D image-point files per camera.
  --result-out <path>           Optional report output.
```

### GUI mask/seed workflow

In `--gui` mode a window opens on the reference image:

1. **Left-click** to add polygon vertices for the MASK.
2. **Right-click** (or `ENTER`) to close the polygon and switch to seed picking.
3. **Left-click** to place each seed point (`--num-seeds` total).
4. Keys: `u` undo last point, `r` reset, `ENTER` accept, `ESC` cancel.

This mirrors the MATLAB `draw_ref_roi` / `draw_ref_seed` interaction.

## Examples

### Local: interactive mask + seed, save to files

```sh
build_stepsABC/apps/xdic_stepsABC/xdic_stepsABC mask-seed \
    --gui --image ref_cam1.png --num-seeds 1 --out roi_seed_cam1
```

### Cluster: headless mask + seed from files

```sh
xdic_stepsABC mask-seed --no-gui \
    --image ref_cam1.png \
    --mask roi_seed_cam1.poly \
    --seed roi_seed_cam1.seed \
    --out roi_seed_cam1_check
```

### Generate calibration object + run StepC

```sh
xdic_stepsABC gen-object --radius 30 --columns 18 --rows 11 --dz 5 \
    --object-out calib_object.txt

xdic_stepsABC stepc \
    --object calib_object.txt \
    --cam1 1 --img1 cam1_points.txt \
    --cam2 2 --img2 cam2_points.txt \
    --result-out stepc_report.txt
```

### Cluster (sbatch)

```bash
#!/bin/bash
#SBATCH --job-name=xdic_stepc
#SBATCH --time=00:10:00
#SBATCH --cpus-per-task=4
#SBATCH --mem=4G

# Built with -DXDIC_STEPSABC_GUI=OFF — no display needed.
srun ./build_stepsABC_headless/apps/xdic_stepsABC/xdic_stepsABC stepc \
    --object calib_object.txt \
    --cam1 1 --img1 cam1_points.txt \
    --cam2 2 --img2 cam2_points.txt \
    --result-out stepc_report.txt
```

For the mask/seed cluster path, run `mask-seed --gui` once locally to produce the
`.poly`/`.seed` files, copy them to the cluster, then consume them headlessly with
`mask-seed --no-gui` (or feed them directly into the main pipeline).

## StepC port status

Fully ported from `stepC_stereovision_calibration.m` and its helpers:

- `generate_cylindrical_calibration_object_coordinate_file` →
  `StereoCalibration::generateCylindricalObject` (dots on a cylinder: row-major
  ordering over `Z` then `theta`).
- `STEP1_CalcDLTparameters_rewrited` (per-camera 11-parameter DLT fit) →
  `StereoCalibration::calcDLTParameters`, reusing `Utils::DLT11Calibration`, plus
  RMS image-plane reprojection error.
- DLT reconstruction-error metric → `StereoCalibration::reconstructionError`
  (linear DLT triangulation of each calibration point, mean/rms/max/std of the 3D
  residual vs the known object coordinates).

`// TODO(stepC):` gaps — see source comments. The MATLAB script also wires in
camera-by-camera image-point *extraction from marked images* (`camera_info_from_view`)
and the global path/settings bookkeeping (`theGlobalSettings_MNG`). This tool
expects the 2D image points as input files instead of re-deriving them from the
marked calibration images; integrating automatic marker extraction is left as a
follow-up and is noted in `src/stepsABC/stereo_calibration.cpp`.

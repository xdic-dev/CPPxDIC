# CPPxDIC User Guide

How to feed data to CPPxDIC, configure a run, and interpret its outputs. For building see
the [Quick-start](quickstart.md); for extending the code see the
[Developer Guide](developer_guide.md).

---

## 1. Supported inputs and folder conventions

### Video input (implemented)

The default pipeline ingests per-camera videos and extracts single-channel frames
(`Utils::importRawVid`). Expected layout under `data_path`:

```
data_path/
├── rawdata/<subject>/speckles/<material>/protocol/*.mat   # protocol files
└── trial_*/                                                # per-trial camera videos
```

Frames are selected by `idx_frame_start`, `idx_frame_end`, and `frame_jump`; the number of
extracted frames is `(idx_frame_end - idx_frame_start) / frame_jump + 1`. Only the Red
channel is kept (matching MATLAB `iloc(:,:,1)`).

### Image-folder input (partial / stub)

`ImageFolderReader` lists and sorts pre-extracted frames in a directory
(`.png .jpg .jpeg .bmp .tif .tiff`, case-insensitive). It skips hidden files and, by default,
`roi.png` / `ref.png` helpers; frames sort lexicographically (use zero-padded numeric names,
e.g. `frame_000.png`). **Note:** pixel decoding/hand-off is a documented TODO; the reader
currently provides discovery only.

### Calibration, ROI and seeds

A full stereo run also needs DLT calibration `.mat` files (per camera, under
`dic_path/<subject>/calib/<calib_folder_set>/`) and reference ROI/seed masks
(`REF_MASK_*`, `REF_SEED_*`) under `dic_path/<subject>/<material>/`.

---

## 2. Parameter reference

### CLI flags (`cppxdic`)

| Flag | Long form | Argument | Effect |
|------|-----------|----------|--------|
| `-s` | `--subject` | `<id>` | Override subject ID (e.g. `S09`). |
| `-r` | `--reftrial` | `<num>` | Override reference trial number. |
| `-C` | `--config` | `<file>` | Unified config file (default `config/default.cfg`). |
| `-d` | `--dic-params` | `<file>` | DIC parameters file (default `dic_params.txt`). |
| `-n` | `--ncorr-params` | `<file>` | NCorr parameters file (default `ncorr_params.txt`). |
| `-v` | `--viz-params` | `<file>` | Visualization parameters file (default `visualization_params.txt`). |
| `-h` | `--help` | — | Show usage and exit. |

CLI flags are the **highest-priority** tier; they override config files and compiled defaults.

### Config keys

Set in `config/default.cfg` (or `dic_params.txt` / `ncorr_params.txt` /
`visualization_params.txt`). Format: `key = value`, `#` comments, comma-separated lists,
booleans `true/false`/`1/0`/`yes`. For path-like values, avoid trailing spaces/tabs after
the value to prevent path lookup mismatches. Key categories (every key mirrors a field in
`include/config.h`, documented inline in `config/default.cfg`):

| Category | Representative keys |
|----------|---------------------|
| Global | `num_pair`, `frictional_conditions`, `robot_sample_freq`, `vid_sample_freq` |
| Paths | `base_path`, `data_path`, `dic_path` |
| Processing flags | `automatic_process`, `parallel_processing`, `im_filter_mode`, `debug_mode` |
| DIC selection | `subject_id`, `phase_id`, `material_id`, `nfcond_set`, `spddxlcond_set`, `calib_folder_set`, `ref_trial_id` |
| Frame range | `idx_frame_start`, `idx_frame_end`, `frame_jump` |
| Step D (tracking) | `step_d_radius`, `step_d_spacing`, `step_d_cutoff_diffnorm`, `step_d_cutoff_iteration`, `step_d_total_threads`, `step_d_seed_type`, `step_d_replacebadcorr` |
| Step E (matching/3D) | `step_e_radius`, `step_e_analysis_type`, `step_e_auto_update`, `step_e_cutoff_diffnorm` |
| Step F (deformation) | `step_f_temporal_filtering`, `step_f_freq_filt`, `step_f_compute_rbm` |
| NCorr engine | `scalefactor`, `interp`, `dic_config`, `cutoff_corrcoef`, `seeds_are_optimized`, `perspective_interp` |
| Geometry/units | `units_per_pixel`, `subregion_radius`, `limit_grayscale` |
| Output | `data_format` (`mat`/`bin`/`json`), `export_format` (`vtk`/`ply`/`csv`), `fileversion`, `export_each_frame`, `export_frame_list` |
| Visualization | `colormap`, `colormap_range_mode`, `colormap_min/max/levels`, `generate_videos`, `video_fps`, `video_codec`, `show_axes`, `show_colorbar` |
| Stats | `generate_summary_stats`, `stats_format`, `stats_per_frame`, `stats_spatial`, `stats_temporal` |

---

## 3. Output formats and directory structure

The pipeline writes into the working/build directory by default:

```
<workdir>/
├── save/      # pipeline results: DIC_*, strain_*, 3D + deformation files (data_format=bin)
├── save_json/ # JSON serialisations (data_format=json)
├── video/     # overlay videos (when generate_videos = true)
└── images/    # extracted / copied frames
```

| Format | Selected by | Contents |
|--------|-------------|----------|
| `.mat` | `data_format = mat` (default) | MATLAB-compatible structures (DIC2D/3D results, deformation). |
| `.bin` | `data_format = bin` | compact native binary (e.g. `DIC3Dcombined::saveBinary`). |
| `.json` | `data_format = json` | human-readable JSON of the same structures. |
| VTK / PLY / CSV | `export_format` | 3D surface + scalar/vector fields for external viewers (ParaView, MeshLab). |
| overlay videos | `generate_videos = true` | per-field colormapped videos (`video_codec`, `video_fps`). |
| summary stats | `generate_summary_stats = true` | per-frame / spatial / temporal statistics (`stats_format`). |

---

## 4. Troubleshooting

1. **`OpenCV/CGAL/Eigen3/matio not found` during CMake configure.**
   Install the missing dependency (see [Quick-start §1](quickstart.md#1-prerequisites)).
   On macOS, header-only libs sometimes need `export
   CMAKE_PREFIX_PATH=/opt/homebrew:$CMAKE_PREFIX_PATH`.

2. **`MATIO library not found` (fatal).**
   Install matio (`brew install libmatio` / `apt-get install libmatio-dev`). MAT v7.3 files
   additionally require HDF5 (`brew install hdf5` / `libhdf5-dev`).

3. **`OpenMP not found` warning / no parallel speedup.**
   On macOS install libomp (`brew install libomp`); the build expects it under
   `/opt/homebrew/opt/libomp` or `/usr/local/opt/libomp`. Without it the build still works
   but runs single-threaded.

4. **`Config file not found … using compiled defaults`.**
   The path passed to `--config` (default `config/default.cfg`) does not exist; run from the
   repo root or pass an absolute path. This is a warning, not an error — defaults are used.

5. **`Warning: Invalid material_id N` / material resolves to `unknown`.**
   `material_id` is 1-based into `frictional_conditions`; with the default 3-element list,
   valid values are 1–3. Fix `material_id` or extend `frictional_conditions`.

6. **`Error: ROI file not found` when running `proxyncorr`.**
   The 2D driver needs an ROI mask; pass `--roi <path>` or place `roi.png` in the image
   folder. The bundled `ohtcfrp` example ships one at
   `Tools/CppNCorr/test/examples/ohtcfrp/images/roi.png`.

7. **No frames extracted / frame count is 0.**
   Check `idx_frame_start` < `idx_frame_end`, a positive `frame_jump`, and that input videos
   exist under `data_path` with the expected naming. The extracted count is
   `(idx_frame_end - idx_frame_start) / frame_jump + 1`.

8. **Temporal smoothing produces extreme/garbage displacement values.**
   The production temporal filter (`filterTime`, used when `step_f_temporal_filtering = true`)
   has a known numerical-instability bug. As a workaround, set
   `step_f_temporal_filtering = false` until the fix lands.

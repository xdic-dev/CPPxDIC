"""
Parameter schema for the CPPxDIC config/param files.

This is the single source of truth the web forms render from. It mirrors the
canonical C++ ``Config`` struct in ``include/config.h``; the ``help`` strings are
lifted from the inline comments there so the GUI documents each field exactly as
the code does.

Each field is a dict with:
    key     : the INI key as written in the param files (e.g. "maxCorrCoeff")
    type    : one of int|float|bool|string|enum|int_list|double_list|string_list
    default : default value (matches the C++ member initializer)
    help    : one-line description (from config.h)
    enum    : (enum type only) list of allowed string values

Fields are organised into GROUPS, and each group declares which target file(s) it
belongs to so the UI can show only the relevant fields per file.

Target files (match Config::loadFrom* in src/config.cpp):
    dic_params.txt            -> global, paths, processing, DIC, frames, step D/E/F
    ncorr_params.txt          -> ncorr_* algorithm parameters
    visualization_params.txt  -> plot / filter / export / colormap / video / stats
    config/default.cfg        -> unified file: every key above may appear
"""

from __future__ import annotations

# Field type constants
INT = "int"
FLOAT = "float"
BOOL = "bool"
STRING = "string"
ENUM = "enum"
INT_LIST = "int_list"
DOUBLE_LIST = "double_list"
STRING_LIST = "string_list"


def _f(key, type_, default, help_, enum=None):
    field = {"key": key, "type": type_, "default": default, "help": help_}
    if enum is not None:
        field["enum"] = enum
    return field


# StepConfig sub-fields shared by step D / E / F. The actual INI keys are prefixed
# (step_d_radius, step_e_radius, ...) — see _step_group below.
_STEP_FIELDS = [
    ("analysis_type", STRING, "regular", "Step analysis type"),
    ("radius", INT, 40, "Subset radius (pixels)"),
    ("spacing", INT, 10, "Subset spacing (pixels)"),
    ("cutoff_diffnorm", FLOAT, 1e-5, "Convergence cutoff on the diff-norm"),
    ("cutoff_iteration", INT, 100, "Maximum iterations per subset"),
    ("total_threads", INT, 6, "Threads used for this step"),
    ("high_strain_enabled", BOOL, True, "Enable high-strain analysis"),
    ("seed_type", STRING, "seed", "Seed type ('seed' or 'auto')"),
    ("auto_update", BOOL, True, "Auto-update reference during tracking"),
    ("step_ref_change", INT, 10, "Frame interval before reference change"),
    ("initial_seed", INT_LIST, [], "Initial seed [x, y] (empty = auto)"),
]


def _step_group(prefix: str, title: str, radius_default: int) -> dict:
    fields = []
    for name, type_, default, help_ in _STEP_FIELDS:
        d = radius_default if name == "radius" else default
        fields.append(_f(f"{prefix}_{name}", type_, d, help_))
    return {"title": title, "files": ["dic_params.txt", "config/default.cfg"], "fields": fields}


GROUPS = [
    {
        "title": "Global",
        "files": ["dic_params.txt", "config/default.cfg"],
        "fields": [
            _f("frictional_conditions", STRING_LIST, ["glass", "coating", "coating_oil"],
               "Frictional conditions (comma-separated list)"),
            _f("num_pair", INT, 2, "Number of stereopairs"),
            _f("robot_sample_freq", FLOAT, 1000.0, "Robot sampling frequency (Hz)"),
            _f("vid_sample_freq", FLOAT, 50.0, "Image sampling frequency (Hz)"),
        ],
    },
    {
        "title": "Paths",
        "files": ["dic_params.txt", "config/default.cfg"],
        "fields": [
            _f("base_path", STRING, "", "Base path for the project"),
            _f("data_path", STRING, "", "Location of video and protocol files (empty = <base_path>/Data)"),
            _f("dic_path", STRING, "", "Location of DIC output data (empty = <base_path>/DIC_Output)"),
        ],
    },
    {
        "title": "Processing",
        "files": ["dic_params.txt", "config/default.cfg"],
        "fields": [
            _f("im_filter_mode", BOOL, True, "Image filter mode"),
            _f("automatic_process", BOOL, True, "Automatic processing flag"),
            _f("parallel_processing", BOOL, True, "Parallel processing flag"),
            _f("debug_mode", BOOL, False, "Debug mode flag"),
            _f("log_level", ENUM, "", "Console log threshold",
               enum=["", "trace", "debug", "info", "warn", "error", "off"]),
            _f("log_file", STRING, "", "Path to write a full-detail log (empty = none)"),
        ],
    },
    {
        "title": "DIC analysis",
        "files": ["dic_params.txt", "config/default.cfg"],
        "fields": [
            _f("subject_id", STRING, "S09", "Subject identifier"),
            _f("phase_id", STRING, "loading", "Phase identifier"),
            _f("material_id", INT, 2, "Material identifier (index into frictional_conditions)"),
            _f("nfcond_set", INT_LIST, [5], "Number of conditions"),
            _f("spddxlcond_set", DOUBLE_LIST, [0.04, 0.08], "Speed conditions"),
            _f("calib_folder_set", STRING, "2", "Calibration folder set"),
            _f("ref_trial_id", INT, 5, "Reference trial number"),
            _f("units_per_pixel", FLOAT, 0.2, "Scale, e.g. mm per pixel"),
            _f("subregion_radius", INT, 20, "Default subset radius (pixels)"),
            _f("limit_grayscale", INT, 70, "Grayscale limit threshold"),
            _f("data_format", ENUM, "mat", "Pipeline I/O data format",
               enum=["mat", "bin", "json"]),
        ],
    },
    {
        "title": "Frames",
        "files": ["dic_params.txt", "config/default.cfg"],
        "fields": [
            _f("idx_frame_start", INT, 10, "First frame index"),
            _f("idx_frame_end", INT, 150, "Last frame index"),
            _f("frame_jump", INT, 1, "Frame stride"),
        ],
    },
    _step_group("step_d", "Step D — initial tracking", 40),
    _step_group("step_e", "Step E — matching", 60),
    _step_group("step_f", "Step F — combined/final", 40),
    {
        "title": "Step D/E/F extras",
        "files": ["dic_params.txt", "config/default.cfg"],
        "fields": [
            _f("step_e_distortion_removal", BOOL, False, "Remove distortion from 2D points"),
            _f("step_d_replacebadcorr", BOOL, True, "Replace bad correlation subsets (slow)"),
            _f("step_f_temporal_filtering", BOOL, True, "Enable temporal filtering of displacements"),
            _f("step_f_freq_filt", FLOAT, 10.0, "Low-pass cutoff frequency (Hz) for temporal filter"),
            _f("step_f_compute_rbm", BOOL, False, "Compute rigid body motion (RBM) and ARBM deformation"),
        ],
    },
    {
        "title": "NCorr algorithm",
        "files": ["ncorr_params.txt", "config/default.cfg"],
        "fields": [
            _f("ncorr_scalefactor", INT, 3, "Scale factor"),
            _f("ncorr_interp", STRING, "quintic_bspline_precompute", "Interpolation method"),
            _f("ncorr_subregion", ENUM, "circle", "Subregion shape",
               enum=["circle", "square"]),
            _f("ncorr_dic_config", STRING, "no_update", "DIC config mode"),
            _f("ncorr_cutoff_corrcoef", FLOAT, 10.0, "Correlation cutoff"),
            _f("ncorr_roi_update_mode", STRING, "none", "ROI update mode"),
            _f("ncorr_accumulation_mode", STRING, "none", "Accumulation mode"),
            _f("ncorr_save_disps_steps", BOOL, False, "Save intermediate disps"),
            _f("ncorr_perspective_interp", BOOL, False, "Perspective interpolation"),
            _f("ncorr_units", STRING, "mm", "Units string"),
            _f("ncorr_seeds_are_optimized", BOOL, True, "Optimized seeds"),
            _f("ncorr_cutoff_max_diffnorm", FLOAT, 1e-5, "Max diff norm cutoff"),
            _f("ncorr_cutoff_max_corrcoef", FLOAT, 10.0, "Max corr coef cutoff"),
            _f("ncorr_threads", INT, 4, "Number of threads"),
            _f("ncorr_use_exact_matlab", BOOL, False,
               "Use exact_matlab_DIC_analysis_* instead of matlab_DIC_analysis_*"),
        ],
    },
    {
        "title": "Visualization — plot map",
        "files": ["visualization_params.txt", "config/default.cfg"],
        "fields": [
            _f("showvisu", BOOL, False, "Boolean for visualization"),
            _f("mapLogic", BOOL, True, "Enable 3D map plotting"),
            _f("plotopt", STRING_LIST, ["Epc1", "Epc2"], "Face measurement plot options"),
            _f("deftype", ENUM, "both", "Derivative order", enum=["cum", "rate", "both"]),
            _f("viewplot", ENUM, "below", "Initial plot perspective",
               enum=["lateralR", "below", "lateralL", "front"]),
            _f("contactAreaLogic", BOOL, False, "Contact area delimitation"),
            _f("gapLogic", BOOL, True, "Suppress stitched pair boundary"),
            _f("maxCorrCoeff", FLOAT, 10.0, "Max admissible correlation coefficient"),
            _f("format", ENUM, "small", "Plot format", enum=["small", "normal"]),
            _f("showRobotLogic", BOOL, True, "Plot kinematic/dynamic data"),
            _f("showImgLogic", BOOL, False, "Plot image"),
            _f("camNbr", INT, 0, "Camera number (0 or specific camera)"),
            _f("FaceAlpha", FLOAT, 1.0, "Face transparency (0-1)"),
            _f("lineColor", STRING, "none", "Line color: 'none'|'k'|'b', etc."),
            _f("quiverScaleFactor", FLOAT, 20.0, "Arrow scale factor for vector plots"),
        ],
    },
    {
        "title": "Visualization — filtering",
        "files": ["visualization_params.txt", "config/default.cfg"],
        "fields": [
            _f("smoothTimeLogic", BOOL, True, "Smooth time of the maps"),
            _f("filterFreq", FLOAT, 5.0, "Cut-off frequency (Hz)"),
            _f("smoothSpaceLogic", BOOL, True, "Smooth space of the maps"),
            _f("smoothPar_n", INT, 30, "Space filtering parameter 1"),
            _f("smoothPar_sigma", FLOAT, 2.0, "Space filtering parameter 2"),
        ],
    },
    {
        "title": "Visualization — export",
        "files": ["visualization_params.txt", "config/default.cfg"],
        "fields": [
            _f("export_format", ENUM, "vtk", "Export format", enum=["vtk", "ply", "csv"]),
            _f("export_each_frame", BOOL, False, "Export each frame separately"),
            _f("export_frame_list", INT_LIST, [], "Specific frames to export (empty = all)"),
            _f("vtk_format", ENUM, "ascii", "VTK format", enum=["ascii", "binary"]),
            _f("vtk_include_scalars", BOOL, True, "Include scalar data in VTK"),
            _f("vtk_include_vectors", BOOL, True, "Include vector data in VTK"),
            _f("ply_format", ENUM, "ascii", "PLY format", enum=["ascii", "binary"]),
            _f("ply_include_colors", BOOL, True, "Include colors in PLY"),
            _f("csv_delimiter", STRING, ",", "CSV delimiter"),
            _f("csv_include_header", BOOL, True, "Include header in CSV"),
        ],
    },
    {
        "title": "Visualization — video & colormap",
        "files": ["visualization_params.txt", "config/default.cfg"],
        "fields": [
            _f("generate_videos", BOOL, False, "Generate videos"),
            _f("video_fps", INT, 10, "Video FPS"),
            _f("video_codec", STRING, "MJPG", "Video codec"),
            _f("video_quality", INT, 90, "Video quality (0-100)"),
            _f("video_alpha", FLOAT, 1.0, "Video transparency"),
            _f("colormap", STRING, "jet", "Colormap name"),
            _f("colormap_range_mode", ENUM, "auto", "Range mode", enum=["auto", "manual"]),
            _f("colormap_min", FLOAT, 0.0, "Manual colormap min"),
            _f("colormap_max", FLOAT, 1.0, "Manual colormap max"),
            _f("colormap_levels", INT, 256, "Number of color levels"),
        ],
    },
    {
        "title": "Visualization — statistics & mesh",
        "files": ["visualization_params.txt", "config/default.cfg"],
        "fields": [
            _f("generate_summary_stats", BOOL, True, "Generate summary statistics"),
            _f("stats_format", ENUM, "txt", "Stats format", enum=["txt", "csv", "json"]),
            _f("stats_per_frame", BOOL, True, "Per-frame statistics"),
            _f("stats_spatial", BOOL, True, "Spatial statistics"),
            _f("stats_temporal", BOOL, True, "Temporal statistics"),
            _f("mesh_decimation", FLOAT, 1.0, "Mesh decimation factor (1.0 = none)"),
            _f("mesh_smoothing", BOOL, False, "Enable mesh smoothing"),
            _f("mesh_smoothing_iterations", INT, 10, "Smoothing iterations"),
            _f("show_axes", BOOL, True, "Show axes in visualization"),
            _f("show_colorbar", BOOL, True, "Show colorbar"),
            _f("show_grid", BOOL, False, "Show grid"),
            _f("background_color", STRING, "white", "Background color"),
        ],
    },
]

# Valid target files the UI may load/save.
TARGET_FILES = [
    "dic_params.txt",
    "ncorr_params.txt",
    "visualization_params.txt",
    "config/default.cfg",
]


def groups_for_file(target_file: str) -> list[dict]:
    """Return the groups whose fields belong in the given target file."""
    return [g for g in GROUPS if target_file in g["files"]]


def field_index() -> dict[str, dict]:
    """Flat key -> field-definition map across all groups."""
    idx: dict[str, dict] = {}
    for g in GROUPS:
        for fld in g["fields"]:
            idx[fld["key"]] = fld
    return idx

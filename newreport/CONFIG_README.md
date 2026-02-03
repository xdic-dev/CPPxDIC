# Configuration System Documentation

## Overview

The CPPxDIC application now supports runtime configuration through parameter files, eliminating the need to recompile when changing analysis parameters. The configuration system follows a hierarchical loading strategy with command-line overrides.

## Configuration Loading Hierarchy

The configuration is loaded in the following order (later sources override earlier ones):

1. **Default values** - Hard-coded defaults in the `Config` class
2. **`dic_params.txt`** - Main DIC parameters file (if exists)
3. **`ncorr_params.txt`** - NCorr-specific parameters (if exists, overrides DIC params)
4. **`visualization_params.txt`** - Visualization parameters (if exists)
5. **Legacy methods** - `loadGlobalParams()` and `loadDicParams()` for backward compatibility
6. **Command-line arguments** - Highest priority overrides

## Parameter Files

### 1. `dic_params.txt` - Main DIC Parameters

Contains global and DIC analysis parameters for all three steps (d, e, f):

**Key sections:**
- **Global parameters**: frictional conditions, stereo pairs, sampling frequencies
- **Path definitions**: base_path, data_path, dic_path
- **Processing flags**: im_filter_mode, automatic_process, parallel_processing, debug_mode
- **DIC analysis parameters**: subject_id, phase_id, material_id, trial settings
- **Frame settings**: start/end frames, frame jump
- **Step-specific parameters** (d, e, f): radius, spacing, cutoff thresholds, threading
- **Units and calibration**: units_per_pixel, subregion_radius
- **Output control**: fileversion, generate_mat_files, cleanup_cache_bins

**Example:**
```
subject_id = S09
phase_id = loading
material_id = 2
ref_trial_id = 5
idx_frame_start = 1
idx_frame_end = 150
frame_jump = 1
debug_mode = true
```

### 2. `ncorr_params.txt` - NCorr Engine Parameters

Based on `DIC_analysis_input` class and ncorr library configuration:

**Key sections:**
- **Image/ROI paths**: folder, roi, ref, output
- **DIC analysis parameters**: scalefactor, interpolation type, subregion shape, radius, threads
- **DIC configuration**: dic_config, correlation thresholds
- **ROI update mode**: SKIP_ALL vs SKIP_INVALID
- **Accumulation mode**: ON_THE_FLY vs POST_PROCESS
- **Strain analysis**: strain_subregion, strain_radius
- **Algorithm mode**: auto, sequential, parallel with seed support
- **Output options**: save_json, save_binary, save_videos

**Example:**
```
scalefactor = 3
interp = QUINTIC_BSPLINE_PRECOMPUTE
subregion = CIRCLE
radius = 20
threads = 4
units_per_pixel = 0.2
debug = false
```

**Note:** Parameters in this file override corresponding parameters from `dic_params.txt` (e.g., `units_per_pixel`, `debug`, `radius`).

### 3. `visualization_params.txt` - Visualization Parameters

Controls plot and export settings after DIC analysis:

**Key sections:**
- **General settings**: showvisu
- **3D map plotting**: mapLogic, plotopt (Epc1, Epc2, etc.), deftype, viewplot
- **Filtering**: temporal and spatial smoothing, correlation filtering
- **Plot overlays**: robot data, images
- **Appearance**: transparency, colors, scale factors
- **Export settings**: format (vtk/ply/csv), frame selection
- **Video generation**: fps, codec, quality
- **Colormap settings**: type, range, levels

**Example:**
```
showvisu = false
mapLogic = true
plotopt = Epc1,Epc2
smoothTimeLogic = true
filterFreq = 5.0
export_format = vtk
```

## Command-Line Usage

### Basic Usage

```bash
./cppxdic
```

Loads default parameter files from the current directory.

### Override Subject and Reference Trial

```bash
./cppxdic --subject S10 --reftrial 3
```

or using short options:

```bash
./cppxdic -s S10 -r 3
```

### Specify Custom Parameter Files

```bash
./cppxdic --dic-params custom_dic.txt --ncorr-params custom_ncorr.txt
```

or using short options:

```bash
./cppxdic -d custom_dic.txt -n custom_ncorr.txt -v custom_viz.txt
```

### Combined Example

```bash
./cppxdic -s S08 -r 5 -d experiments/exp1_dic.txt
```

### Help

```bash
./cppxdic --help
```

## Command-Line Options

| Short | Long | Argument | Description |
|-------|------|----------|-------------|
| `-s` | `--subject` | `<id>` | Override subject ID (e.g., S09) |
| `-r` | `--reftrial` | `<num>` | Override reference trial number |
| `-d` | `--dic-params` | `<file>` | DIC parameters file (default: dic_params.txt) |
| `-n` | `--ncorr-params` | `<file>` | NCorr parameters file (default: ncorr_params.txt) |
| `-v` | `--viz-params` | `<file>` | Visualization parameters file (default: visualization_params.txt) |
| `-h` | `--help` | - | Show help message |

## Parameter File Format

All parameter files use the same simple format:

```
# Comments start with #
# Format: key = value

# String values
subject_id = S09

# Integer values
ref_trial_id = 5

# Floating-point values
units_per_pixel = 0.2

# Boolean values (true/false, 1/0, yes/no)
debug_mode = true

# Comma-separated lists
plotopt = Epc1,Epc2,J
nfcond_set = 5,10,15
spddxlcond_set = 0.04,0.08,0.12

# Empty values use defaults
data_path = 
```

## Configuration Strategy Examples

### Example 1: Quick Subject Change

Keep all parameters the same but analyze a different subject:

```bash
./cppxdic --subject S11
```

No need to edit any files!

### Example 2: Different Trial Configurations

Create multiple DIC parameter files for different experiments:

```
experiments/
  ├── loading_dic.txt
  ├── unloading_dic.txt
  └── cyclic_dic.txt
```

Run different experiments:

```bash
./cppxdic -d experiments/loading_dic.txt
./cppxdic -d experiments/unloading_dic.txt
```

### Example 3: Override for Quick Test

Test with a smaller frame range without editing files:

Edit `dic_params.txt` temporarily or create a test file:

```
# test_dic.txt
idx_frame_start = 1
idx_frame_end = 10
frame_jump = 1
debug_mode = true
```

```bash
./cppxdic -d test_dic.txt
```

### Example 4: Different Visualization Settings

Keep analysis parameters constant but change visualization:

```bash
# Run analysis with default visualization
./cppxdic

# Re-run with different visualization (if supported)
# Edit visualization_params.txt and re-run visualization step
```

## Implementation Details

### Config Class Methods

```cpp
// Load parameter files
bool loadFromDicParamsFile(const std::string& filepath = "dic_params.txt");
bool loadFromNcorrParamsFile(const std::string& filepath = "ncorr_params.txt");
bool loadFromVisualizationParamsFile(const std::string& filepath = "visualization_params.txt");

// Command-line overrides
void overrideSubject(const std::string& subject);
void overrideRefTrial(int trial);
```

### Loading Sequence in main.cpp

```cpp
Config config;

// Load parameter files (in order)
config.loadFromDicParamsFile(dic_params_file);
config.loadFromNcorrParamsFile(ncorr_params_file);
config.loadFromVisualizationParamsFile(viz_params_file);

// Legacy methods for backward compatibility
config.loadGlobalParams();
config.loadDicParams();

// Apply command-line overrides
if (!subject_override.empty()) {
    config.overrideSubject(subject_override);
}
if (reftrial_override >= 0) {
    config.overrideRefTrial(reftrial_override);
}

// Update derived variables
config.updateVariables();
```

## Benefits

1. **No recompilation needed** - Change parameters without rebuilding
2. **Flexible configuration** - Multiple parameter files for different experiments
3. **Quick overrides** - Command-line arguments for rapid testing
4. **Hierarchical control** - Fine-grained control over parameter precedence
5. **Backward compatible** - Existing code still works with default values
6. **Self-documenting** - Parameter files include comments explaining each option

## Troubleshooting

### Parameter file not found

If a parameter file doesn't exist, the application will use default values and print:
```
DIC params file not found: dic_params.txt, using defaults
```

This is not an error - the application will continue with defaults.

### Invalid parameter values

If a parameter value cannot be parsed, a warning is printed:
```
Warning: Could not parse int value: abc
```

The parameter will be skipped and the previous value retained.

### Override not working

Check the loading order - command-line arguments have the highest priority and should always work. If a parameter isn't being overridden, ensure:

1. The parameter name matches exactly (case-sensitive)
2. The parameter is supported in the respective file
3. The file is being loaded (check console output)

## Future Enhancements

Potential improvements to the configuration system:

- JSON/YAML format support for complex nested parameters
- Configuration validation with error reporting
- Parameter templates for common use cases
- Environment variable support
- Configuration file generation from current settings

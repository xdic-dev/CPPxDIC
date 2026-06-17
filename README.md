# CPPXDIC - C++ Digital Image Correlation Library

[![CI](https://github.com/xdic-dev/CPPxDIC/actions/workflows/ci.yml/badge.svg)](https://github.com/xdic-dev/CPPxDIC/actions/workflows/ci.yml)
[![codecov](https://codecov.io/gh/xdic-dev/CPPxDIC/branch/main/graph/badge.svg)](https://codecov.io/gh/xdic-dev/CPPxDIC)
[![Docs](https://img.shields.io/badge/docs-Doxygen-blue)](https://xdic-dev.github.io/CPPxDIC/)
[![Release](https://img.shields.io/github/v/release/xdic-dev/CPPxDIC?include_prereleases&sort=semver)](https://github.com/xdic-dev/CPPxDIC/releases)
[![License](https://img.shields.io/github/license/xdic-dev/CPPxDIC)](https://github.com/xdic-dev/CPPxDIC/blob/main/LICENSE)

CPPXDIC is a C++ equivalent of the Matlab xDIC library for fingertip 3D reconstruction using Digital Image Correlation (DIC). It uses the ncorr C++ library as its core DIC engine.

## Features

- **2D DIC Analysis**: Performs digital image correlation on image sequences
- **3D Reconstruction**: Stereo reconstruction from multiple camera views
- **Deformation Analysis**: Strain analysis and deformation measurements
- **Multi-threaded Processing**: Parallel processing capabilities
- **Cross-platform**: Works on Linux, macOS, and Windows

## Dependencies

- **CMake** (>= 3.16)
- **OpenCV** (for image processing)
- **C++17 compatible compiler** (GCC 10+, Clang 12+, MSVC 2019+)
- **ncorr C++ library** (included in the project)

## Building the Project

### Prerequisites

1. Install OpenCV:
   ```bash
   # On Ubuntu/Debian
   sudo apt-get install libopencv-dev
   
   # On macOS with Homebrew
   brew install opencv
   
   # On Windows, download from opencv.org
   ```

2. Ensure the ncorr C++ library is built:
   ```bash
   cd ../ncorr_2D_cpp-master
   mkdir -p build && cd build
   cmake ..
   make
   ```

### Build Steps

1. Create build directory:
   ```bash
   mkdir build && cd build
   ```

2. Configure with CMake:
   ```bash
   cmake ..
   ```

3. Build the project:
   ```bash
   make -j$(nproc)
   ```

### Alternative Build (if CMake fails)

If CMake configuration fails, you can build manually:

```bash
# Compile manually
g++ -std=c++17 -O2 -Wall -Wextra \
    -Iinclude -I../ncorr_2D_cpp-master/include \
    src/*.cpp \
    -lopencv_core -lopencv_imgproc -lopencv_imgcodecs -lopencv_highgui \
    -L../ncorr_2D_cpp-master/lib -lncorr \
    -o cppxdic
```

## Usage

### Basic Usage

```bash
# Run the DIC analysis
./cppxdic
```

### Configuration

The program uses configuration files equivalent to the Matlab version:
- Global parameters (paths, processing flags)
- DIC parameters (subject ID, trial settings, frame ranges)

You can modify the configuration in `src/config.cpp` or create a configuration file loader.

### Input Data Structure

The program expects the following directory structure:
```
data_path/
├── rawdata/
│   └── S09/
│       └── speckles/
│           └── coating/
│               └── protocol/
│                   └── *.mat
└── trial_*/
    └── *.png (or other image formats)

dic_path/
├── S09/
│   ├── coating/
│   │   ├── REF_MASK_005_loading_pair1.mat
│   │   └── REF_SEED_005_loading_pair1.mat
│   └── calib/
│       └── 2/
│           └── *cam_*.mat
```

## Project Structure

```
cppxdic/
├── CMakeLists.txt          # Build configuration
├── README.md              # This file
├── include/               # Header files
│   ├── config.h          # Configuration management
│   ├── dic_analysis.h    # Main DIC analysis class
│   └── utils.h           # Utility functions
└── src/                  # Source files
    ├── main.cpp          # Entry point
    ├── config.cpp        # Configuration implementation
    ├── dic_analysis.cpp  # DIC analysis implementation
    └── utils.cpp         # Utility functions implementation
```

## Key Classes

### Config
Manages all configuration parameters equivalent to `global_param.m` and `dic_param.m`:
- Path settings
- Processing parameters
- Trial and frame settings

### DicAnalysis
Main analysis class equivalent to `dic_analysis.m`:
- 2D DIC processing
- 3D reconstruction
- Deformation analysis

### Utils
Utility functions equivalent to various Matlab utilities:
- File system operations
- Data validation
- String processing

## Comparison with Matlab xDIC

| Feature | Matlab xDIC | CPPXDIC |
|---------|-------------|---------|
| Entry Point | `xdic.m` | `main.cpp` |
| Configuration | `global_param.m`, `dic_param.m` | `Config` class |
| Main Analysis | `dic_analysis.m` | `DicAnalysis` class |
| Utilities | Various `.m` files | `Utils` class |
| DIC Engine | ncorr Matlab | ncorr C++ |

## Performance

CPPXDIC offers several performance advantages over the Matlab version:
- **Faster execution**: Native C++ performance
- **Lower memory usage**: Efficient memory management
- **Better parallelization**: Multi-threaded processing
- **No Matlab license required**: Standalone executable

## Troubleshooting

### Common Issues

1. **OpenCV not found**:
   ```bash
   export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH
   ```

2. **ncorr library not found**:
   - Ensure ncorr is built: `ls ../ncorr_2D_cpp-master/lib/libncorr.a`
   - Check include path: `ls ../ncorr_2D_cpp-master/include/ncorr.h`

3. **C++17 not supported**:
   - Use a newer compiler (GCC 10+, Clang 12+, MSVC 2019+); the project requires
     C++17 for `std::filesystem` support (set in `CMakeLists.txt`).

### Debug Mode

Enable debug output by setting `debug_mode = true` in the configuration.

## Contributing

1. Follow the existing code style
2. Add unit tests for new features
3. Update documentation
4. Ensure compatibility with the ncorr C++ library

## License

This project follows the same license as the original ncorr library and Matlab xDIC.

## Acknowledgments

- Based on the Matlab xDIC library
- Uses the ncorr C++ Digital Image Correlation library
- Inspired by the original ncorr Matlab implementation

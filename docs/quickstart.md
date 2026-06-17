# CPPxDIC Quick-start

CPPxDIC is a C++ port of the MATLAB **xDIC** library for fingertip 3D reconstruction
using Digital Image Correlation (DIC). It ingests image sequences (or videos) from
calibrated cameras and produces 3D surfaces with deformation / strain fields. The 2D DIC
engine is the vendored **CppNCorr** submodule, built in-tree.

---

## 1. Prerequisites

| Requirement | Version | Notes |
|-------------|---------|-------|
| CMake | >= 3.16 | |
| C++ compiler | C++17 | GCC 10+, Clang 12+, or MSVC 2019+ (needs `std::filesystem`) |
| OpenCV | 4.x | image/video I/O and processing |
| CGAL | any recent | Delaunay triangulation |
| Eigen3 | 3.x | matrix math for strain computation |
| matio | any recent | MATLAB `.mat` read/write |
| HDF5 | C library | MAT v7.3 support |
| FFTW3 | 3.x | required by CppNCorr |
| SuiteSparse, LAPACK, BLAS | — | sparse / dense linear algebra |
| nlohmann/json | header-only | JSON serialisation |
| OpenMP | — | parallel DIC (libomp on macOS) |

### Install dependencies

```bash
# Ubuntu / Debian
sudo apt-get update
sudo apt-get install -y cmake g++ \
    libopencv-dev libcgal-dev libeigen3-dev libmatio-dev libhdf5-dev \
    libfftw3-dev libsuitesparse-dev liblapack-dev libblas-dev \
    nlohmann-json3-dev libomp-dev

# macOS (Homebrew)
brew install cmake opencv cgal eigen libmatio hdf5 fftw suite-sparse \
    nlohmann-json libomp
```

### Initialise submodules

CppNCorr (the 2D DIC engine) and the MATLAB reference project are git submodules:

```bash
git submodule update --init --recursive
```

---

## 2. Build

```bash
cmake -S . -B build
cmake --build build -j
```

This builds the default `cppxdic` executable (camera-pairs reconstruction mode) and the
`generate_ncorr_bin` helper.

### Optional targets (default OFF)

| Target | CMake flag | Purpose |
|--------|------------|---------|
| `proxyncorr` | `-DBUILD_PROXYNCORR=ON` | thin pass-through driver around the CppNCorr 2D DIC engine for quick validation (`apps/proxyncorr/`) |
| `singledic` | `-DBUILD_SINGLEDIC=ON` | single-camera 2D DIC stub (`apps/singledic/`) |
| test suite | `-DBUILD_TESTING=ON` (default ON) | in-tree CTest suite under `tests/cppxdic_suite/` |

```bash
# Example: build with the proxyncorr validation driver
cmake -S . -B build -DBUILD_PROXYNCORR=ON
cmake --build build -j --target proxyncorr
```

### Reconstruction mode (compile-time)

The `xdic` reconstruction mode is selected at configure time:

```bash
cmake -S . -B build -DXDIC_MODE=camerapairs   # default, fully implemented
# -DXDIC_MODE=mirrored   # single camera + mirror rig (stub)
# -DXDIC_MODE=multi      # N-camera generalisation (placeholder, intentionally errors)
```

---

## 3. Minimal run

The default executable reads parameters from the three-tier override chain
(CLI > config file > compiled defaults) and runs the full fingertip pipeline:

```bash
./build/cppxdic --config config/default.cfg --subject S09 --reftrial 5
./build/cppxdic --help    # list all CLI flags
```

A full stereo run requires a subject dataset (videos + calibration + protocol +
ROI/seed `.mat`). See the [User Guide](user_guide.md) for input conventions.

### 2D DIC on the bundled `ohtcfrp` example

The lightweight `ohtcfrp` example (12 frames + `roi.png`) ships with the CppNCorr submodule
and exercises the 2D path only:

```bash
cmake -S . -B build -DBUILD_PROXYNCORR=ON && cmake --build build -j --target proxyncorr
./build/proxyncorr \
    --folder Tools/CppNCorr/test/examples/ohtcfrp/images \
    --roi    Tools/CppNCorr/test/examples/ohtcfrp/images/roi.png \
    --mode   sequential \
    --output /tmp/ohtcfrp_out
```

---

## 4. Run the tests

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build -j
cd build && ctest --output-on-failure
```

The suite uses a tiny in-tree assertion harness (no GoogleTest/Catch2). Stereo-dependent
tests are registered as SKIPPED placeholders until a stereo fixture is provided. See the
[Developer Guide](developer_guide.md#running-tests-locally) for details.

---

## 5. Reference papers

- [xDIC: An open-source toolbox for digital image correlation and 3D reconstruction](DOI_PLACEHOLDER)
- [Ncorr: Open-Source 2D Digital Image Correlation Matlab Software](DOI_PLACEHOLDER)
- [Surface-marker cluster design criteria for 3-D bone movement reconstruction](DOI_PLACEHOLDER)

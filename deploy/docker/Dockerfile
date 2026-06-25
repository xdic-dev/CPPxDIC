# =============================================================================
# Dockerfile for CPPxDIC - C++ Digital Image Correlation
#
# Multi-stage build:
#   Stage 1 (builder): installs deps, compiles the project
#   Stage 2 (runtime): copies only binaries + shared libs for a smaller image
#
# Build:
#   docker build -t cppxdic -f deploy/docker/Dockerfile .
#
# Run:
#   docker run --rm \
#     -v /path/to/configs:/configs:ro \
#     -v /path/to/data:/data:ro \
#     -v /path/to/output:/output \
#     cppxdic --dic-params /configs/dic_params.txt
# =============================================================================

# --------------- Stage 1: build ---------------
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

# Packages installed:
#   build-essential, cmake, g++, git, pkg-config  - build tools
#   libopencv-dev                                  - OpenCV (core, imgproc, imgcodecs, highgui, videoio)
#   libfftw3-dev                                   - FFTW3
#   libmatio-dev                                   - MATIO (MATLAB .mat I/O)
#   libhdf5-dev                                    - HDF5 (required by matio for v7.3 format)
#   libcgal-dev                                    - CGAL (Delaunay triangulation)
#   libeigen3-dev                                  - Eigen3 (header-only linear algebra)
#   libsuitesparse-dev                             - SuiteSparse (spqr, cholmod, amd, colamd)
#   liblapack-dev, libblas-dev, libopenblas-dev    - BLAS / LAPACK
#   libomp-dev                                     - OpenMP
#   nlohmann-json3-dev                             - nlohmann/json (header-only JSON)
#   pthreads is included in libc (no extra package needed)
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        g++ \
        git \
        pkg-config \
        ca-certificates \
        wget \
        libopencv-dev \
        libfftw3-dev \
        libmatio-dev \
        libhdf5-dev \
        libcgal-dev \
        libeigen3-dev \
        libsuitesparse-dev \
        liblapack-dev \
        libblas-dev \
        libopenblas-dev \
        libomp-dev \
        nlohmann-json3-dev \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# Copy source into builder
COPY . /opt/cppxdic/src

# Build
#   The optional apps are enabled here (default OFF in a normal local build) so
#   they are baked into the image alongside cppxdic and installed onto PATH:
#     BUILD_SINGLEDIC          single-camera 2D DIC
#     BUILD_PROXYNCORR         pass-through DIC validation driver
#     BUILD_GEN_SUBJECT_TRIAL  subject_trial.csv generator (for SLURM arrays)
#     BUILD_XDIC_STEPSABC      MASK+Seed setup + StepC DLT calibration
#   xdic_stepsABC is built headless (XDIC_STEPSABC_GUI=OFF): no display backend
#   is needed at run time; its --gui mode fails cleanly with a helpful message.
WORKDIR /opt/cppxdic/src
RUN rm -rf build tests/build Tools/CppNCorr/lib compile_commands.json \
    && mkdir -p build && cd build \
    && cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/opt/cppxdic \
        -DCMAKE_CXX_STANDARD=17 \
        -DBUILD_SINGLEDIC=ON \
        -DBUILD_PROXYNCORR=ON \
        -DBUILD_GEN_SUBJECT_TRIAL=ON \
        -DBUILD_XDIC_STEPSABC=ON \
        -DXDIC_STEPSABC_GUI=OFF \
    && make -j$(nproc) \
    && make install

# --------------- Stage 2: runtime ---------------
FROM ubuntu:22.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

# Install only runtime libraries (no -dev packages)
RUN apt-get update && apt-get install -y --no-install-recommends \
        libopencv-core4.5d \
        libopencv-imgproc4.5d \
        libopencv-imgcodecs4.5d \
        libopencv-highgui4.5d \
        libopencv-videoio4.5d \
        libfftw3-3 \
        libmatio11 \
        libhdf5-103-1 \
        libcgal-dev \
        libeigen3-dev \
        libsuitesparseconfig5 \
        libspqr2 \
        libcholmod3 \
        libamd2 \
        libcolamd2 \
        liblapack3 \
        libblas3 \
        libopenblas0 \
        libomp5 \
        libgomp1 \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

# Copy built binaries from builder (cppxdic + the optional apps)
COPY --from=builder /opt/cppxdic/bin /opt/cppxdic/bin

# App dispatcher: `docker run <image> <app> [args]` selects a binary; with no
# app name it falls through to cppxdic, so legacy invocations keep working.
COPY deploy/entrypoint.sh /opt/cppxdic/entrypoint.sh
RUN chmod +x /opt/cppxdic/entrypoint.sh

# Environment
ENV PATH=/opt/cppxdic/bin:$PATH
ENV LC_ALL=C
ENV LANG=C
ENV OMP_NUM_THREADS=4

# Create mount points
RUN mkdir -p /configs /data /output

WORKDIR /output

ENTRYPOINT ["/opt/cppxdic/entrypoint.sh"]
CMD ["--help"]

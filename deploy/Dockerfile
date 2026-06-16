# =============================================================================
# CPPxDIC — Docker image for the default `cppxdic` camera-pairs target.
#
# Builds the fully-working camera-pairs reconstruction mode (XDIC_MODE=camerapairs)
# with the vendored CppNCorr submodule compiled in-tree.
#
# Build context: the repository ROOT (so the submodules under Tools/ are available).
#   docker build -f deploy/Dockerfile -t cppxdic:latest .
#
# Run:
#   docker run --rm -v "$PWD/data:/data" cppxdic:latest --help
#
# Image-tag convention: see deploy/README.md.
# =============================================================================

FROM ubuntu:24.04 AS build

ENV DEBIAN_FRONTEND=noninteractive

# Heavy DIC/3D dependencies (mirrors docs/quickstart.md).
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake git ca-certificates pkg-config \
        libopencv-dev libcgal-dev libeigen3-dev libmatio-dev libhdf5-dev \
        libfftw3-dev libsuitesparse-dev liblapack-dev libblas-dev \
        nlohmann-json3-dev libomp-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

# Copy the whole repository (including the Tools/ submodules, which must already be
# checked out in the build context — `git submodule update --init --recursive`).
COPY . /src

# Configure + build the default camera-pairs target only (no optional apps, no tests).
RUN cmake -S . -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DXDIC_MODE=camerapairs \
        -DBUILD_TESTING=OFF \
        -DBUILD_PROXYNCORR=OFF \
        -DBUILD_SINGLEDIC=OFF \
    && cmake --build build -j"$(nproc)" --target cppxdic

# -----------------------------------------------------------------------------
# Runtime image: ship the binary + shared-library runtimes only.
# -----------------------------------------------------------------------------
FROM ubuntu:24.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        libopencv-core406t64 libopencv-imgproc406t64 libopencv-imgcodecs406t64 \
        libopencv-videoio406t64 libopencv-highgui406t64 \
        libcgal14t64 libmatio13 libhdf5-103-1t64 libfftw3-double3 \
        libamd3 libcolamd3 libcholmod5 libspqr4 libsuitesparseconfig7 \
        liblapack3 libblas3 libgomp1 \
    || apt-get install -y --no-install-recommends \
        libopencv-dev libcgal-dev libmatio-dev libhdf5-dev libfftw3-dev \
        libsuitesparse-dev liblapack3 libblas3 libgomp1 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=build /src/build/cppxdic /usr/local/bin/cppxdic
COPY --from=build /src/config /opt/cppxdic/config

WORKDIR /work
ENTRYPOINT ["/usr/local/bin/cppxdic"]
CMD ["--help"]

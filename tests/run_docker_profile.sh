#!/usr/bin/env bash
# Run the instrumented pipeline inside the Docker image for ref trial 5 (D->E->F),
# with XPROF profiling + an independent cgroup peak-memory readout.
#
# Mounts the whole MultiDIC tree at the SAME path so the compiled-in default
# base_path (/Users/jaoga/devlab/MultiDIC) resolves and data/dic paths derive
# correctly. Profiling CSVs land in /tmp/xprof_docker on the host.
set -euo pipefail
ROOT=/Users/jaoga/devlab/MultiDIC
CFG=$ROOT/CPPxDIC/config/default.cfg
rm -rf /tmp/xprof_docker && mkdir -p /tmp/xprof_docker

docker run --rm \
  -v "$ROOT":"$ROOT" \
  -v /tmp/xprof_docker:/profout \
  -e XDIC_PROFILE=1 -e XDIC_PROFILE_OUT=/profout -e OMP_NUM_THREADS=4 \
  -w /profout \
  --entrypoint /bin/bash \
  cppxdic-prof -c "
    /opt/cppxdic/bin/cppxdic -C '$CFG' -d '$CFG' -n '$CFG' -v '$CFG' \
      --subject S09 --reftrial 5 --trial 5
    rc=\$?
    echo '--- cgroup peak memory (bytes) ---'
    cat /sys/fs/cgroup/memory.peak 2>/dev/null || cat /sys/fs/cgroup/memory.max_usage_in_bytes 2>/dev/null || echo 'n/a'
    exit \$rc
  "

/**
 * @file multi_mode.cpp
 * @brief Placeholder for the N-camera xDIC mode (NOT IMPLEMENTED).
 *
 * This translation unit is intentionally empty of working code. It exists to mark where the
 * multi-camera reconstruction will live (see src/xdic/multi/README.md for the architecture).
 *
 * The hard `#error` below only trips when the build is configured with `-DXDIC_MODE=multi`,
 * which is the only configuration that adds this file to the build. In the default
 * `camerapairs` build this file is never compiled, so the default build is unaffected.
 */

#if defined(XDIC_MODE_MULTI)
#error "xdic multi-camera mode is not implemented"
#endif

// TODO: implement the N-camera pipeline described in src/xdic/multi/README.md:
//   camera-graph construction -> per-edge 2D DIC -> per-edge triangulation ->
//   multi-view fusion -> deformation/strain.

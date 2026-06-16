# xDIC multi-camera mode (planned)

The **multi** mode generalises the camera-pair reconstruction to **N calibrated cameras**
observing the specimen simultaneously. Where `camerapairs` handles a fixed set of stereo
pairs (default `pair1=(1,2)`, `pair2=(4,3)`), `multi` treats the cameras as a single rig and
fuses all overlapping views into one consistent 3D surface.

> **Status:** design note only. `multi_mode.cpp` is a placeholder that trips
> `#error "xdic multi-camera mode is not implemented"` when the build is configured with
> `-DXDIC_MODE=multi`. It is never added to the default (`camerapairs`) build.

## Intended architecture

1. **Camera graph construction.** Build a graph whose nodes are cameras and whose edges are
   pairs with sufficient view overlap (derived from calibration extrinsics). The existing
   camera-pair stereo step becomes the per-edge primitive.

2. **Per-edge 2D DIC.** For every overlapping pair, run the existing CppNCorr 2D DIC engine
   (reuse the camerapairs back-end / `apps/proxyncorr`) to get correspondences.

3. **Per-edge 3D reconstruction.** Triangulate each pair into a partial 3D point set, reusing
   the camerapairs stereo reconstruction (`stepE`).

4. **Multi-view fusion.** Merge per-edge partial surfaces into one mesh:
   - register partial surfaces in a common world frame using calibration extrinsics;
   - resolve overlapping regions (weighted averaging by correlation confidence / view angle);
   - stitch into a single watertight surface (generalise `src/surface_stitching.cpp`, which
     currently stitches two pairs).

5. **Deformation / strain.** Run the existing deformation analysis (`stepF`) on the fused
   surface, unchanged.

## Reuse map

| Stage              | Reused from camerapairs                          |
|--------------------|--------------------------------------------------|
| 2D DIC             | CppNCorr engine / `apps/proxyncorr`              |
| Stereo triangulation | `src/dic_analysis.cpp` (stepE reconstruction) |
| Surface stitching  | `src/surface_stitching.cpp` (to be generalised) |
| Deformation/strain | `src/strain_computation.cpp`, `src/step_d_workflow.cpp` |

## What "fully implemented" means

- Accept an arbitrary list of cameras + their pairwise overlaps from config.
- Produce a single fused 3D surface and deformation fields equivalent to running, and then
  merging, the per-pair camerapairs pipeline — with no double-counting in overlap regions.

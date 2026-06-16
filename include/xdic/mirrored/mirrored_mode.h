/**
 * @file mirrored_mode.h
 * @brief Skeleton interface for the mirrored-camera xDIC mode (STUB).
 *
 * The mirrored mode reconstructs a 3D surface from a single physical camera that observes
 * the specimen together with one or more mirrors, yielding multiple virtual viewpoints in a
 * single frame. In the reference MATLAB project this corresponds to the MNG-prefixed steps:
 *   - Tools/MultiDIC/main_script/stepD_2DDIC_MNG.m
 *   - Tools/MultiDIC/main_script/stepE_3DReconstruction_MNG.m
 *
 * Status: STUB. Compiled only when configured with `-DXDIC_MODE=mirrored`.
 */

#pragma once

class Config;

namespace xdic {
namespace mirrored {

/**
 * @brief Run the mirrored-camera reconstruction pipeline (STUB).
 *
 * Logs that the mode is not yet implemented and returns true (clean no-op) so the
 * executable exits gracefully.
 *
 * @param config Fully-resolved configuration (paths, frames, DIC params).
 * @return true on clean exit.
 *
 * @par TODO - what "fully implemented" means
 *  - Split each mirrored frame into virtual views using the mirror geometry.
 *  - Run 2D DIC per virtual view (mirror of stepD_2DDIC_MNG.m).
 *  - Reconstruct the 3D surface from the virtual stereo views (stepE_3DReconstruction_MNG.m).
 *  - Run deformation/strain analysis and export, reusing the camerapairs back-end.
 */
bool run(const Config& config);

} // namespace mirrored
} // namespace xdic

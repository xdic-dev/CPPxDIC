/**
 * @file mirrored_mode.cpp
 * @brief Skeleton implementation for the mirrored-camera xDIC mode (STUB).
 *
 * Compiled into `cppxdic` only when configured with `-DXDIC_MODE=mirrored`.
 * See include/xdic/mirrored/mirrored_mode.h for the documented interface and the TODO
 * describing what "fully implemented" means.
 */

#include "xdic/mirrored/mirrored_mode.h"
#include "config.h"

#include <iostream>

namespace xdic {
namespace mirrored {

bool run(const Config& config) {
    (void)config; // Unused until the pipeline is implemented.
    std::cout << "xdic mirrored-camera mode not yet implemented" << std::endl;
    // TODO: implement mirror-view splitting, per-view 2D DIC (stepD_2DDIC_MNG.m),
    //       3D reconstruction (stepE_3DReconstruction_MNG.m), and deformation analysis.
    return true; // Clean no-op exit.
}

} // namespace mirrored
} // namespace xdic

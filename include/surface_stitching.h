/**
 * Surface Stitching for Multi-Pair 3D Reconstruction
 */

#ifndef SURFACE_STITCHING_H
#define SURFACE_STITCHING_H

#include "dic_structures.h"
#include <vector>

namespace cppxdic {

/**
 * Simple append-based stitching for multiple stereo pairs
 * 
 * Concatenates all pairs together with proper index offsetting.
 * Does not perform geometric overlap removal (that's for future enhancement).
 * 
 * @param all_pairs Vector of DIC3Dcombined structures, one per stereo pair
 * @return Stitched DIC3Dcombined structure with all pairs combined
 */
DIC3Dcombined stitchPairsSimple(const std::vector<DIC3Dcombined>& all_pairs);

// Future: Complex geometric stitching with overlap removal
// DIC3Dcombined stitchPairsComplex(const std::vector<DIC3Dcombined>& all_pairs,
//                                   const std::vector<int>& pairIndList);

} // namespace cppxdic

#endif // SURFACE_STITCHING_H

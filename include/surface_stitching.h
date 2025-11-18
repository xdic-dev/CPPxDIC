/**
 * Surface Stitching for Multi-Pair 3D Reconstruction
 */

#ifndef SURFACE_STITCHING_H
#define SURFACE_STITCHING_H

#include "dic_structures.h"
#include <vector>
#include <Eigen/Dense>

namespace cppxdic {

/**
 * Simple append-based stitching for multiple stereo pairs
 * 
 * Concatenates all pairs together with proper index offsetting.
 * Does not perform geometric overlap removal.
 * 
 * @param all_pairs Vector of DIC3Dcombined structures, one per stereo pair
 * @return Stitched DIC3Dcombined structure with all pairs combined
 */
DIC3Dcombined stitchPairsSimple(const std::vector<DIC3Dcombined>& all_pairs);

/**
 * Geometric stitching with overlap removal and boundary zipping
 * Matches MATLAB's DIC3DsurfaceStitch.m with geometric stitching enabled
 * 
 * @param all_pairs Vector of DIC3Dcombined structures, one per stereo pair
 * @param pair_order Order in which to stitch pairs (1-indexed, matching MATLAB pairIndList)
 * @return Stitched DIC3Dcombined structure with overlaps removed
 */
DIC3Dcombined stitchPairsGeometric(const std::vector<DIC3Dcombined>& all_pairs,
                                    const std::vector<int>& pair_order);

// ============================================================================
// Helper functions for geometric stitching
// ============================================================================

/**
 * Compute boundary edges of a triangular mesh
 * Matches MATLAB: patchBoundary(F, V)
 * 
 * @param faces Triangle faces (flat array: [v0,v1,v2, v0,v1,v2, ...])
 * @param vertices Vertex positions
 * @return Boundary edges as pairs [v0,v1, v0,v1, ...]
 */
std::vector<int> computeMeshBoundary(const std::vector<int>& faces,
                                      const std::vector<Eigen::Vector3d>& vertices);

/**
 * Compute edge lengths for all triangles
 * Matches MATLAB: patchEdgeLengths(F, V)
 */
std::vector<double> computeEdgeLengths(const std::vector<int>& faces,
                                        const std::vector<Eigen::Vector3d>& vertices);

/**
 * Remove overlapping regions from two surfaces
 * Matches MATLAB: removeOverlapSurface_temp_2018_10_30(F1, F2, V1, V2, [], [], minGap, pairforced)
 * 
 * @param faces1 First surface faces
 * @param faces2 Second surface faces
 * @param vertices1 First surface vertices
 * @param vertices2 Second surface vertices
 * @param min_gap Minimum gap threshold for overlap detection
 * @return Pair of boolean masks (keep face 1, keep face 2)
 */
std::pair<std::vector<bool>, std::vector<bool>> removeOverlapSurfaces(
    const std::vector<int>& faces1,
    const std::vector<int>& faces2,
    const std::vector<Eigen::Vector3d>& vertices1,
    const std::vector<Eigen::Vector3d>& vertices2,
    double min_gap);

} // namespace cppxdic

#endif // SURFACE_STITCHING_H

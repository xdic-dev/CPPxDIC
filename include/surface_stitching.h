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
 * @param all_pairs Vector of DIC3DpairResults structures, one per stereo pair
 * @return Stitched DIC3Dcombined structure with all pairs combined
 */
DIC3Dcombined stitchPairsSimple(const std::vector<DIC3DpairResults>& all_pairs);

/**
 * Geometric stitching with overlap removal and boundary zipping
 * Matches MATLAB's DIC3DsurfaceStitch.m with geometric stitching enabled
 * 
 * @param all_pairs Vector of DIC3DpairResults structures, one per stereo pair
 * @param pair_order Order in which to stitch pairs (1-indexed, matching MATLAB pairIndList)
 * @return Stitched DIC3Dcombined structure with overlaps removed
 */
DIC3Dcombined stitchPairsGeometric(const std::vector<DIC3DpairResults>& all_pairs,
                                    const std::vector<int>& pair_order,
                                    bool pair_forced = false);

/**
 * Enable/disable the AABB-hierarchy prefilter used by the overlap-removal ray casts.
 *
 * Off by default. The prefilter is exact (a ray that hits a triangle also hits its
 * padded bounding box, so nothing is pruned that could have been hit) and only changes
 * how many triangles are tested per ray: O(log F) box tests instead of O(F) triangle
 * tests. On rigs with many pairs the brute-force path is the dominant cost of Step E.
 *
 * Process-wide, set once from Config::step_e_ray_bvh before stitching.
 */
void setStitchRayAccel(bool enabled);

/** Current state of the ray-cast prefilter (see setStitchRayAccel). */
bool stitchRayAccel();

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
    double min_gap,
    bool pair_forced = false);

/**
 * Group boundary edges into connected components.
 * Matches MATLAB: tesgroup(E)
 * 
 * @param boundary_edges Boundary edges as pairs [v0,v1, v0,v1, ...]
 * @return Vector of groups, each group is a list of edge indices
 */
std::vector<std::vector<int>> groupBoundaryEdges(const std::vector<int>& boundary_edges);

/**
 * Convert an unordered list of edges into an ordered curve of vertex indices.
 * Matches MATLAB: edgeListToCurve(E)
 * 
 * @param edges Edge pairs [v0,v1, v0,v1, ...]
 * @return Ordered list of vertex indices forming the curve
 */
std::vector<int> edgeListToCurve(const std::vector<int>& edges);

/**
 * Zip two boundary curves together by creating a triangle strip.
 * Simplified equivalent of MATLAB's delaunayZip.
 * Uses greedy advancing-front: picks the shorter diagonal at each step.
 * 
 * @param curve1 Ordered vertex indices of boundary curve 1
 * @param curve2 Ordered vertex indices of boundary curve 2 (already offset for combined mesh)
 * @param V_combined Combined vertex positions [V1; V2]
 * @return New faces (flat array) connecting the two curves
 */
std::vector<int> zipBoundaryCurves(const std::vector<int>& curve1,
                                    const std::vector<int>& curve2,
                                    const std::vector<Eigen::Vector3d>& V_combined);

/**
 * Remove faces where all 3 edges are on the boundary (degenerate boundary triangles).
 * Matches MATLAB DIC3DsurfaceStitch lines 127-160.
 * 
 * @param faces Triangle faces (flat: [v0,v1,v2, ...])
 * @param boundary_edges Boundary edge pairs [v0,v1, ...]
 * @return Boolean mask per face: true = keep, false = remove
 */
std::vector<bool> findAllBoundaryFaces(const std::vector<int>& faces,
                                        const std::vector<int>& boundary_edges);

} // namespace cppxdic

#endif // SURFACE_STITCHING_H

/**
 * Delaunay Triangulation for CPPXDIC
 * CGAL-based implementation matching MATLAB's delaunayTriangulation
 */

#ifndef DELAUNAY_TRIANGULATION_H
#define DELAUNAY_TRIANGULATION_H

#include <opencv2/opencv.hpp>
#include <vector>

namespace cppxdic {

/**
 * DelaunayTriangulation class
 * Provides Delaunay triangulation utilities using CGAL
 */
class DelaunayTriangulation {
public:
    /**
     * Compute Delaunay triangulation of 2D points
     * Matches MATLAB: DT = delaunayTriangulation(P)
     * 
     * @param points Vector of 2D points
     * @return Faces as flat vector [v1, v2, v3, v1, v2, v3, ...]
     */
    static std::vector<int> compute(const std::vector<cv::Point2f>& points);
    
    /**
     * Filter irregular triangles by edge length
     * Matches MATLAB: F(EdgeLengthsMax > threshold, :) = []
     * 
     * @param faces Input faces (flat vector)
     * @param vertices Vertex positions
     * @param max_edge_threshold Maximum edge length threshold
     * @return Filtered faces
     */
    static std::vector<int> filterByEdgeLength(const std::vector<int>& faces,
                                                const std::vector<cv::Point2f>& vertices,
                                                double max_edge_threshold);
    
    /**
     * Compute edge lengths for all triangles
     * Matches MATLAB: patchEdgeLengths(F, V)
     * 
     * @param faces Faces (flat vector)
     * @param vertices Vertex positions
     * @return Edge lengths (3 per triangle: [e1, e2, e3, e1, e2, e3, ...])
     */
    static std::vector<double> computeEdgeLengths(const std::vector<int>& faces,
                                                   const std::vector<cv::Point2f>& vertices);
    
    /**
     * Flip triangle orientation
     * Matches MATLAB: F = F(:, [1 3 2])
     * 
     * @param faces Input faces
     * @return Faces with flipped orientation
     */
    static std::vector<int> flipOrientation(const std::vector<int>& faces);
    
    /**
     * Compute face centroids
     * 
     * @param faces Faces (flat vector)
     * @param vertices Vertex positions
     * @return Centroid for each triangle
     */
    static std::vector<cv::Point2f> computeCentroids(const std::vector<int>& faces,
                                                      const std::vector<cv::Point2f>& vertices);
    
    /**
     * Compute face areas
     * 
     * @param faces Faces (flat vector)
     * @param vertices Vertex positions
     * @return Area for each triangle
     */
    static std::vector<double> computeAreas(const std::vector<int>& faces,
                                            const std::vector<cv::Point2f>& vertices);
};

} // namespace cppxdic

#endif // DELAUNAY_TRIANGULATION_H

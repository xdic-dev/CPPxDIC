/**
 * Face Isotropy Index for CPPXDIC
 * Port of MATLAB faceIsotropyIndex.m
 * 
 * Computes triangle regularity/quality metric
 * 
 * Reference:
 * Cappozzo, A., Cappello, A., Della Croce, U., & Pensalfini, F. (1997).
 * Surface-marker cluster design criteria for 3-D bone movement reconstruction.
 * IEEE Transactions on Biomedical Engineering, 44(12), 1165-1174.
 */

#ifndef FACE_ISOTROPY_H
#define FACE_ISOTROPY_H

#include <vector>
#include <Eigen/Dense>

namespace cppxdic {

/**
 * Compute isotropy index for triangular faces
 * 
 * The isotropy index measures the "regularity" of a triangle:
 * - Value of 1.0: Perfectly equilateral triangle (most regular)
 * - Value of 0.0: Degenerate triangle (vertices aligned on a line)
 * - NaN: Triangle has NaN vertices
 * 
 * Algorithm:
 * 1. Compute cluster position model: X = [x1-xa, x2-xa, x3-xa] where xa is centroid
 * 2. Compute covariance-like matrix: K = X*X'/3
 * 3. Compute eigenvalues of K
 * 4. Isotropy index = 2*λ2 / (λ2 + λ3) where λ2 < λ3 are sorted eigenvalues
 * 
 * @param faces Triangle connectivity (3*nFaces flattened: [v0,v1,v2, v0,v1,v2, ...])
 * @param vertices 3D vertex positions (nVertices x 3)
 * @return Isotropy index for each face (nFaces values)
 */
std::vector<double> computeFaceIsotropyIndex(
    const std::vector<int>& faces,
    const std::vector<Eigen::Vector3d>& vertices
);

/**
 * Compute isotropy index for a single triangle
 * 
 * @param v0 First vertex position
 * @param v1 Second vertex position
 * @param v2 Third vertex position
 * @return Isotropy index (0 to 1, or NaN if degenerate)
 */
double computeSingleFaceIsotropyIndex(
    const Eigen::Vector3d& v0,
    const Eigen::Vector3d& v1,
    const Eigen::Vector3d& v2
);

} // namespace cppxdic

#endif // FACE_ISOTROPY_H

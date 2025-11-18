/**
 * Face Isotropy Index Implementation
 * Port of MATLAB faceIsotropyIndex.m
 */

#include "face_isotropy.h"
#include <cmath>
#include <limits>

namespace cppxdic {

double computeSingleFaceIsotropyIndex(
    const Eigen::Vector3d& v0,
    const Eigen::Vector3d& v1,
    const Eigen::Vector3d& v2
) {
    // Check for NaN in any vertex
    if (!v0.allFinite() || !v1.allFinite() || !v2.allFinite()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    
    // Compute centroid
    Eigen::Vector3d centroid = (v0 + v1 + v2) / 3.0;
    
    // Cluster position model: X = [x1-xa, x2-xa, x3-xa]
    Eigen::Matrix3d X;
    X.col(0) = v0 - centroid;
    X.col(1) = v1 - centroid;
    X.col(2) = v2 - centroid;
    
    // Compute K = X * X' / 3
    Eigen::Matrix3d K = (X * X.transpose()) / 3.0;
    
    // Compute eigenvalues
    Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> eigensolver(K);
    if (eigensolver.info() != Eigen::Success) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    
    Eigen::Vector3d eigenvalues = eigensolver.eigenvalues();
    
    // Sort eigenvalues (they should already be sorted by Eigen, but let's be sure)
    std::sort(eigenvalues.data(), eigenvalues.data() + 3);
    
    // Isotropy index = 2*λ2 / (λ2 + λ3)
    // where λ1 ≤ λ2 ≤ λ3 are sorted eigenvalues
    double lambda2 = eigenvalues(1);
    double lambda3 = eigenvalues(2);
    
    // Avoid division by zero
    double denominator = lambda2 + lambda3;
    if (std::abs(denominator) < 1e-12) {
        return 0.0;  // Degenerate case
    }
    
    double isotropy = 2.0 * lambda2 / denominator;
    
    // Clamp to [0, 1] due to numerical precision
    isotropy = std::max(0.0, std::min(1.0, isotropy));
    
    return isotropy;
}

std::vector<double> computeFaceIsotropyIndex(
    const std::vector<int>& faces,
    const std::vector<Eigen::Vector3d>& vertices
) {
    size_t nFaces = faces.size() / 3;
    std::vector<double> isotropy(nFaces);
    
    for (size_t iface = 0; iface < nFaces; ++iface) {
        int i0 = faces[iface * 3 + 0];
        int i1 = faces[iface * 3 + 1];
        int i2 = faces[iface * 3 + 2];
        
        // Bounds check
        if (i0 < 0 || i0 >= static_cast<int>(vertices.size()) ||
            i1 < 0 || i1 >= static_cast<int>(vertices.size()) ||
            i2 < 0 || i2 >= static_cast<int>(vertices.size())) {
            isotropy[iface] = std::numeric_limits<double>::quiet_NaN();
            continue;
        }
        
        const Eigen::Vector3d& v0 = vertices[i0];
        const Eigen::Vector3d& v1 = vertices[i1];
        const Eigen::Vector3d& v2 = vertices[i2];
        
        isotropy[iface] = computeSingleFaceIsotropyIndex(v0, v1, v2);
    }
    
    return isotropy;
}

} // namespace cppxdic

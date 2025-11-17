/**
 * Strain Computation Implementation
 * Direct port of MATLAB triSurfaceDeformation_rewrited.m
 */

#include "strain_computation.h"
#include <iostream>
#include <cmath>
#include <limits>

namespace cppxdic {

DeformationResult computeSingleFrameDeformation(
    const std::vector<int>& faces,
    const std::vector<Eigen::Vector3d>& vertices_ref,
    const std::vector<Eigen::Vector3d>& vertices_cur) {
    
    DeformationResult result;
    
    if (faces.size() % 3 != 0) {
        std::cerr << "Error: Faces must be Nx3 (got " << faces.size() << " elements)" << std::endl;
        return result;
    }
    
    size_t n_faces = faces.size() / 3;
    
    // Pre-allocate all vectors
    result.D1.resize(n_faces);
    result.D2.resize(n_faces);
    result.D3.resize(n_faces);
    result.d1.resize(n_faces);
    result.d2.resize(n_faces);
    result.d3.resize(n_faces);
    result.Drec1.resize(n_faces);
    result.Drec2.resize(n_faces);
    result.Dnorm.resize(n_faces);
    result.Fmat.resize(n_faces);
    result.Cmat.resize(n_faces);
    result.Lamda1.resize(n_faces);
    result.Lamda2.resize(n_faces);
    result.J.resize(n_faces);
    result.Emat.resize(n_faces);
    result.emat.resize(n_faces);
    result.Emgn.resize(n_faces);
    result.emgn.resize(n_faces);
    result.Epc1.resize(n_faces);
    result.Epc2.resize(n_faces);
    result.Epc1vec.resize(n_faces);
    result.Epc2vec.resize(n_faces);
    result.Epc1vecCur.resize(n_faces);
    result.Epc2vecCur.resize(n_faces);
    result.epc1.resize(n_faces);
    result.epc2.resize(n_faces);
    result.epc1vec.resize(n_faces);
    result.epc2vec.resize(n_faces);
    result.EShearMax.resize(n_faces);
    result.eShearMax.resize(n_faces);
    result.EShearMaxVec1.resize(n_faces);
    result.EShearMaxVec2.resize(n_faces);
    result.EShearMaxVecCur1.resize(n_faces);
    result.EShearMaxVecCur2.resize(n_faces);
    result.eShearMaxVec1.resize(n_faces);
    result.eShearMaxVec2.resize(n_faces);
    result.Eeq.resize(n_faces);
    result.eeq.resize(n_faces);
    result.Area.resize(n_faces);
    
    // Process each triangle
    for (size_t itri = 0; itri < n_faces; ++itri) {
        size_t idx = itri * 3;
        int v0 = faces[idx];
        int v1 = faces[idx + 1];
        int v2 = faces[idx + 2];
        
        // Check bounds
        if (v0 >= vertices_ref.size() || v1 >= vertices_ref.size() || v2 >= vertices_ref.size() ||
            v0 >= vertices_cur.size() || v1 >= vertices_cur.size() || v2 >= vertices_cur.size()) {
            std::cerr << "Warning: Invalid vertex index in triangle " << itri << std::endl;
            // Fill with NaN
            result.Fmat[itri] = Eigen::Matrix3d::Constant(std::nan(""));
            continue;
        }
        
        // Reference director vectors (MATLAB lines 145-158)
        result.D1[itri] = vertices_ref[v1] - vertices_ref[v0];
        result.D2[itri] = vertices_ref[v2] - vertices_ref[v0];
        Eigen::Vector3d cross_D = result.D1[itri].cross(result.D2[itri]);
        double norm_D = cross_D.norm();
        if (norm_D < 1e-12) {
            // Degenerate triangle
            result.D3[itri] = Eigen::Vector3d(0, 0, 1);
        } else {
            result.D3[itri] = cross_D / norm_D;
        }
        
        // Current director vectors (MATLAB lines 161-164)
        result.d1[itri] = vertices_cur[v1] - vertices_cur[v0];
        result.d2[itri] = vertices_cur[v2] - vertices_cur[v0];
        Eigen::Vector3d cross_d = result.d1[itri].cross(result.d2[itri]);
        double norm_d = cross_d.norm();
        if (norm_d < 1e-12) {
            result.d3[itri] = Eigen::Vector3d(0, 0, 1);
        } else {
            result.d3[itri] = cross_d / norm_d;
        }
        
        // Reciprocal vectors (MATLAB lines 166-169)
        result.Dnorm[itri] = cross_D.dot(result.D3[itri]);
        if (std::abs(result.Dnorm[itri]) < 1e-12) {
            // Degenerate triangle
            result.Fmat[itri] = Eigen::Matrix3d::Constant(std::nan(""));
            continue;
        }
        
        result.Drec1[itri] = result.D2[itri].cross(result.D3[itri]) / result.Dnorm[itri];
        result.Drec2[itri] = result.D3[itri].cross(result.D1[itri]) / result.Dnorm[itri];
        
        // Area (MATLAB line 172)
        result.Area[itri] = 0.5 * result.Dnorm[itri];
        
        // Deformation gradient tensor F (MATLAB lines 174-179)
        result.Fmat[itri] = Eigen::Matrix3d::Zero();
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                result.Fmat[itri](i, j) = 
                    result.d1[itri](i) * result.Drec1[itri](j) +
                    result.d2[itri](i) * result.Drec2[itri](j) +
                    result.d3[itri](i) * result.D3[itri](j);
            }
        }
        
        // Check if F has NaNs (MATLAB line 181)
        if (result.Fmat[itri].array().isNaN().any()) {
            // Fill all measures with NaN
            result.Cmat[itri] = Eigen::Matrix3d::Constant(std::nan(""));
            result.Lamda1[itri] = std::nan("");
            result.Lamda2[itri] = std::nan("");
            result.J[itri] = std::nan("");
            result.Emat[itri] = Eigen::Matrix3d::Constant(std::nan(""));
            result.emat[itri] = Eigen::Matrix3d::Constant(std::nan(""));
            result.Emgn[itri] = std::nan("");
            result.emgn[itri] = std::nan("");
            result.Epc1[itri] = std::nan("");
            result.Epc2[itri] = std::nan("");
            result.Epc1vec[itri] = Eigen::Vector3d::Constant(std::nan(""));
            result.Epc2vec[itri] = Eigen::Vector3d::Constant(std::nan(""));
            result.Epc1vecCur[itri] = Eigen::Vector3d::Constant(std::nan(""));
            result.Epc2vecCur[itri] = Eigen::Vector3d::Constant(std::nan(""));
            result.epc1[itri] = std::nan("");
            result.epc2[itri] = std::nan("");
            result.epc1vec[itri] = Eigen::Vector3d::Constant(std::nan(""));
            result.epc2vec[itri] = Eigen::Vector3d::Constant(std::nan(""));
            result.EShearMax[itri] = std::nan("");
            result.eShearMax[itri] = std::nan("");
            result.EShearMaxVec1[itri] = Eigen::Vector3d::Constant(std::nan(""));
            result.EShearMaxVec2[itri] = Eigen::Vector3d::Constant(std::nan(""));
            result.EShearMaxVecCur1[itri] = Eigen::Vector3d::Constant(std::nan(""));
            result.EShearMaxVecCur2[itri] = Eigen::Vector3d::Constant(std::nan(""));
            result.eShearMaxVec1[itri] = Eigen::Vector3d::Constant(std::nan(""));
            result.eShearMaxVec2[itri] = Eigen::Vector3d::Constant(std::nan(""));
            result.Eeq[itri] = std::nan("");
            result.eeq[itri] = std::nan("");
            continue;
        }
        
        // Cauchy-Green deformation tensor C = F^T * F (MATLAB line 184)
        result.Cmat[itri] = result.Fmat[itri].transpose() * result.Fmat[itri];
        
        // Principal stretches (MATLAB lines 185-191)
        Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> eigC(result.Cmat[itri]);
        Eigen::Vector3d eigvals_C = eigC.eigenvalues();
        Eigen::Vector3d lamdas = eigvals_C.array().sqrt();
        
        // Find and remove the stretch closest to 1
        int min_idx = 0;
        double min_diff = std::abs(lamdas(0) - 1.0);
        for (int i = 1; i < 3; ++i) {
            double diff = std::abs(lamdas(i) - 1.0);
            if (diff < min_diff) {
                min_diff = diff;
                min_idx = i;
            }
        }
        
        std::vector<double> lamda_vec;
        for (int i = 0; i < 3; ++i) {
            if (i != min_idx) lamda_vec.push_back(lamdas(i));
        }
        result.Lamda1[itri] = lamda_vec[0];
        result.Lamda2[itri] = lamda_vec[1];
        
        // Dilatation J = det(F) (MATLAB line 194)
        result.J[itri] = result.Fmat[itri].determinant();
        
        // Lagrangian finite strain tensor E = 0.5*(F^T*F - I) (MATLAB line 197)
        result.Emat[itri] = 0.5 * (result.Fmat[itri].transpose() * result.Fmat[itri] - Eigen::Matrix3d::Identity());
        
        // Eulerian-Almansi finite strain tensor e = 0.5*(I - inv(F*F^T)) (MATLAB line 200)
        Eigen::Matrix3d FFT = result.Fmat[itri] * result.Fmat[itri].transpose();
        result.emat[itri] = 0.5 * (Eigen::Matrix3d::Identity() - FFT.inverse());
        
        // Strain magnitudes (Frobenius norm) (MATLAB lines 202-206)
        result.Emgn[itri] = result.Emat[itri].norm();
        result.emgn[itri] = result.emat[itri].norm();
        
        // Lagrangian principal strains (MATLAB lines 208-222)
        Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> eigE(result.Emat[itri]);
        Eigen::Vector3d eigvals_E = eigE.eigenvalues();
        Eigen::Matrix3d eigvecs_E = eigE.eigenvectors();
        
        // Find eigenvector closest to D3 (normal direction)
        int D3_idx = 0;
        double max_dot = std::abs(eigvecs_E.col(0).dot(result.D3[itri]));
        for (int i = 1; i < 3; ++i) {
            double dot_val = std::abs(eigvecs_E.col(i).dot(result.D3[itri]));
            if (dot_val > max_dot) {
                max_dot = dot_val;
                D3_idx = i;
            }
        }
        
        // Get planar eigenvectors (not D3)
        std::vector<int> plan_idx;
        for (int i = 0; i < 3; ++i) {
            if (i != D3_idx) plan_idx.push_back(i);
        }
        
        // Find min and max planar strains
        double E_plan1 = eigvals_E(plan_idx[0]);
        double E_plan2 = eigvals_E(plan_idx[1]);
        
        if (E_plan1 < E_plan2) {
            result.Epc1[itri] = E_plan1;
            result.Epc2[itri] = E_plan2;
            result.Epc1vec[itri] = eigvecs_E.col(plan_idx[0]);
            result.Epc2vec[itri] = eigvecs_E.col(plan_idx[1]);
        } else {
            result.Epc1[itri] = E_plan2;
            result.Epc2[itri] = E_plan1;
            result.Epc1vec[itri] = eigvecs_E.col(plan_idx[1]);
            result.Epc2vec[itri] = eigvecs_E.col(plan_idx[0]);
        }
        
        // Transform principal strain directions to current configuration
        result.Epc1vecCur[itri] = result.Fmat[itri] * result.Epc1vec[itri];
        result.Epc1vecCur[itri].normalize();
        result.Epc2vecCur[itri] = result.Fmat[itri] * result.Epc2vec[itri];
        result.Epc2vecCur[itri].normalize();
        
        // Eulerian principal strains (MATLAB lines 224-233)
        Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> eige(result.emat[itri]);
        Eigen::Vector3d eigvals_e = eige.eigenvalues();
        Eigen::Matrix3d eigvecs_e = eige.eigenvectors();
        
        // Find eigenvector closest to d3
        int d3_idx = 0;
        max_dot = std::abs(eigvecs_e.col(0).dot(result.d3[itri]));
        for (int i = 1; i < 3; ++i) {
            double dot_val = std::abs(eigvecs_e.col(i).dot(result.d3[itri]));
            if (dot_val > max_dot) {
                max_dot = dot_val;
                d3_idx = i;
            }
        }
        
        plan_idx.clear();
        for (int i = 0; i < 3; ++i) {
            if (i != d3_idx) plan_idx.push_back(i);
        }
        
        double e_plan1 = eigvals_e(plan_idx[0]);
        double e_plan2 = eigvals_e(plan_idx[1]);
        
        if (e_plan1 < e_plan2) {
            result.epc1[itri] = e_plan1;
            result.epc2[itri] = e_plan2;
            result.epc1vec[itri] = eigvecs_e.col(plan_idx[0]);
            result.epc2vec[itri] = eigvecs_e.col(plan_idx[1]);
        } else {
            result.epc1[itri] = e_plan2;
            result.epc2[itri] = e_plan1;
            result.epc1vec[itri] = eigvecs_e.col(plan_idx[1]);
            result.epc2vec[itri] = eigvecs_e.col(plan_idx[0]);
        }
        
        // Max shear strain (MATLAB lines 235-245)
        const double inv_sqrt2 = 1.0 / std::sqrt(2.0);
        
        // Lagrangian
        result.EShearMax[itri] = 0.5 * (result.Epc2[itri] - result.Epc1[itri]);
        result.EShearMaxVec1[itri] = inv_sqrt2 * (result.Epc1vec[itri] + result.Epc2vec[itri]);
        result.EShearMaxVec2[itri] = inv_sqrt2 * (result.Epc2vec[itri] - result.Epc1vec[itri]);
        result.EShearMaxVecCur1[itri] = inv_sqrt2 * (result.Epc1vecCur[itri] + result.Epc2vecCur[itri]);
        result.EShearMaxVecCur2[itri] = inv_sqrt2 * (result.Epc2vecCur[itri] - result.Epc1vecCur[itri]);
        
        // Eulerian
        result.eShearMax[itri] = 0.5 * (result.epc2[itri] - result.epc1[itri]);
        result.eShearMaxVec1[itri] = inv_sqrt2 * (result.epc1vec[itri] + result.epc2vec[itri]);
        result.eShearMaxVec2[itri] = inv_sqrt2 * (result.epc2vec[itri] - result.epc1vec[itri]);
        
        // Equivalent strain (von Mises) (MATLAB lines 247-251)
        Eigen::Matrix3d Edev = result.Emat[itri] - (result.Emat[itri].trace() / 3.0) * Eigen::Matrix3d::Identity();
        result.Eeq[itri] = std::sqrt((2.0 / 3.0) * (Edev.array() * Edev.array()).sum());
        
        Eigen::Matrix3d edev = result.emat[itri] - (result.emat[itri].trace() / 3.0) * Eigen::Matrix3d::Identity();
        result.eeq[itri] = std::sqrt((2.0 / 3.0) * (edev.array() * edev.array()).sum());
    }
    
    return result;
}

FrameDeformationResult computeTriSurfaceDeformation(
    const std::vector<int>& faces,
    const std::vector<Eigen::Vector3d>& vertices_ref,
    const std::vector<std::vector<Eigen::Vector3d>>& vertices_def,
    bool cumulative) {
    
    FrameDeformationResult result;
    result.n_frames = vertices_def.size();
    result.n_faces = faces.size() / 3;
    result.frames.reserve(result.n_frames);
    
    std::cout << "Computing deformation for " << result.n_frames << " frames, " 
              << result.n_faces << " faces" << std::endl;
    
    // Process each frame
    for (size_t itime = 0; itime < result.n_frames; ++itime) {
        // Determine reference configuration
        const std::vector<Eigen::Vector3d>* ref_vertices;
        
        if (cumulative) {
            // Use initial frame as reference (MATLAB cum == 1)
            ref_vertices = &vertices_ref;
        } else {
            // Use previous frame as reference (MATLAB cum == 0)
            if (itime == 0) {
                ref_vertices = &vertices_ref;
            } else {
                ref_vertices = &vertices_def[itime - 1];
            }
        }
        
        // Compute deformation for this frame
        DeformationResult frame_result = computeSingleFrameDeformation(
            faces,
            *ref_vertices,
            vertices_def[itime]
        );
        
        result.frames.push_back(std::move(frame_result));
        
        if ((itime + 1) % 10 == 0 || itime == result.n_frames - 1) {
            std::cout << "  Processed frame " << (itime + 1) << "/" << result.n_frames << std::endl;
        }
    }
    
    return result;
}

} // namespace cppxdic

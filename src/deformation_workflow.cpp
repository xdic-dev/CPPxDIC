/**
 * Deformation Workflow (Step F) implementation for CPPXDIC
 * Refactored from dic_analysis.cpp dicDeformationAnalysis
 */

#include "deformation_workflow.h"
#include "mat_writer.h"
#include "temporal_filter.h"
#include "face_isotropy.h"
#include "visualization.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <cmath>

namespace cppxdic {

// ============================================================================
// Input validation
// ============================================================================

bool DeformationInputs::isValid() const {
    return trial_id > 0 &&
           !dic3d_combined_path.empty() &&
           std::filesystem::exists(dic3d_combined_path) &&
           !output_dir.empty();
}

// ============================================================================
// Constructor
// ============================================================================

DeformationWorkflow::DeformationWorkflow(const Config& config) 
    : config_(config) {
}

// ============================================================================
// Main Entry Points
// ============================================================================

bool DeformationWorkflow::execute(const std::vector<int>& trial_target) {
    std::cout << "Starting Deformation/Strain Analysis (Step F)..." << std::endl;
    std::cout << "NOTE: This requires DIC3Dcombined from Step E" << std::endl;
    
    try {
        for (int trial : trial_target) {
            DeformationInputs inputs = buildInputs(trial);
            
            if (!inputs.isValid()) {
                std::cerr << "ERROR: DIC3Dcombined file not found: " << inputs.dic3d_combined_path << std::endl;
                std::cerr << "You must run Step E (dic3DReconstruction) first!" << std::endl;
                return false;
            }
            
            auto outputs = execute(inputs);
            
            if (!outputs.isValid()) {
                std::cerr << "Deformation analysis failed for trial " << trial << std::endl;
                return false;
            }
            
            std::cout << "✓ Trial " << trial << " complete: " 
                      << outputs.num_frames << " frames processed" << std::endl;
        }
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "ERROR in Deformation/Strain Analysis: " << e.what() << std::endl;
        return false;
    }
}

DeformationOutputs DeformationWorkflow::execute(const DeformationInputs& inputs) {
    DeformationOutputs outputs;
    
    // Check for existing checkpoint
    std::ostringstream binout;
    binout << inputs.output_dir << "/DIC3DPPresults_" << config_.num_pair 
           << "Pairs_cum_" << config_.fileversion << ".bin";
    
    if (hasDeformationCheckpoint(inputs.output_dir, config_.num_pair)) {
        std::cout << "Checkpoint found: " << binout.str() << std::endl;
        outputs.ppresults_binary_path = binout.str();
        
        // Load to get statistics
        DIC3DPPresults ppresults = loadCheckpoint(binout.str());
        outputs.num_frames = ppresults.n_frames;
        outputs.num_points = ppresults.Points3D.empty() ? 0 : ppresults.Points3D[0].x.size();
        outputs.num_faces = ppresults.Faces.size() / 3;
        
        return outputs;
    }
    
    // Load DIC3Dcombined
    std::cout << "Loading DIC3Dcombined from: " << inputs.dic3d_combined_path << std::endl;
    DIC3Dcombined dic3d = loadDIC3DCombined(inputs.dic3d_combined_path);
    
    if (dic3d.Points3D.empty() || dic3d.Faces.empty()) {
        std::cerr << "DIC3Dcombined has no 3D data" << std::endl;
        return outputs;
    }
    
    std::cout << "  Loaded: " << dic3d.Points3D.size() << " frames, "
              << dic3d.Points3D[0].x.size() << " points, "
              << dic3d.Faces.size() / 3 << " faces" << std::endl;
    
    // Convert to Eigen format
    std::cout << "\nConverting data to Eigen format..." << std::endl;
    std::vector<Eigen::Vector3d> vertices_ref = convertToEigen(dic3d.Points3D[0]);
    
    std::vector<std::vector<Eigen::Vector3d>> vertices_all_frames;
    vertices_all_frames.reserve(dic3d.Points3D.size());
    for (const auto& frame_pts : dic3d.Points3D) {
        vertices_all_frames.push_back(convertToEigen(frame_pts));
    }
    std::cout << "  Converted " << vertices_all_frames.size() << " frames" << std::endl;
    
    // Apply temporal filtering
    if (inputs.apply_temporal_filtering && vertices_all_frames.size() > 3) {
        std::cout << "\nApplying temporal filtering..." << std::endl;
        vertices_all_frames = applyTemporalFiltering(
            vertices_all_frames, vertices_ref,
            inputs.filter_freq, inputs.acquisition_freq);
        std::cout << "  ✓ Temporal filtering applied (freqFilt=" << inputs.filter_freq 
                  << " Hz, freqAcq=" << inputs.acquisition_freq << " Hz)" << std::endl;
    }
    
    // Update Points3D with filtered data
    std::cout << "\nUpdating Points3D with filtered data..." << std::endl;
    updatePoints3D(vertices_all_frames, dic3d.Points3D);
    std::cout << "  ✓ Points3D updated" << std::endl;
    
    // Recompute displacement
    std::cout << "\nRecomputing displacement after filtering..." << std::endl;
    recomputeDisplacement(vertices_all_frames, vertices_ref, dic3d.Disp);
    std::cout << "  ✓ Displacement recomputed" << std::endl;
    
    // Recompute face centroids
    std::cout << "\nRecomputing face centroids..." << std::endl;
    dic3d.FaceCentroids = recomputeFaceCentroids(vertices_all_frames, dic3d.Faces);
    std::cout << "  ✓ Face centroids recomputed" << std::endl;
    
    // Recompute face correlation
    std::cout << "\nRecomputing face correlation..." << std::endl;
    dic3d.FaceCorrComb = recomputeFaceCorrelation(dic3d.corrComb, dic3d.Faces);
    std::cout << "  ✓ Face correlation recomputed" << std::endl;
    
    // Compute RBM transformations
    std::cout << "\nComputing rigid body motion (RBM) transformations..." << std::endl;
    std::vector<Utils::RigidTransform> rbm_transforms;
    std::vector<std::vector<Eigen::Vector3d>> vertices_arbm = 
        applyRBMRemoval(vertices_all_frames, vertices_ref, rbm_transforms);
    std::cout << "  ✓ RBM transformations computed" << std::endl;
    
    // Compute deformation (with RBM)
    std::cout << "\nComputing 3D surface deformation (with RBM)..." << std::endl;
    std::cout << "  Method: Triangular Cosserat Point Elements (TCPE)" << std::endl;
    FrameDeformationResult deform_result = computeSurfaceDeformation(
        dic3d.Faces, vertices_ref, vertices_all_frames, inputs.cumulative_deformation);
    std::cout << "  ✓ Deformation computation complete (with RBM)" << std::endl;
    
    // Compute deformation (after RBM removal)
    std::cout << "\nComputing 3D surface deformation (after RBM removal)..." << std::endl;
    FrameDeformationResult deform_result_arbm = computeSurfaceDeformation(
        dic3d.Faces, vertices_arbm[0], vertices_arbm, inputs.cumulative_deformation);
    std::cout << "  ✓ Deformation computation complete (ARBM)" << std::endl;
    
    // Compute face isotropy index
    std::cout << "\nComputing face isotropy index..." << std::endl;
    std::vector<std::vector<double>> face_iso_ind;
    face_iso_ind.reserve(vertices_all_frames.size());
    for (const auto& verts : vertices_all_frames) {
        face_iso_ind.push_back(computeFaceIsotropyIndex(dic3d.Faces, verts));
    }
    std::cout << "  ✓ Face isotropy index computed" << std::endl;
    
    // Build DIC3DPPresults
    std::cout << "\nBuilding DIC3DPPresults structure..." << std::endl;
    DIC3DPPresults ppresults = buildPPResults(
        dic3d, deform_result, deform_result_arbm,
        rbm_transforms, vertices_arbm, face_iso_ind);
    std::cout << "  ✓ DIC3DPPresults built" << std::endl;
    
    // Save binary output
    if (!saveBinary(ppresults, binout.str())) {
        std::cerr << "Warning: Failed to save binary" << std::endl;
    } else {
        outputs.ppresults_binary_path = binout.str();
        std::cout << "✓ Saved DIC3DPPresults binary: " << binout.str() << std::endl;
    }
    
    // Optionally generate MAT file
    if (config_.generate_mat_files) {
        std::ostringstream matout;
        matout << inputs.output_dir << "/DIC3DPPresults_" << config_.num_pair << "Pairs_cum_v1.mat";
        
        if (!std::filesystem::exists(matout.str())) {
            if (saveMAT(ppresults, matout.str())) {
                outputs.ppresults_mat_path = matout.str();
                std::cout << "✓ Generated MAT file: " << matout.str() << std::endl;
            }
        } else {
            outputs.ppresults_mat_path = matout.str();
        }
    }
    
    // Generate visualization if enabled
    if (config_.mapLogic) {
        generateVisualization(ppresults, inputs.output_dir, inputs.trial_id);
    }
    
    // Set output statistics
    outputs.num_frames = ppresults.n_frames;
    outputs.num_points = ppresults.Points3D.empty() ? 0 : ppresults.Points3D[0].x.size();
    outputs.num_faces = ppresults.Faces.size() / 3;
    
    std::cout << "\n=== Step F Complete ===" << std::endl;
    return outputs;
}

// ============================================================================
// Milestone Functions
// ============================================================================

DIC3Dcombined DeformationWorkflow::loadDIC3DCombined(const std::string& path) {
    return DIC3Dcombined::loadBinary(path);
}

std::vector<Eigen::Vector3d> DeformationWorkflow::convertToEigen(const Points3D& points3d) {
    std::vector<Eigen::Vector3d> vertices;
    vertices.reserve(points3d.x.size());
    
    for (size_t i = 0; i < points3d.x.size(); ++i) {
        vertices.emplace_back(points3d.x[i], points3d.y[i], points3d.z[i]);
    }
    
    return vertices;
}

std::vector<std::vector<Eigen::Vector3d>> DeformationWorkflow::applyTemporalFiltering(
    const std::vector<std::vector<Eigen::Vector3d>>& vertices_all_frames,
    const std::vector<Eigen::Vector3d>& vertices_ref,
    double freq_filter,
    double freq_acq) {
    
    size_t nFrames = vertices_all_frames.size();
    size_t nPoints = vertices_ref.size();
    
    // Organize data for filtering: nPoints x nFrames
    std::vector<std::vector<double>> disp_x(nPoints, std::vector<double>(nFrames));
    std::vector<std::vector<double>> disp_y(nPoints, std::vector<double>(nFrames));
    std::vector<std::vector<double>> disp_z(nPoints, std::vector<double>(nFrames));
    
    for (size_t iframe = 0; iframe < nFrames; ++iframe) {
        for (size_t ipt = 0; ipt < nPoints; ++ipt) {
            disp_x[ipt][iframe] = vertices_all_frames[iframe][ipt].x() - vertices_ref[ipt].x();
            disp_y[ipt][iframe] = vertices_all_frames[iframe][ipt].y() - vertices_ref[ipt].y();
            disp_z[ipt][iframe] = vertices_all_frames[iframe][ipt].z() - vertices_ref[ipt].z();
        }
    }
    
    // Filter displacement components
    auto [filt_x, filt_y, filt_z] = filterTime3D(disp_x, disp_y, disp_z, freq_filter, freq_acq);
    
    // Reconstruct filtered vertex positions
    std::vector<std::vector<Eigen::Vector3d>> result = vertices_all_frames;
    for (size_t iframe = 0; iframe < nFrames; ++iframe) {
        for (size_t ipt = 0; ipt < nPoints; ++ipt) {
            result[iframe][ipt].x() = vertices_ref[ipt].x() + filt_x[ipt][iframe];
            result[iframe][ipt].y() = vertices_ref[ipt].y() + filt_y[ipt][iframe];
            result[iframe][ipt].z() = vertices_ref[ipt].z() + filt_z[ipt][iframe];
        }
    }
    
    return result;
}

Utils::RigidTransform DeformationWorkflow::computeRigidBodyMotion(
    const std::vector<Eigen::Vector3d>& vertices_current,
    const std::vector<Eigen::Vector3d>& vertices_reference) {
    
    Utils::RigidTransform transform;
    Utils::computeRigidTransform(vertices_current, vertices_reference, transform);
    return transform;
}

std::vector<std::vector<Eigen::Vector3d>> DeformationWorkflow::applyRBMRemoval(
    const std::vector<std::vector<Eigen::Vector3d>>& vertices_all_frames,
    const std::vector<Eigen::Vector3d>& vertices_ref,
    std::vector<Utils::RigidTransform>& transforms) {
    
    transforms.resize(vertices_all_frames.size());
    std::vector<std::vector<Eigen::Vector3d>> result;
    result.reserve(vertices_all_frames.size());
    
    for (size_t iframe = 0; iframe < vertices_all_frames.size(); ++iframe) {
        bool success = Utils::computeRigidTransform(
            vertices_all_frames[iframe], vertices_ref, transforms[iframe]);
        
        if (success) {
            result.push_back(Utils::applyRigidTransform(
                vertices_all_frames[iframe], transforms[iframe]));
        } else {
            std::cerr << "  Warning: RBM computation failed for frame " << iframe << std::endl;
            result.push_back(vertices_all_frames[iframe]);
        }
    }
    
    return result;
}

FrameDeformationResult DeformationWorkflow::computeSurfaceDeformation(
    const std::vector<int>& faces,
    const std::vector<Eigen::Vector3d>& vertices_ref,
    const std::vector<std::vector<Eigen::Vector3d>>& vertices_all_frames,
    bool cumulative) {
    
    return computeTriSurfaceDeformation(faces, vertices_ref, vertices_all_frames, cumulative);
}

void DeformationWorkflow::recomputeDisplacement(
    const std::vector<std::vector<Eigen::Vector3d>>& vertices_all_frames,
    const std::vector<Eigen::Vector3d>& vertices_ref,
    DispData& disp) {
    
    size_t nFrames = vertices_all_frames.size();
    size_t nPoints = vertices_ref.size();
    
    disp.DispVec.clear();
    disp.DispMgn.clear();
    disp.DispVec.resize(nFrames);
    disp.DispMgn.resize(nFrames);
    
    for (size_t iframe = 0; iframe < nFrames; ++iframe) {
        disp.DispVec[iframe].resize(nPoints * 3);
        disp.DispMgn[iframe].resize(nPoints);
        
        for (size_t ipt = 0; ipt < nPoints; ++ipt) {
            double dx = vertices_all_frames[iframe][ipt].x() - vertices_ref[ipt].x();
            double dy = vertices_all_frames[iframe][ipt].y() - vertices_ref[ipt].y();
            double dz = vertices_all_frames[iframe][ipt].z() - vertices_ref[ipt].z();
            
            disp.DispVec[iframe][ipt * 3 + 0] = dx;
            disp.DispVec[iframe][ipt * 3 + 1] = dy;
            disp.DispVec[iframe][ipt * 3 + 2] = dz;
            disp.DispMgn[iframe][ipt] = std::sqrt(dx * dx + dy * dy + dz * dz);
        }
    }
}

std::vector<std::vector<double>> DeformationWorkflow::recomputeFaceCentroids(
    const std::vector<std::vector<Eigen::Vector3d>>& vertices_all_frames,
    const std::vector<int>& faces) {
    
    size_t nFrames = vertices_all_frames.size();
    size_t nFaces = faces.size() / 3;
    
    std::vector<std::vector<double>> centroids(nFrames);
    
    for (size_t iframe = 0; iframe < nFrames; ++iframe) {
        centroids[iframe].resize(nFaces * 3);
        
        for (size_t iface = 0; iface < nFaces; ++iface) {
            int v0 = faces[iface * 3 + 0];
            int v1 = faces[iface * 3 + 1];
            int v2 = faces[iface * 3 + 2];
            
            const auto& verts = vertices_all_frames[iframe];
            centroids[iframe][iface * 3 + 0] = (verts[v0].x() + verts[v1].x() + verts[v2].x()) / 3.0;
            centroids[iframe][iface * 3 + 1] = (verts[v0].y() + verts[v1].y() + verts[v2].y()) / 3.0;
            centroids[iframe][iface * 3 + 2] = (verts[v0].z() + verts[v1].z() + verts[v2].z()) / 3.0;
        }
    }
    
    return centroids;
}

std::vector<std::vector<double>> DeformationWorkflow::recomputeFaceCorrelation(
    const std::vector<std::vector<double>>& point_corr,
    const std::vector<int>& faces) {
    
    size_t nFrames = point_corr.size();
    size_t nFaces = faces.size() / 3;
    
    std::vector<std::vector<double>> face_corr(nFrames);
    
    for (size_t iframe = 0; iframe < nFrames; ++iframe) {
        face_corr[iframe].resize(nFaces);
        
        for (size_t iface = 0; iface < nFaces; ++iface) {
            int v0 = faces[iface * 3 + 0];
            int v1 = faces[iface * 3 + 1];
            int v2 = faces[iface * 3 + 2];
            
            face_corr[iframe][iface] = std::max({
                point_corr[iframe][v0],
                point_corr[iframe][v1],
                point_corr[iframe][v2]
            });
        }
    }
    
    return face_corr;
}

std::vector<double> DeformationWorkflow::computeFaceIsotropyIndex(
    const std::vector<int>& faces,
    const std::vector<Eigen::Vector3d>& vertices) {
    
    return cppxdic::computeFaceIsotropyIndex(faces, vertices);
}

DIC3DPPresults DeformationWorkflow::buildPPResults(
    const DIC3Dcombined& dic3d,
    const FrameDeformationResult& deform_result,
    const FrameDeformationResult& deform_result_arbm,
    const std::vector<Utils::RigidTransform>& rbm_transforms,
    const std::vector<std::vector<Eigen::Vector3d>>& vertices_arbm,
    const std::vector<std::vector<double>>& face_iso_ind) {
    
    DIC3DPPresults ppresults;
    
    // Copy all fields from DIC3Dcombined
    ppresults.pairIndices = dic3d.pairIndices;
    ppresults.Points3D = dic3d.Points3D;
    ppresults.Faces = dic3d.Faces;
    ppresults.FaceColors = dic3d.FaceColors;
    ppresults.corrComb = dic3d.corrComb;
    ppresults.FaceCorrComb = dic3d.FaceCorrComb;
    ppresults.FaceCentroids = dic3d.FaceCentroids;
    ppresults.Disp = dic3d.Disp;
    ppresults.calibration = dic3d.calibration;
    ppresults.distortion = dic3d.distortion;
    ppresults.FacePairInds = dic3d.FacePairInds;
    ppresults.PointPairInds = dic3d.PointPairInds;
    ppresults.DIC2Dinfo = dic3d.DIC2Dinfo;
    ppresults.AllPairsResults = dic3d.AllPairsResults;
    
    // Set deformation type and frame count
    ppresults.deftype = "cum";
    ppresults.n_frames = deform_result.n_frames;
    ppresults.deform_full = deform_result;
    ppresults.FaceIsoInd = face_iso_ind;
    
    // Convert deformation results
    size_t nFaces = dic3d.Faces.size() / 3;
    ppresults.Deform = convertToDeformData(deform_result, nFaces);
    ppresults.Deform_ARBM = convertToDeformData(deform_result_arbm, nFaces);
    
    // Populate RBM data
    ppresults.RBM.RotMat.resize(rbm_transforms.size());
    ppresults.RBM.TransVec.resize(rbm_transforms.size());
    
    for (size_t iframe = 0; iframe < rbm_transforms.size(); ++iframe) {
        const auto& transform = rbm_transforms[iframe];
        
        ppresults.RBM.RotMat[iframe].resize(9);
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j) {
                ppresults.RBM.RotMat[iframe][i * 3 + j] = transform.R(i, j);
            }
        }
        
        ppresults.RBM.TransVec[iframe].resize(3);
        ppresults.RBM.TransVec[iframe][0] = transform.t(0);
        ppresults.RBM.TransVec[iframe][1] = transform.t(1);
        ppresults.RBM.TransVec[iframe][2] = transform.t(2);
    }
    
    // Populate Points3D_ARBM
    ppresults.Points3D_ARBM_x.resize(vertices_arbm.size());
    ppresults.Points3D_ARBM_y.resize(vertices_arbm.size());
    ppresults.Points3D_ARBM_z.resize(vertices_arbm.size());
    
    for (size_t iframe = 0; iframe < vertices_arbm.size(); ++iframe) {
        const auto& verts = vertices_arbm[iframe];
        ppresults.Points3D_ARBM_x[iframe].resize(verts.size());
        ppresults.Points3D_ARBM_y[iframe].resize(verts.size());
        ppresults.Points3D_ARBM_z[iframe].resize(verts.size());
        
        for (size_t i = 0; i < verts.size(); ++i) {
            ppresults.Points3D_ARBM_x[iframe][i] = verts[i].x();
            ppresults.Points3D_ARBM_y[iframe][i] = verts[i].y();
            ppresults.Points3D_ARBM_z[iframe][i] = verts[i].z();
        }
    }
    
    return ppresults;
}

DeformData DeformationWorkflow::convertToDeformData(const FrameDeformationResult& result,
                                                    size_t nFaces) {
    DeformData data;
    data.F.resize(result.n_frames);
    data.strain.resize(result.n_frames);
    data.princStrain.resize(result.n_frames);
    data.maxShearStrain.resize(result.n_frames);
    
    for (size_t iframe = 0; iframe < result.n_frames; ++iframe) {
        const auto& frame = result.frames[iframe];
        
        // Deformation gradient F
        DeformGradient& F = data.F[iframe];
        F.F11.resize(nFaces);
        F.F12.resize(nFaces);
        F.F13.resize(nFaces);
        F.F21.resize(nFaces);
        F.F22.resize(nFaces);
        F.F23.resize(nFaces);
        F.F31.resize(nFaces);
        F.F32.resize(nFaces);
        F.F33.resize(nFaces);
        
        for (size_t iface = 0; iface < nFaces; ++iface) {
            F.F11[iface] = frame.Fmat[iface](0, 0);
            F.F12[iface] = frame.Fmat[iface](0, 1);
            F.F13[iface] = frame.Fmat[iface](0, 2);
            F.F21[iface] = frame.Fmat[iface](1, 0);
            F.F22[iface] = frame.Fmat[iface](1, 1);
            F.F23[iface] = frame.Fmat[iface](1, 2);
            F.F31[iface] = frame.Fmat[iface](2, 0);
            F.F32[iface] = frame.Fmat[iface](2, 1);
            F.F33[iface] = frame.Fmat[iface](2, 2);
        }
        
        // Strain tensor E
        StrainTensor& E = data.strain[iframe];
        E.E11.resize(nFaces);
        E.E12.resize(nFaces);
        E.E13.resize(nFaces);
        E.E21.resize(nFaces);
        E.E22.resize(nFaces);
        E.E23.resize(nFaces);
        E.E31.resize(nFaces);
        E.E32.resize(nFaces);
        E.E33.resize(nFaces);
        
        for (size_t iface = 0; iface < nFaces; ++iface) {
            E.E11[iface] = frame.Emat[iface](0, 0);
            E.E12[iface] = frame.Emat[iface](0, 1);
            E.E13[iface] = frame.Emat[iface](0, 2);
            E.E21[iface] = frame.Emat[iface](1, 0);
            E.E22[iface] = frame.Emat[iface](1, 1);
            E.E23[iface] = frame.Emat[iface](1, 2);
            E.E31[iface] = frame.Emat[iface](2, 0);
            E.E32[iface] = frame.Emat[iface](2, 1);
            E.E33[iface] = frame.Emat[iface](2, 2);
        }
        
        // Principal strains
        data.princStrain[iframe].resize(nFaces * 2);
        for (size_t iface = 0; iface < nFaces; ++iface) {
            data.princStrain[iframe][iface * 2 + 0] = frame.Epc1[iface];
            data.princStrain[iframe][iface * 2 + 1] = frame.Epc2[iface];
        }
        
        // Max shear strain
        data.maxShearStrain[iframe] = frame.EShearMax;
    }
    
    return data;
}

// ============================================================================
// Checkpoint Functions
// ============================================================================

bool DeformationWorkflow::hasDeformationCheckpoint(const std::string& output_dir, 
                                                   int num_pairs) {
    std::ostringstream path;
    path << output_dir << "/DIC3DPPresults_" << num_pairs 
         << "Pairs_cum_" << config_.fileversion << ".bin";
    return std::filesystem::exists(path.str());
}

DIC3DPPresults DeformationWorkflow::loadCheckpoint(const std::string& checkpoint_path) {
    DIC3DPPresults ppresults;
    ppresults.loadBinary(checkpoint_path);
    return ppresults;
}

// ============================================================================
// Output Functions
// ============================================================================

bool DeformationWorkflow::saveBinary(const DIC3DPPresults& ppresults, 
                                     const std::string& path) {
    try {
        ppresults.saveBinary(path);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to save binary: " << e.what() << std::endl;
        return false;
    }
}

bool DeformationWorkflow::saveMAT(const DIC3DPPresults& ppresults, 
                                  const std::string& path) {
    return MatWriter::write3DPPresults(path, ppresults);
}

bool DeformationWorkflow::generateVisualization(const DIC3DPPresults& ppresults,
                                                const std::string& output_dir,
                                                int trial) {
    std::cout << "\n=== Generating Visualization Exports ===" << std::endl;
    
    try {
        Visualization viz(config_);
        
        // Print trial info
        viz.printTrialInfo(ppresults);
        
        // Prepare visualization data
        auto vis_data = viz.prepareVisualizationData(ppresults);
        
        // Export visualization data
        std::ostringstream viz_path;
        viz_path << output_dir << "/viz/trial_" 
                 << std::setfill('0') << std::setw(3) << trial 
                 << "_" << config_.phase_id;
        
        std::filesystem::create_directories(output_dir + "/viz");
        viz.exportData(vis_data, viz_path.str());
        
        // Generate summary statistics
        std::ostringstream stats_path;
        stats_path << output_dir << "/viz/trial_" 
                   << std::setfill('0') << std::setw(3) << trial 
                   << "_summary.txt";
        viz.generateSummaryStats(ppresults, stats_path.str());
        
        std::cout << "✓ Visualization exports complete" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Warning: Visualization export failed: " << e.what() << std::endl;
        return false;
    }
}

// ============================================================================
// Utility Functions
// ============================================================================

DeformationInputs DeformationWorkflow::buildInputs(int trial_id) {
    DeformationInputs inputs;
    inputs.trial_id = trial_id;
    
    inputs.output_dir = config_.dic_path + "/" + config_.subject_id + "/" + config_.material;
    
    std::ostringstream dic3d_path;
    dic3d_path << inputs.output_dir << "/DIC3Dcombined_" << config_.num_pair << "Pairs_stitched.bin";
    inputs.dic3d_combined_path = dic3d_path.str();
    
    // Processing options from config
    inputs.apply_temporal_filtering = true;
    inputs.filter_freq = config_.filterFreq;
    inputs.acquisition_freq = config_.vid_sample_freq;
    inputs.cumulative_deformation = (config_.deftype == "cum" || config_.deftype == "both");
    
    return inputs;
}

void DeformationWorkflow::updatePoints3D(
    const std::vector<std::vector<Eigen::Vector3d>>& vertices_all_frames,
    std::vector<Points3D>& points3d) {
    
    points3d.clear();
    points3d.resize(vertices_all_frames.size());
    
    for (size_t iframe = 0; iframe < vertices_all_frames.size(); ++iframe) {
        const auto& verts = vertices_all_frames[iframe];
        points3d[iframe].x.resize(verts.size());
        points3d[iframe].y.resize(verts.size());
        points3d[iframe].z.resize(verts.size());
        
        for (size_t ipt = 0; ipt < verts.size(); ++ipt) {
            points3d[iframe].x[ipt] = verts[ipt].x();
            points3d[iframe].y[ipt] = verts[ipt].y();
            points3d[iframe].z[ipt] = verts[ipt].z();
        }
    }
}

} // namespace cppxdic

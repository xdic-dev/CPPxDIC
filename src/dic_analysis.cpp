/**
 * DIC Analysis implementation for CPPXDIC
 */

#include "dic_analysis.h"
#include "utils.h"
#include "step_d_workflow.h"
#include "data_serializer.h"
#include "strain_computation.h"
#include "surface_stitching.h"
#include "temporal_filter.h"
#include "face_isotropy.h"
#include "visualization.h"
#include <iostream>
#include <chrono>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <matio.h>
#include <limits>
#include <Eigen/Dense>
#include <cctype>
#include <cmath>
#include <fstream>
// JSON
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>

using namespace ncorr;
using namespace cppxdic;

DicAnalysis::DicAnalysis(const Config& config) : config_(config) {
}

bool DicAnalysis::dicDeformationAnalysis(const std::vector<int>& trial_target) {
    std::cout << "Starting Deformation/Strain Analysis (Step F)..." << std::endl;
    std::cout << "NOTE: This requires DIC3Dcombined from Step E" << std::endl;
    
    try {
        for (int trial : trial_target) {
            // Step F works on combined 3D reconstruction, not per-pair
            std::string output_dir = config_.dic_path + "/" + config_.subject_id + "/" + config_.material;
            
            // Load DIC3Dcombined from Step E output using configured format
            auto serializer = cppxdic::DataSerializer::create(config_.data_format);
            std::string dic3d_file = output_dir + "/DIC3Dcombined_" + std::to_string(config_.num_pair) 
                                   + "Pairs_stitched" + serializer->extension();
            
            if (!std::filesystem::exists(dic3d_file)) {
                std::cerr << "ERROR: DIC3Dcombined file not found: " << dic3d_file << std::endl;
                std::cerr << "You must run Step E (dic3DReconstruction) first!" << std::endl;
                return false;
            }
            
            std::cout << "Loading DIC3Dcombined from: " << dic3d_file << std::endl;
            
            DIC3Dcombined dic3d;
            if (!serializer->loadDIC3Dcombined(dic3d_file, dic3d)) {
                std::cerr << "Failed to load DIC3Dcombined structure" << std::endl;
                return false;
            }
            
            if (dic3d.Points3D.empty() || dic3d.Faces.empty()) {
                std::cerr << "DIC3Dcombined has no 3D data" << std::endl;
                return false;
            }
            
            std::cout << "  Loaded: " << dic3d.Points3D.size() << " frames, "
                      << dic3d.Points3D[0].x.size() << " points, "
                      << dic3d.Faces.size() / 3 << " faces" << std::endl;
            
            // Convert Points3D to Eigen::Vector3d format for deformation computation
            std::cout << "\nConverting data to Eigen format..." << std::endl;
            
            // Reference frame (frame 0)
            std::vector<Eigen::Vector3d> vertices_ref;
            size_t nPoints = dic3d.Points3D[0].x.size();
            vertices_ref.reserve(nPoints);
            for (size_t i = 0; i < nPoints; ++i) {
                vertices_ref.emplace_back(
                    dic3d.Points3D[0].x[i],
                    dic3d.Points3D[0].y[i],
                    dic3d.Points3D[0].z[i]
                );
            }
            
            // All frames
            std::vector<std::vector<Eigen::Vector3d>> vertices_all_frames;
            vertices_all_frames.reserve(dic3d.Points3D.size());
            for (const auto& frame_pts : dic3d.Points3D) {
                std::vector<Eigen::Vector3d> frame_verts;
                frame_verts.reserve(frame_pts.x.size());
                for (size_t i = 0; i < frame_pts.x.size(); ++i) {
                    frame_verts.emplace_back(
                        frame_pts.x[i],
                        frame_pts.y[i],
                        frame_pts.z[i]
                    );
                }
                vertices_all_frames.push_back(std::move(frame_verts));
            }
            
            std::cout << "  Converted " << vertices_all_frames.size() << " frames" << std::endl;
            
            // Apply temporal filtering to displacement fields (optional but recommended)
            bool apply_temporal_filtering = true;  // Can be made configurable
            if (apply_temporal_filtering && vertices_all_frames.size() > 3) {
                std::cout << "\nApplying temporal filtering..." << std::endl;
                
                // Organize data for filtering: nPoints x nFrames
                size_t nFrames = vertices_all_frames.size();
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
                double freq_filt = 10.0;  // Low-pass cutoff frequency (Hz)
                double freq_acq = 50.0;   // Acquisition frequency (Hz) - adjust based on your data
                auto [filt_x, filt_y, filt_z] = filterTime3D(disp_x, disp_y, disp_z, freq_filt, freq_acq);
                
                // Reconstruct filtered vertex positions
                for (size_t iframe = 0; iframe < nFrames; ++iframe) {
                    for (size_t ipt = 0; ipt < nPoints; ++ipt) {
                        vertices_all_frames[iframe][ipt].x() = vertices_ref[ipt].x() + filt_x[ipt][iframe];
                        vertices_all_frames[iframe][ipt].y() = vertices_ref[ipt].y() + filt_y[ipt][iframe];
                        vertices_all_frames[iframe][ipt].z() = vertices_ref[ipt].z() + filt_z[ipt][iframe];
                    }
                }
                
                std::cout << "  ✓ Temporal filtering applied (freqFilt=" << freq_filt 
                          << " Hz, freqAcq=" << freq_acq << " Hz)" << std::endl;
            } else if (apply_temporal_filtering) {
                std::cout << "\nSkipping temporal filtering (too few frames: " 
                          << vertices_all_frames.size() << ")" << std::endl;
            }
            
            // Update Points3D with filtered data (matching MATLAB line 115)
            std::cout << "\nUpdating Points3D with filtered data..." << std::endl;
            dic3d.Points3D.clear();
            dic3d.Points3D.resize(vertices_all_frames.size());
            for (size_t iframe = 0; iframe < vertices_all_frames.size(); ++iframe) {
                Points3D& frame_pts = dic3d.Points3D[iframe];
                frame_pts.x.resize(nPoints);
                frame_pts.y.resize(nPoints);
                frame_pts.z.resize(nPoints);
                for (size_t ipt = 0; ipt < nPoints; ++ipt) {
                    frame_pts.x[ipt] = vertices_all_frames[iframe][ipt].x();
                    frame_pts.y[ipt] = vertices_all_frames[iframe][ipt].y();
                    frame_pts.z[ipt] = vertices_all_frames[iframe][ipt].z();
                }
            }
            std::cout << "  ✓ Points3D updated with filtered data" << std::endl;
            
            // Recompute displacement based on filtered Points3D (matching MATLAB lines 69-71)
            std::cout << "\nRecomputing displacement after filtering..." << std::endl;
            dic3d.Disp.DispVec.clear();
            dic3d.Disp.DispMgn.clear();
            dic3d.Disp.DispVec.resize(vertices_all_frames.size());
            dic3d.Disp.DispMgn.resize(vertices_all_frames.size());
            
            for (size_t iframe = 0; iframe < vertices_all_frames.size(); ++iframe) {
                std::vector<double>& disp_vec = dic3d.Disp.DispVec[iframe];
                std::vector<double>& disp_mgn = dic3d.Disp.DispMgn[iframe];
                
                disp_vec.resize(nPoints * 3);
                disp_mgn.resize(nPoints);
                
                for (size_t ipt = 0; ipt < nPoints; ++ipt) {
                    double dx = vertices_all_frames[iframe][ipt].x() - vertices_ref[ipt].x();
                    double dy = vertices_all_frames[iframe][ipt].y() - vertices_ref[ipt].y();
                    double dz = vertices_all_frames[iframe][ipt].z() - vertices_ref[ipt].z();
                    
                    disp_vec[ipt * 3 + 0] = dx;
                    disp_vec[ipt * 3 + 1] = dy;
                    disp_vec[ipt * 3 + 2] = dz;
                    
                    disp_mgn[ipt] = std::sqrt(dx*dx + dy*dy + dz*dz);
                }
            }
            std::cout << "  ✓ Displacement recomputed for " << vertices_all_frames.size() << " frames" << std::endl;
            
            // Recompute face centroids based on filtered Points3D (matching MATLAB lines 64-66)
            std::cout << "\nRecomputing face centroids after filtering..." << std::endl;
            dic3d.FaceCentroids.clear();
            dic3d.FaceCentroids.resize(vertices_all_frames.size());
            
            size_t nFaces = dic3d.Faces.size() / 3;
            for (size_t iframe = 0; iframe < vertices_all_frames.size(); ++iframe) {
                std::vector<double>& centroids = dic3d.FaceCentroids[iframe];
                centroids.resize(nFaces * 3);
                
                for (size_t iface = 0; iface < nFaces; ++iface) {
                    int v0 = dic3d.Faces[iface * 3 + 0];
                    int v1 = dic3d.Faces[iface * 3 + 1];
                    int v2 = dic3d.Faces[iface * 3 + 2];
                    
                    double cx = (vertices_all_frames[iframe][v0].x() + 
                                 vertices_all_frames[iframe][v1].x() + 
                                 vertices_all_frames[iframe][v2].x()) / 3.0;
                    double cy = (vertices_all_frames[iframe][v0].y() + 
                                 vertices_all_frames[iframe][v1].y() + 
                                 vertices_all_frames[iframe][v2].y()) / 3.0;
                    double cz = (vertices_all_frames[iframe][v0].z() + 
                                 vertices_all_frames[iframe][v1].z() + 
                                 vertices_all_frames[iframe][v2].z()) / 3.0;
                    
                    centroids[iface * 3 + 0] = cx;
                    centroids[iface * 3 + 1] = cy;
                    centroids[iface * 3 + 2] = cz;
                }
            }
            std::cout << "  ✓ Face centroids recomputed for " << vertices_all_frames.size() << " frames" << std::endl;
            
            // Recompute face correlation (worst of 3 vertices) (matching MATLAB line 61)
            std::cout << "\nRecomputing face correlation after filtering..." << std::endl;
            dic3d.FaceCorrComb.clear();
            dic3d.FaceCorrComb.resize(vertices_all_frames.size());
            
            for (size_t iframe = 0; iframe < vertices_all_frames.size(); ++iframe) {
                std::vector<double>& face_corr = dic3d.FaceCorrComb[iframe];
                face_corr.resize(nFaces);
                
                const auto& point_corr = dic3d.corrComb[iframe];
                
                for (size_t iface = 0; iface < nFaces; ++iface) {
                    int v0 = dic3d.Faces[iface * 3 + 0];
                    int v1 = dic3d.Faces[iface * 3 + 1];
                    int v2 = dic3d.Faces[iface * 3 + 2];
                    
                    // Take worst (max) correlation of 3 vertices
                    face_corr[iface] = std::max({point_corr[v0], point_corr[v1], point_corr[v2]});
                }
            }
            std::cout << "  ✓ Face correlation recomputed for " << vertices_all_frames.size() << " frames" << std::endl;
            
            // Compute rigid body motion (RBM) removal (matching MATLAB STEP4 lines 61-70)
            std::cout << "\nComputing rigid body motion (RBM) transformations..." << std::endl;
            std::vector<Utils::RigidTransform> rbm_transforms(vertices_all_frames.size());
            std::vector<std::vector<Eigen::Vector3d>> vertices_all_frames_ARBM;
            vertices_all_frames_ARBM.reserve(vertices_all_frames.size());
            
            for (size_t iframe = 0; iframe < vertices_all_frames.size(); ++iframe) {
                Utils::RigidTransform transform;
                bool success = Utils::computeRigidTransform(
                    vertices_all_frames[iframe],  // from
                    vertices_ref,                  // to (reference frame)
                    transform
                );
                
                rbm_transforms[iframe] = transform;
                
                if (success) {
                    // Apply RBM transformation to get ARBM (After RBM) coordinates
                    auto verts_arbm = Utils::applyRigidTransform(vertices_all_frames[iframe], transform);
                    vertices_all_frames_ARBM.push_back(verts_arbm);
                } else {
                    std::cerr << "  Warning: RBM computation failed for frame " << iframe << std::endl;
                    vertices_all_frames_ARBM.push_back(vertices_all_frames[iframe]);
                }
            }
            std::cout << "  ✓ RBM transformations computed for " << vertices_all_frames.size() << " frames" << std::endl;
            
            // Compute 3D deformation and strain (with original coordinates)
            std::cout << "\nComputing 3D surface deformation (with RBM)..." << std::endl;
            std::cout << "  Method: Triangular Cosserat Point Elements (TCPE)" << std::endl;
            std::cout << "  Deformation type: Cumulative (reference = frame 1)" << std::endl;
            
            FrameDeformationResult deform_result = computeTriSurfaceDeformation(
                dic3d.Faces,
                vertices_ref,
                vertices_all_frames,
                true  // cumulative: use frame 1 as reference for all frames
            );
            
            std::cout << "  ✓ Deformation computation complete (with RBM)" << std::endl;
            
            // Compute deformation after RBM removal (matching MATLAB STEP4 lines 84-85)
            std::cout << "\nComputing 3D surface deformation (after RBM removal)..." << std::endl;
            FrameDeformationResult deform_result_ARBM = computeTriSurfaceDeformation(
                dic3d.Faces,
                vertices_all_frames_ARBM[0],  // reference frame after RBM
                vertices_all_frames_ARBM,
                true  // cumulative
            );
            
            std::cout << "  ✓ Deformation computation complete (ARBM)" << std::endl;
            std::cout << "\n✓ Both deformation analyses complete!" << std::endl;
            std::cout << "  - With RBM: Deformation gradient F, strain tensors E/e" << std::endl;
            std::cout << "  - After RBM: Deformation gradient F_ARBM, strain tensors E_ARBM/e_ARBM" << std::endl;
            
            // Build DIC3DPPresults structure
            std::cout << "\nBuilding DIC3DPPresults structure..." << std::endl;
            DIC3DPPresults ppresults;
            
            // Store full deformation result for MAT file writing (all 36 fields)
            ppresults.deform_full = deform_result;
            
            // Compute face isotropy index for each frame
            std::cout << "\nComputing face isotropy index..." << std::endl;
            ppresults.FaceIsoInd.resize(vertices_all_frames.size());
            for (size_t iframe = 0; iframe < vertices_all_frames.size(); ++iframe) {
                ppresults.FaceIsoInd[iframe] = computeFaceIsotropyIndex(
                    dic3d.Faces,
                    vertices_all_frames[iframe]
                );
            }
            std::cout << "  ✓ Face isotropy index computed for " << vertices_all_frames.size() << " frames" << std::endl;
            
            // Copy all fields from DIC3Dcombined (inheritance)
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
            ppresults.deftype = "cum";  // cumulative deformation
            ppresults.n_frames = deform_result.n_frames;
            
            // Convert FrameDeformationResult to DeformData
            // This extracts the key fields needed for MATLAB compatibility
            ppresults.Deform.F.resize(deform_result.n_frames);
            ppresults.Deform.strain.resize(deform_result.n_frames);
            ppresults.Deform.princStrain.resize(deform_result.n_frames);
            ppresults.Deform.maxShearStrain.resize(deform_result.n_frames);
            
            // nFaces already defined above (line 204)
            
            for (size_t iframe = 0; iframe < deform_result.n_frames; ++iframe) {
                const auto& frame = deform_result.frames[iframe];
                
                // Deformation gradient F (3x3 per face)
                DeformGradient& F = ppresults.Deform.F[iframe];
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
                
                // Green-Lagrangian strain tensor E (3x3 per face)
                StrainTensor& E = ppresults.Deform.strain[iframe];
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
                
                // Principal strains (2 per face: Epc1, Epc2)
                std::vector<double>& princStrain = ppresults.Deform.princStrain[iframe];
                princStrain.resize(nFaces * 2);
                for (size_t iface = 0; iface < nFaces; ++iface) {
                    princStrain[iface * 2 + 0] = frame.Epc1[iface];
                    princStrain[iface * 2 + 1] = frame.Epc2[iface];
                }
                
                // Max shear strain (1 per face)
                std::vector<double>& maxShear = ppresults.Deform.maxShearStrain[iframe];
                maxShear = frame.EShearMax;
            }
            
            std::cout << "  Populated deformation data (with RBM) for " << ppresults.n_frames << " frames" << std::endl;
            
            // Populate ARBM deformation data (after rigid body motion removal)
            std::cout << "  Populating ARBM deformation data..." << std::endl;
            ppresults.Deform_ARBM.F.resize(deform_result_ARBM.n_frames);
            ppresults.Deform_ARBM.strain.resize(deform_result_ARBM.n_frames);
            ppresults.Deform_ARBM.princStrain.resize(deform_result_ARBM.n_frames);
            ppresults.Deform_ARBM.maxShearStrain.resize(deform_result_ARBM.n_frames);
            
            for (size_t iframe = 0; iframe < deform_result_ARBM.n_frames; ++iframe) {
                const auto& frame = deform_result_ARBM.frames[iframe];
                
                // Deformation gradient F_ARBM
                DeformGradient& F = ppresults.Deform_ARBM.F[iframe];
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
                
                // Strain tensor E_ARBM
                StrainTensor& E = ppresults.Deform_ARBM.strain[iframe];
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
                std::vector<double>& princStrain = ppresults.Deform_ARBM.princStrain[iframe];
                princStrain.resize(nFaces * 2);
                for (size_t iface = 0; iface < nFaces; ++iface) {
                    princStrain[iface * 2 + 0] = frame.Epc1[iface];
                    princStrain[iface * 2 + 1] = frame.Epc2[iface];
                }
                
                // Max shear strain
                ppresults.Deform_ARBM.maxShearStrain[iframe] = frame.EShearMax;
            }
            
            // Populate RBM transformation matrices
            ppresults.RBM.RotMat.resize(rbm_transforms.size());
            ppresults.RBM.TransVec.resize(rbm_transforms.size());
            for (size_t iframe = 0; iframe < rbm_transforms.size(); ++iframe) {
                const auto& transform = rbm_transforms[iframe];
                
                // Rotation matrix (row-major 9 values)
                ppresults.RBM.RotMat[iframe].resize(9);
                for (int i = 0; i < 3; ++i) {
                    for (int j = 0; j < 3; ++j) {
                        ppresults.RBM.RotMat[iframe][i * 3 + j] = transform.R(i, j);
                    }
                }
                
                // Translation vector (3 values)
                ppresults.RBM.TransVec[iframe].resize(3);
                ppresults.RBM.TransVec[iframe][0] = transform.t(0);
                ppresults.RBM.TransVec[iframe][1] = transform.t(1);
                ppresults.RBM.TransVec[iframe][2] = transform.t(2);
            }
            
            // Populate Points3D_ARBM
            ppresults.Points3D_ARBM_x.resize(vertices_all_frames_ARBM.size());
            ppresults.Points3D_ARBM_y.resize(vertices_all_frames_ARBM.size());
            ppresults.Points3D_ARBM_z.resize(vertices_all_frames_ARBM.size());
            
            for (size_t iframe = 0; iframe < vertices_all_frames_ARBM.size(); ++iframe) {
                const auto& verts = vertices_all_frames_ARBM[iframe];
                ppresults.Points3D_ARBM_x[iframe].resize(verts.size());
                ppresults.Points3D_ARBM_y[iframe].resize(verts.size());
                ppresults.Points3D_ARBM_z[iframe].resize(verts.size());
                
                for (size_t i = 0; i < verts.size(); ++i) {
                    ppresults.Points3D_ARBM_x[iframe][i] = verts[i].x();
                    ppresults.Points3D_ARBM_y[iframe][i] = verts[i].y();
                    ppresults.Points3D_ARBM_z[iframe][i] = verts[i].z();
                }
            }
            
            std::cout << "  ✓ Populated all deformation data (with RBM and ARBM) for " << ppresults.n_frames << " frames" << std::endl;
            
            // Save DIC3DPPresults using configured format
            {
                auto pp_serializer = cppxdic::DataSerializer::create(config_.data_format);
                std::string ppout = output_dir + "/DIC3DPPresults_" + std::to_string(config_.num_pair) 
                                  + "Pairs_cum_" + config_.fileversion + pp_serializer->extension();
                
                if (std::filesystem::exists(ppout)) {
                    std::cout << "\nCheckpoint found: " << ppout << " (skipping)" << std::endl;
                } else {
                    std::cout << "\nWriting DIC3DPPresults to: " << ppout << std::endl;
                    if (!pp_serializer->saveDIC3DPPresults(ppout, ppresults)) {
                        std::cerr << "ERROR: Failed to write DIC3DPPresults" << std::endl;
                        return false;
                    }
                    std::cout << "✓ Saved DIC3DPPresults: " << ppout << std::endl;
                }
            }
            
            // Generate visualization exports if enabled
            if (config_.mapLogic) {
                std::cout << "\n=== Generating Visualization Exports ===" << std::endl;
                try {
                    cppxdic::Visualization viz(config_);
                    
                    // Print trial info
                    viz.printTrialInfo(ppresults);
                    
                    // Apply filtering if enabled
                    viz.applyTemporalFilter(ppresults);
                    
                    // Prepare visualization data
                    auto vis_data = viz.prepareVisualizationData(ppresults);
                    
                    // Export visualization data
                    std::ostringstream viz_path;
                    viz_path << output_dir << "/viz/trial_" 
                             << std::setfill('0') << std::setw(3) << trial 
                             << "_" << config_.phase_id;
                    
                    // Create viz directory if needed
                    std::filesystem::create_directories(output_dir + "/viz");
                    
                    viz.exportData(vis_data, viz_path.str());
                    
                    // Generate summary statistics
                    std::ostringstream stats_path;
                    stats_path << output_dir << "/viz/trial_" 
                               << std::setfill('0') << std::setw(3) << trial 
                               << "_summary.txt";
                    viz.generateSummaryStats(ppresults, stats_path.str());
                    
                    std::cout << "✓ Visualization exports complete" << std::endl;
                    
                } catch (const std::exception& e) {
                    std::cerr << "Warning: Visualization export failed: " << e.what() << std::endl;
                    std::cerr << "  (Continuing anyway...)" << std::endl;
                }
            }
            
            std::cout << "\n=== Step F Complete ==="  << std::endl;
            std::cout << "✓ 3D deformation and strain analysis finished for trial " << trial << std::endl;
        }
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "ERROR in Deformation/Strain Analysis: " << e.what() << std::endl;
        return false;
    }
}
bool DicAnalysis::dic3DReconstruction(const std::vector<int>& trial_target) {
    std::cout << "Starting 3D Reconstruction (Step E)..." << std::endl;
    try {

        auto write_bin = [](const std::string& path, const std::vector<double>& buf){
            std::ofstream ofs(path, std::ios::binary);
            ofs.write(reinterpret_cast<const char*>(buf.data()), static_cast<std::streamsize>(buf.size()*sizeof(double)));
        };

        auto build_faces_from_roi = [](const ncorr::ROI2D& roi, std::vector<int>& faces, std::vector<int>& indexLUT, int& W, int& H){
            const auto& mask = roi.get_mask();
            H = static_cast<int>(mask.height());
            W = static_cast<int>(mask.width());
            indexLUT.assign(H*W, -1);
            int idx=0;
            for (int y=0;y<H;++y){
                for (int x=0;x<W;++x){
                    if (mask(y,x)) indexLUT[y*W+x]=idx++;
                }
            }
            for (int y=0;y<H-1;++y){
                for (int x=0;x<W-1;++x){
                    int a=indexLUT[y*W+x];
                    int b=indexLUT[y*W+(x+1)];
                    int c=indexLUT[(y+1)*W+x];
                    int d=indexLUT[(y+1)*W+(x+1)];
                    if (a>=0 && b>=0 && c>=0) { faces.push_back(a); faces.push_back(b); faces.push_back(c); }
                    if (b>=0 && c>=0 && d>=0) { faces.push_back(b); faces.push_back(d); faces.push_back(c); }
                }
            }
        };

        auto extract_points2D = [](const ncorr::Disp2D& disp, std::vector<double>& pts_xy, std::vector<int>& indexLUT, int W, int H){
            const auto& Au = disp.get_u().get_array();
            const auto& Av = disp.get_v().get_array();
            // get_scalefactor() returns spacing+1 (the stride between subset centers)
            int sf = disp.get_scalefactor();
            pts_xy.clear(); pts_xy.reserve(std::count_if(indexLUT.begin(), indexLUT.end(), [](int v){return v>=0;})*2);
            // Build in index order
            // x,y are reduced coords; u,v are full-resolution displacements
            // Convert to full-resolution pixel coords: px = x*sf + u
            for (int y=0;y<H;++y){
                for (int x=0;x<W;++x){
                    int idx = indexLUT[y*W+x];
                    if (idx<0) continue;
                    double u = Au(y,x);
                    double v = Av(y,x);
                    double px = static_cast<double>(x) * sf + u;
                    double py = static_cast<double>(y) * sf + v;
                    pts_xy.resize(std::max((size_t)((idx+1)*2), pts_xy.size()));
                    pts_xy[idx*2+0] = px;
                    pts_xy[idx*2+1] = py;
                }
            }
        };

        //

        auto solve_3d = [](const std::vector<double>& L1, const std::vector<double>& L2, double x1, double y1, double x2, double y2){
            // Build A X = b, A is 4x3, b is 4
            auto rows = [&](const std::vector<double>& L, double x, double y){
                double l1=L[0],l2=L[1],l3=L[2],l4=L[3],l5=L[4],l6=L[5],l7=L[6],l8=L[7],l9=L[8],l10=L[9],l11=L[10];
                std::array<double,3> r1{l1 - x*l9, l2 - x*l10, l3 - x*l11};
                std::array<double,3> r2{l5 - y*l9, l6 - y*l10, l7 - y*l11};
                double b1 = x - l4;
                double b2 = y - l8;
                return std::tuple(r1,r2,b1,b2);
            };
            auto [r11,r12,b11,b12] = rows(L1,x1,y1);
            auto [r21,r22,b21,b22] = rows(L2,x2,y2);
            // Normal equations A^T A X = A^T b
            double ATA[3][3] = {{0}}; double ATb[3]={0};
            auto accum = [&](const std::array<double,3>& r, double b){
                for(int i=0;i<3;++i){ ATb[i]+=r[i]*b; for(int j=0;j<3;++j) ATA[i][j]+=r[i]*r[j]; }
            };
            accum(r11,b11); accum(r12,b12); accum(r21,b21); accum(r22,b22);
            // Solve 3x3 via Cramer's or Gaussian elimination
            // Gaussian elimination
            double A_[3][4] = {
                {ATA[0][0], ATA[0][1], ATA[0][2], ATb[0]},
                {ATA[1][0], ATA[1][1], ATA[1][2], ATb[1]},
                {ATA[2][0], ATA[2][1], ATA[2][2], ATb[2]}
            };
            for(int i=0;i<3;++i){
                // pivot
                int piv=i; for(int r=i+1;r<3;++r) if (std::fabs(A_[r][i])>std::fabs(A_[piv][i])) piv=r;
                if (piv!=i) for(int c=0;c<4;++c) std::swap(A_[i][c],A_[piv][c]);
                double diag = A_[i][i]; if (std::fabs(diag)<1e-12) continue; for(int c=i;c<4;++c) A_[i][c]/=diag;
                for(int r=0;r<3;++r){ if (r==i) continue; double f=A_[r][i]; for(int c=i;c<4;++c) A_[r][c]-=f*A_[i][c]; }
            }
            return std::array<double,3>{A_[0][3],A_[1][3],A_[2][3]};
        };

        for (int trial : trial_target) {
            // Collect all individual pair results before stitching
            std::vector<DIC3DpairResults> all_pairs;
            std::vector<DIC2DPairResults> dic2d_info;
            
            // Format trial as 3-digit string (e.g., "005")
            std::ostringstream trial_str;
            trial_str << std::setw(3) << std::setfill('0') << trial;
            
            for (int pair = 1; pair <= config_.num_pair; ++pair) {
                std::cout << "\n=== Processing Pair " << pair << " ===" << std::endl;
                
                // Get camera numbers for this pair (pair 1 -> cams 1,2; pair 2 -> cams 3,4)
                int cam_1 = (pair - 1) * 2 + 1;
                int cam_2 = (pair - 1) * 2 + 2;
                
                // Load ncorr DIC outputs from output directory (ncorr native .bin format)
                std::string output_dir = config_.dic_path + "/" + config_.subject_id + "/" + config_.material + "/" + trial_str.str() + "/" + config_.phase_id;
                std::string cam1_bin = output_dir + "/ncorr" + std::to_string(cam_1) + ".bin";
                std::string cam2_bin = output_dir + "/ncorr" + std::to_string(cam_2) + ".bin";
                std::string matching_bin = output_dir + "/ncorr" + std::to_string(cam_1) + std::to_string(cam_2) + ".bin";
                
                if (!std::filesystem::exists(cam1_bin) || !std::filesystem::exists(cam2_bin)) {
                    std::cerr << "Missing cached 2D outputs for trial " << trial << ", pair " << pair << ". Skipping." << std::endl;
                    std::cerr << "  Expected: " << cam1_bin << std::endl;
                    std::cerr << "  Expected: " << cam2_bin << std::endl;
                    continue;
                }
                
                if (config_.debug_mode) {
                    std::cout << "[DEBUG] Loading 2D DIC data from:" << std::endl;
                    std::cout << "[DEBUG]   Cam" << cam_1 << ": " << cam1_bin << std::endl;
                    std::cout << "[DEBUG]   Cam" << cam_2 << ": " << cam2_bin << std::endl;
                }

                // Locate calibration .mat files for this camera pair
                std::string calib_dir = config_.data_path + "/rawdata/" + config_.subject_id + "/speckles/" + config_.material + "/calibration/";
                std::string calib_cam1, calib_cam2;
                std::string cam1_str = "cam" + std::to_string(cam_1);
                std::string cam2_str = "cam" + std::to_string(cam_2);
                std::string camera1_str = "camera" + std::to_string(cam_1);
                std::string camera2_str = "camera" + std::to_string(cam_2);
                
                if (std::filesystem::exists(calib_dir)) {
                    for (const auto& entry : std::filesystem::directory_iterator(calib_dir)) {
                        if (!entry.is_regular_file()) continue;
                        auto name = entry.path().filename().string();
                        std::string lower = name; std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                        if (lower.find(cam1_str) != std::string::npos || lower.find(camera1_str) != std::string::npos) calib_cam1 = entry.path().string();
                        if (lower.find(cam2_str) != std::string::npos || lower.find(camera2_str) != std::string::npos) calib_cam2 = entry.path().string();
                        if (!calib_cam1.empty() && !calib_cam2.empty()) break;
                    }
                }

                // Read DLT parameters from calibration MAT files (match Matlab field names)
                auto read_dltparams = [&](const std::string& matpath)->std::vector<double> {
                    std::vector<double> L;
                    if (matpath.empty()) return L;
                    mat_t *fp = Mat_Open(matpath.c_str(), MAT_ACC_RDONLY);
                    if (!fp) return L;
                    // Expect variable 'DLTstructCam' with field 'DLTparams'
                    matvar_t *st = Mat_VarRead(fp, "DLTstructCam");
                    if (st && st->class_type == MAT_C_STRUCT) {
                        matvar_t *field = Mat_VarGetStructFieldByName(st, "DLTparams", 0);
                        if (field && field->data && field->class_type == MAT_C_DOUBLE) {
                            size_t n = 1;
                            for (int i=0;i<field->rank;i++) n *= field->dims[i];
                            const double* d = static_cast<const double*>(field->data);
                            L.assign(d, d + n);
                        }
                    }
                    if (st) Mat_VarFree(st);
                    Mat_Close(fp);
                    return L;
                };

                std::vector<double> L1 = read_dltparams(calib_cam1);
                std::vector<double> L2 = read_dltparams(calib_cam2);
                
                // Load distortion parameters (if available)
                Utils::CameraParameters distortion_cam1, distortion_cam2;
                std::string distortion_cam1_path, distortion_cam2_path;
                bool use_distortion_removal = false;
                
                // Look for distortion parameter files in calibration directory
                if (std::filesystem::exists(calib_dir)) {
                    // Build search strings for this camera pair
                    std::string cam_1_search = "cam_" + std::to_string(cam_1);
                    std::string cam_2_search = "cam_" + std::to_string(cam_2);
                    std::string camera_1_search = "camera_" + std::to_string(cam_1);
                    std::string camera_2_search = "camera_" + std::to_string(cam_2);
                    
                    for (const auto& entry : std::filesystem::directory_iterator(calib_dir)) {
                        if (!entry.is_regular_file()) continue;
                        auto name = entry.path().filename().string();
                        std::string lower = name;
                        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                        // Look for cameraCBparameters files
                        if (lower.find("cameracbparameters") != std::string::npos) {
                            if (lower.find(cam_1_search) != std::string::npos || lower.find(camera_1_search) != std::string::npos) {
                                distortion_cam1_path = entry.path().string();
                            }
                            if (lower.find(cam_2_search) != std::string::npos || lower.find(camera_2_search) != std::string::npos) {
                                distortion_cam2_path = entry.path().string();
                            }
                        }
                    }
                    
                    // Load distortion parameters if found
                    if (!distortion_cam1_path.empty() && !distortion_cam2_path.empty()) {
                        bool cam1_loaded = Utils::loadCameraParameters(distortion_cam1_path, distortion_cam1);
                        bool cam2_loaded = Utils::loadCameraParameters(distortion_cam2_path, distortion_cam2);
                        if (cam1_loaded && cam2_loaded) {
                            use_distortion_removal = true;
                            std::cout << "  ✓ Distortion removal enabled" << std::endl;
                        } else {
                            std::cout << "  ! Distortion parameters found but failed to load" << std::endl;
                        }
                    } else {
                        std::cout << "  ! No distortion parameters found (undistorted points will be used as-is)" << std::endl;
                    }
                }
                
                // Persist a calibration.json for traceability
                try {
                    std::ostringstream cjson;
                    cjson << config_.dic_path << "/" << config_.subject_id << "/" << config_.material << "/calibration.json";
                    nlohmann::json jc;
                    jc["subject"] = config_.subject_id;
                    jc["material"] = config_.material;
                    jc["phase"] = config_.phase_id;
                    jc["cam1_mat"] = calib_cam1;
                    jc["cam2_mat"] = calib_cam2;
                    if (!L1.empty()) jc["DLTparameters"]["cam1"] = L1;
                    if (!L2.empty()) jc["DLTparameters"]["cam2"] = L2;
                    std::ofstream oc(cjson.str());
                    oc << jc.dump(2) << std::endl;
                } catch (...) {}

                // Load DIC outputs
                auto dic1 = DIC_analysis_output::load(cam1_bin);
                auto dic2 = DIC_analysis_output::load(cam2_bin);
                if (dic1.disps.size() != dic2.disps.size()) {
                    std::cerr << "Cam1/Cam2 frame count mismatch for trial " << trial << " pair " << pair << std::endl;
                    continue;
                }
                
                // Load matching displacement (cam1_ref -> cam2_ref) for point correspondence
                // Without this, cam1 grid position (y,x) would be incorrectly paired with
                // cam2 grid position (y,x), but they represent different physical points.
                bool has_matching = false;
                ncorr::DIC_analysis_output dic12;
                if (std::filesystem::exists(matching_bin)) {
                    dic12 = DIC_analysis_output::load(matching_bin);
                    if (!dic12.disps.empty()) {
                        has_matching = true;
                        std::cout << "  ✓ Loaded matching displacement for cam" << cam_1 << "→cam" << cam_2 << " correspondence" << std::endl;
                    }
                }
                if (!has_matching) {
                    std::cerr << "  WARNING: No matching displacement found (" << matching_bin << ")." << std::endl;
                    std::cerr << "           cam2 points will use direct grid mapping (may be inaccurate for large parallax)." << std::endl;
                }

                // Build faces and index LUT from reference ROI (use frame 0 from cam1)
                std::vector<int> faces; std::vector<int> indexLUT; int W=0,H=0;
                build_faces_from_roi(dic1.disps.front().get_roi(), faces, indexLUT, W, H);

                // Create individual pair result structure
                DIC3DpairResults pair_result;
                pair_result.cameraPairInd = {cam_1, cam_2};  // Camera pair indices
                pair_result.DLTpath = {calib_cam1, calib_cam2};
                pair_result.DLTparameters = {L1, L2};
                
                std::vector<double> P3D_ref; // frame 1 reference
                
                // Extract correlation coefficients from both cameras (for combined corr)
                std::vector<std::vector<double>> corrCam1, corrCam2;
                
                // Lambda to extract correlation data using indexLUT
                auto extract_correlation = [](const ncorr::Disp2D& disp, std::vector<double>& corr_out, 
                                            const std::vector<int>& indexLUT, int W, int H) {
                    const auto& cc_array = disp.get_cc().get_array();
                    const auto& roi_mask = disp.get_roi().get_mask();
                    size_t num_points = std::count_if(indexLUT.begin(), indexLUT.end(), [](int v){return v>=0;});
                    corr_out.clear();
                    corr_out.resize(num_points, 0.0);
                    for (int y=0; y<H; ++y) {
                        for (int x=0; x<W; ++x) {
                            int idx = indexLUT[y*W+x];
                            if (idx < 0) continue;
                            double cc = (roi_mask(y, x)) ? cc_array(y, x) : 0.0;
                            corr_out[idx] = cc;
                        }
                    }
                };
                
                // Extract real correlation data for each frame
                for (size_t fi=0; fi<dic1.disps.size(); ++fi) {
                    std::vector<double> corr1, corr2;
                    extract_correlation(dic1.disps[fi], corr1, indexLUT, W, H);
                    extract_correlation(dic2.disps[fi], corr2, indexLUT, W, H);
                    corrCam1.push_back(corr1);
                    corrCam2.push_back(corr2);
                    if (fi == 0 && !corr1.empty()) {
                        double avg1 = std::accumulate(corr1.begin(), corr1.end(), 0.0) / corr1.size();
                        double avg2 = std::accumulate(corr2.begin(), corr2.end(), 0.0) / corr2.size();
                        std::cout << "  Using real correlation data - Frame 1: Cam1 avg=" 
                                  << std::fixed << std::setprecision(3) << avg1 
                                  << ", Cam2 avg=" << avg2 << std::endl;
                    }
                }

                // Lambda to extract cam2 points mapped through matching displacement
                // MATLAB step2_dic_finish equivalent: bilinear interpolation of cam2 displacements
                // at positions offset by matching displacement, then add matching displacement
                auto extract_points2D_cam2_mapped = [](const ncorr::Disp2D& disp_cam2,
                                                       const ncorr::Disp2D& disp_matching,
                                                       std::vector<double>& pts_xy,
                                                       std::vector<int>& indexLUT, int W, int H) {
                    const auto& Au2 = disp_cam2.get_u().get_array();
                    const auto& Av2 = disp_cam2.get_v().get_array();
                    const auto& Au12 = disp_matching.get_u().get_array();
                    const auto& Av12 = disp_matching.get_v().get_array();
                    int sf = disp_cam2.get_scalefactor();
                    int H2 = Au2.height(), W2 = Au2.width();
                    
                    pts_xy.clear();
                    pts_xy.reserve(std::count_if(indexLUT.begin(), indexLUT.end(), [](int v){return v>=0;}) * 2);
                    
                    for (int y = 0; y < H; ++y) {
                        for (int x = 0; x < W; ++x) {
                            int idx = indexLUT[y * W + x];
                            if (idx < 0) continue;
                            
                            // Matching displacement at cam1 grid position (y,x)
                            double u12 = Au12(y, x);  // x-displacement cam1→cam2 (full-res pixels)
                            double v12 = Av12(y, x);  // y-displacement cam1→cam2 (full-res pixels)
                            
                            // Mapped position in cam2's reduced grid
                            double cam2_rx = static_cast<double>(x) + u12 / sf;
                            double cam2_ry = static_cast<double>(y) + v12 / sf;
                            
                            // Bilinear interpolation of cam2 displacement at mapped position
                            int x0 = static_cast<int>(std::floor(cam2_rx));
                            int y0 = static_cast<int>(std::floor(cam2_ry));
                            int x1 = x0 + 1;
                            int y1 = y0 + 1;
                            double fx = cam2_rx - x0;
                            double fy = cam2_ry - y0;
                            
                            double u2_mapped = 0.0, v2_mapped = 0.0;
                            if (x0 >= 0 && y0 >= 0 && x1 < W2 && y1 < H2) {
                                // Standard bilinear interpolation
                                u2_mapped = (1-fx)*(1-fy)*Au2(y0,x0) + fx*(1-fy)*Au2(y0,x1)
                                          + (1-fx)*fy*Au2(y1,x0) + fx*fy*Au2(y1,x1);
                                v2_mapped = (1-fx)*(1-fy)*Av2(y0,x0) + fx*(1-fy)*Av2(y0,x1)
                                          + (1-fx)*fy*Av2(y1,x0) + fx*fy*Av2(y1,x1);
                            } else if (x0 >= 0 && y0 >= 0 && x0 < W2 && y0 < H2) {
                                // Edge case: nearest neighbor
                                int cx = std::min(std::max(static_cast<int>(std::round(cam2_rx)), 0), W2-1);
                                int cy = std::min(std::max(static_cast<int>(std::round(cam2_ry)), 0), H2-1);
                                u2_mapped = Au2(cy, cx);
                                v2_mapped = Av2(cy, cx);
                            }
                            // else: out of bounds, u2/v2 remain 0
                            
                            // Final cam2 pixel position = cam2_ref_pixel + cam2_displacement
                            // cam2_ref_pixel = cam1_pixel + matching_displacement
                            double px = static_cast<double>(x) * sf + u12 + u2_mapped;
                            double py = static_cast<double>(y) * sf + v12 + v2_mapped;
                            
                            pts_xy.resize(std::max((size_t)((idx+1)*2), pts_xy.size()));
                            pts_xy[idx*2+0] = px;
                            pts_xy[idx*2+1] = py;
                        }
                    }
                };
                
                for (size_t fi=0; fi<dic1.disps.size(); ++fi) {
                    const auto& d1 = dic1.disps[fi];
                    const auto& d2 = dic2.disps[fi];
                    std::vector<double> pts1, pts2;
                    extract_points2D(d1, pts1, indexLUT, W, H);
                    if (has_matching) {
                        extract_points2D_cam2_mapped(d2, dic12.disps[0], pts2, indexLUT, W, H);
                    } else {
                        extract_points2D(d2, pts2, indexLUT, W, H);
                    }
                    
                    // Apply distortion removal if enabled (matching MATLAB STEP3 lines 128-161)
                    if (use_distortion_removal) {
                        size_t N = pts1.size() / 2;
                        
                        // Convert to cv::Point2d format for undistortion
                        std::vector<cv::Point2d> pts1_cv, pts2_cv;
                        pts1_cv.reserve(N);
                        pts2_cv.reserve(N);
                        
                        for (size_t k = 0; k < N; ++k) {
                            pts1_cv.emplace_back(pts1[k*2+0], pts1[k*2+1]);
                            pts2_cv.emplace_back(pts2[k*2+0], pts2[k*2+1]);
                        }
                        
                        // Undistort points
                        std::vector<cv::Point2d> pts1_undist, pts2_undist;
                        Utils::undistortPoints(pts1_cv, distortion_cam1, pts1_undist);
                        Utils::undistortPoints(pts2_cv, distortion_cam2, pts2_undist);
                        
                        // Convert back to flat array
                        for (size_t k = 0; k < N; ++k) {
                            pts1[k*2+0] = pts1_undist[k].x;
                            pts1[k*2+1] = pts1_undist[k].y;
                            pts2[k*2+0] = pts2_undist[k].x;
                            pts2[k*2+1] = pts2_undist[k].y;
                        }
                        
                        if (fi == 0) {
                            std::cout << "  ✓ Applied distortion removal to 2D points (frame 1)" << std::endl;
                        }
                    }
                    size_t N = pts1.size()/2;
                    std::vector<double> pts3d; pts3d.resize(N*3, std::numeric_limits<double>::quiet_NaN());
                    for (size_t k=0; k<N; ++k){
                        double x1 = pts1[k*2+0], y1 = pts1[k*2+1];
                        double x2 = pts2[k*2+0], y2 = pts2[k*2+1];
                        if (L1.size()>=11 && L2.size()>=11) {
                            auto X = solve_3d(L1,L2,x1,y1,x2,y2);
                            pts3d[k*3+0]=X[0]; pts3d[k*3+1]=X[1]; pts3d[k*3+2]=X[2];
                        }
                    }
                    // Accumulate points3d for this frame
                    Points3D frame_pts;
                    for (size_t k=0; k<N; ++k) {
                        frame_pts.x.push_back(pts3d[k*3+0]);
                        frame_pts.y.push_back(pts3d[k*3+1]);
                        frame_pts.z.push_back(pts3d[k*3+2]);
                    }
                    pair_result.Points3D.push_back(frame_pts);

                    // Compute combined correlation (max of cam1 and cam2 - worst case)
                    std::vector<double> corr_comb;
                    for (size_t k=0; k<N; ++k) {
                        corr_comb.push_back(std::max(corrCam1[fi][k], corrCam2[fi][k]));
                    }
                    pair_result.corrComb.push_back(corr_comb);
                    
                    // Compute face-based correlation (max of 3 vertices)
                    size_t nFaces = faces.size() / 3;
                    std::vector<double> face_corr;
                    for (size_t iface=0; iface<nFaces; ++iface) {
                        int v0 = faces[iface*3];
                        int v1 = faces[iface*3+1];
                        int v2 = faces[iface*3+2];
                        if (v0 < N && v1 < N && v2 < N) {
                            double max_corr = std::max({corr_comb[v0], corr_comb[v1], corr_comb[v2]});
                            face_corr.push_back(max_corr);
                        } else {
                            face_corr.push_back(std::numeric_limits<double>::quiet_NaN());
                        }
                    }
                    pair_result.FaceCorrComb.push_back(face_corr);
                    
                    // Compute face centroids
                    std::vector<double> face_centroids;
                    for (size_t iface=0; iface<nFaces; ++iface) {
                        int v0 = faces[iface*3];
                        int v1 = faces[iface*3+1];
                        int v2 = faces[iface*3+2];
                        if (v0 < N && v1 < N && v2 < N) {
                            double cx = (pts3d[v0*3+0] + pts3d[v1*3+0] + pts3d[v2*3+0]) / 3.0;
                            double cy = (pts3d[v0*3+1] + pts3d[v1*3+1] + pts3d[v2*3+1]) / 3.0;
                            double cz = (pts3d[v0*3+2] + pts3d[v1*3+2] + pts3d[v2*3+2]) / 3.0;
                            face_centroids.push_back(cx);
                            face_centroids.push_back(cy);
                            face_centroids.push_back(cz);
                        } else {
                            face_centroids.push_back(std::numeric_limits<double>::quiet_NaN());
                            face_centroids.push_back(std::numeric_limits<double>::quiet_NaN());
                            face_centroids.push_back(std::numeric_limits<double>::quiet_NaN());
                        }
                    }
                    pair_result.FaceCentroids.push_back(face_centroids);

                    // Compute displacement from frame 1
                    if (fi==0) P3D_ref = pts3d;
                    std::vector<double> dispvec; dispvec.resize(N*3, std::numeric_limits<double>::quiet_NaN());
                    std::vector<double> dispmgn; dispmgn.resize(N, std::numeric_limits<double>::quiet_NaN());
                    for (size_t k=0;k<N;++k){
                        double dx = pts3d[k*3+0]-P3D_ref[k*3+0];
                        double dy = pts3d[k*3+1]-P3D_ref[k*3+1];
                        double dz = pts3d[k*3+2]-P3D_ref[k*3+2];
                        dispvec[k*3+0]=dx; dispvec[k*3+1]=dy; dispvec[k*3+2]=dz;
                        dispmgn[k]=std::sqrt(dx*dx+dy*dy+dz*dz);
                    }
                    // Accumulate displacement data
                    pair_result.Disp.DispVec.push_back(dispvec);
                    pair_result.Disp.DispMgn.push_back(dispmgn);
                }

                // Finalize pair result structure
                pair_result.Faces = faces;
                // Set distortion info (if available)
                if (use_distortion_removal) {
                    pair_result.distortionModel = {"distortion", "distortion"};  // Placeholder
                    pair_result.distortionPath = {distortion_cam1_path, distortion_cam2_path};
                } else {
                    pair_result.distortionModel = {"none", "none"};
                    pair_result.distortionPath = {"none", "none"};
                }
                
                // Store this pair's result
                all_pairs.push_back(pair_result);
                
                std::cout << "✓ Pair " << pair << " complete: " 
                          << pair_result.Points3D[0].x.size() << " points, "
                          << faces.size() / 3 << " faces" << std::endl;
            }
            
            // Stitch all pairs together
            std::cout << "\n=== Stitching " << all_pairs.size() << " pairs ===" << std::endl;
            DIC3Dcombined stitched;
            if (all_pairs.empty()) {
                std::cerr << "No pairs successfully reconstructed for trial " << trial << std::endl;
                continue;
            } else {
                stitched = stitchPairsSimple(all_pairs);
                
                // Store individual pair results in stitched structure
                stitched.AllPairsResults = all_pairs;
                
                // Load DIC2D pair results from Step D output files
                std::cout << "\n=== Loading DIC2D pair results ===" << std::endl;
                auto d_serializer = cppxdic::DataSerializer::create(config_.data_format);
                std::string output_dir = config_.dic_path + "/" + config_.subject_id + "/" + config_.material;
                for (int pair = 1; pair <= config_.num_pair; ++pair) {
                    int cam_1 = (pair - 1) * 2 + 1;
                    int cam_2 = (pair - 1) * 2 + 2;
                    
                    std::string dic2d_file = output_dir + "/myDIC2DpairResults_C_" + 
                        std::to_string(cam_1) + "_C_" + std::to_string(cam_2) + d_serializer->extension();
                    
                    if (std::filesystem::exists(dic2d_file)) {
                        DIC2DPairResults dic2d_result;
                        if (d_serializer->loadDIC2DPairResults(dic2d_file, dic2d_result)) {
                            dic2d_info.push_back(dic2d_result);
                            std::cout << "  ✓ Loaded DIC2D data for pair " << pair << std::endl;
                        } else {
                            std::cerr << "  ! Failed to load DIC2D file, using placeholder: " << dic2d_file << std::endl;
                            dic2d_result.nCamRef = cam_1;
                            dic2d_result.nCamDef = cam_2;
                            dic2d_result.nImages = all_pairs[pair-1].Points3D.size();
                            dic2d_info.push_back(dic2d_result);
                        }
                    } else {
                        std::cout << "  ! DIC2D file not found (creating placeholder): " << dic2d_file << std::endl;
                        DIC2DPairResults dic2d_result;
                        dic2d_result.nCamRef = cam_1;
                        dic2d_result.nCamDef = cam_2;
                        dic2d_result.nImages = all_pairs[pair-1].Points3D.size();
                        dic2d_info.push_back(dic2d_result);
                    }
                }
                
                // Store DIC2D info in stitched structure
                stitched.DIC2Dinfo = dic2d_info;
                std::cout << "  Stored " << stitched.DIC2Dinfo.size() << " DIC2D pair results" << std::endl;
            }
            
            // Save DIC3Dcombined using configured format
            {
                auto e_serializer = cppxdic::DataSerializer::create(config_.data_format);
                std::string e_out = config_.dic_path + "/" + config_.subject_id + "/" + config_.material
                                  + "/DIC3Dcombined_" + std::to_string(config_.num_pair) 
                                  + "Pairs_stitched" + e_serializer->extension();
                
                if (std::filesystem::exists(e_out)) {
                    std::cout << "Checkpoint found: " << e_out << " (skipping)" << std::endl;
                } else {
                    std::cout << "Writing DIC3Dcombined to: " << e_out << std::endl;
                    if (!e_serializer->saveDIC3Dcombined(e_out, stitched)) {
                        std::cerr << "Failed to write DIC3Dcombined" << std::endl;
                    } else {
                        std::cout << "✓ Saved DIC3Dcombined: " << e_out << std::endl;
                    }
                }
            }
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error in 3D Reconstruction: " << e.what() << std::endl;
        return false;
    }
}

bool DicAnalysis::setupNcorrAnalysis(const std::vector<std::string>& images,
                                    const std::string& roi_mask_image_path,
                                    DIC_analysis_input& dic_input) {
    try {
        if (images.size() < 2) {
            std::cerr << "Need at least 2 images for DIC analysis" << std::endl;
            return false;
        }

        std::vector<Image2D> ncorr_images;
        for (const auto& img_path : images) {
            ncorr_images.emplace_back(img_path);
        }

        // Build ROI from provided mask image (non-zero pixels inside ROI)
        Image2D roi_mask(roi_mask_image_path);
        ROI2D roi(roi_mask.get_gs() > 0.5);

        dic_input = DIC_analysis_input(
            ncorr_images,
            roi,
            3,
            INTERP::QUINTIC_BSPLINE_PRECOMPUTE,
            SUBREGION::CIRCLE,
            config_.subregion_radius,
            4,
            DIC_analysis_config::NO_UPDATE,
            config_.debug_mode
        );

        return true;

    } catch (const std::exception& e) {
        std::cerr << "Error setting up ncorr analysis with ROI mask: " << e.what() << std::endl;
        return false;
    }
}

bool DicAnalysis::run() {
    // Search for trial targets (equivalent to search_trial2target)
    std::vector<int> trial_target = {7};//{7, 12, 25};//#searchTrialTarget();
    
    std::cout << "Trial target set: [";
    for (size_t i = 0; i < trial_target.size(); ++i) {
        std::cout << trial_target[i];
        if (i < trial_target.size() - 1) std::cout << ", ";
    }
    std::cout << "]" << std::endl;
    
    // Create serializer for format-aware checkpoint checks
    auto serializer = cppxdic::DataSerializer::create(config_.data_format);
    std::string ext = serializer->extension();
    
    // Helper lambda: Check if 2D DIC outputs exist for all trials/pairs
    auto check_2d_outputs_exist = [&]() -> bool {
        for (int trial : trial_target) {
            for (int pair = 1; pair <= config_.num_pair; ++pair) {
                int cam1, cam2;
                Utils::getCamerasForPair(pair, cam1, cam2);

                std::ostringstream path;
                path << config_.dic_path << "/" << config_.subject_id << "/" 
                     << config_.material << "/"
                     << std::setfill('0') << std::setw(3) << trial << "/"
                     << config_.phase_id << "/"
                     << "myDIC2DpairResults_C_" << cam1 << "_C_" << cam2 << ext;
                if (!std::filesystem::exists(path.str())) {
                    return false;
                }
            }
        }
        return true;
    };
    
    // Helper lambda: Check if 3D reconstruction outputs exist
    auto check_3d_outputs_exist = [&]() -> bool {
        std::string dic3d_file = config_.dic_path + "/" + config_.subject_id + "/" 
                               + config_.material + "/DIC3Dcombined_" 
                               + std::to_string(config_.num_pair) + "Pairs_stitched" + ext;
        return std::filesystem::exists(dic3d_file);
    };
    
    // Helper lambda: Check if deformation analysis outputs exist
    auto check_deformation_outputs_exist = [&]() -> bool {
        std::string pp_file = config_.dic_path + "/" + config_.subject_id + "/" 
                            + config_.material + "/DIC3DPPresults_" 
                            + std::to_string(config_.num_pair) + "Pairs_cum_" 
                            + config_.fileversion + ext;
        return std::filesystem::exists(pp_file);
    };
    
    // STEP D: 2D-DIC
    bool step_d_complete = check_2d_outputs_exist();
    if (step_d_complete) {
        std::cout << "\n=== STEP D: 2D-DIC ===" << std::endl;
        std::cout << "✓ Checkpoint detected: All 2D DIC output files exist" << std::endl;
        std::cout << "  Skipping 2D analysis (use existing results)" << std::endl;
    } else {
        std::cout << "\n=== STEP D: 2D-DIC ===" << std::endl;
        std::cout << "Running 2D DIC analysis..." << std::endl;
        auto start = std::chrono::high_resolution_clock::now();
        bool success = dic2DAnalysis(trial_target);
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        if (!success) {
            std::cerr << "2D DIC Analysis failed!" << std::endl;
            return false;
        }
        
        std::cout << "✓ DIC 2D Analysis done in " << duration.count() / 1000.0 << " s" << std::endl;
    }
    
    // STEP E: 3D Reconstruction
    bool step_e_complete = check_3d_outputs_exist();
    if (step_e_complete) {
        std::cout << "\n=== STEP E: 3D Reconstruction ===" << std::endl;
        std::cout << "✓ Checkpoint detected: All 3D reconstruction output files exist" << std::endl;
        std::cout << "  Skipping 3D reconstruction (use existing results)" << std::endl;
    } else {
        if (!step_d_complete) {
            std::cout << "\n=== STEP E: 3D Reconstruction ===" << std::endl;
        }
        std::cout << "Running 3D reconstruction..." << std::endl;
        auto start = std::chrono::high_resolution_clock::now();
        bool success = dic3DReconstruction(trial_target);
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        if (!success) {
            std::cerr << "3D Reconstruction failed!" << std::endl;
            return false;
        }
        
        std::cout << "✓ DIC 3D Reconstruction done in " << duration.count() / 1000.0 << " s" << std::endl;
    }
    
    // STEP F: Deformation analysis
    bool step_f_complete = check_deformation_outputs_exist();
    if (step_f_complete) {
        std::cout << "\n=== STEP F: Deformation Analysis ===" << std::endl;
        std::cout << "✓ Checkpoint detected: Deformation analysis output exists" << std::endl;
        std::cout << "  Skipping deformation analysis (use existing results)" << std::endl;
    } else {
        if (!step_e_complete) {
            std::cout << "\n=== STEP F: Deformation Analysis ===" << std::endl;
        }
        std::cout << "Running deformation analysis..." << std::endl;
        auto start = std::chrono::high_resolution_clock::now();
        bool success = dicDeformationAnalysis(trial_target);
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        
        if (!success) {
            std::cerr << "Deformation Analysis failed!" << std::endl;
            return false;
        }
        
        std::cout << "✓ DIC Deformation Analysis done in " << duration.count() / 1000.0 << " s" << std::endl;
    }
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "✓ All DIC analysis steps complete!" << std::endl;
    std::cout << "========================================" << std::endl;
    
    return true;
}

std::vector<int> DicAnalysis::searchTrialTarget() {
    std::vector<int> trials;

    // Build protocol directory path
    std::string protocol_dir = config_.data_path + "/rawdata/" + config_.subject_id +
                               "/speckles/" + config_.material + "/protocol/";

    // Find protocol .mat file
    auto protos = Utils::findFiles(protocol_dir, "*.mat");
    if (protos.empty()) {
        std::cerr << "Protocol file not found in: " << protocol_dir << std::endl;
        // Fallback to reference + next
        trials.push_back(config_.ref_trial_id);
        trials.push_back(config_.ref_trial_id + 1);
        return trials;
    }

    std::string proto_file = protos.front();

    // Open MAT file
    mat_t *matfp = Mat_Open(proto_file.c_str(), MAT_ACC_RDONLY);
    if (!matfp) {
        std::cerr << "Failed to open MAT file: " << proto_file << std::endl;
        trials.push_back(config_.ref_trial_id);
        trials.push_back(config_.ref_trial_id + 1);
        return trials;
    }

    // Load 'cond' struct
    matvar_t *cond = Mat_VarRead(matfp, "cond");
    if (!cond || cond->class_type != MAT_C_STRUCT) {
        if (cond) Mat_VarFree(cond);
        Mat_Close(matfp);
        std::cerr << "Variable 'cond' not found or not a struct in: " << proto_file << std::endl;
        trials.push_back(config_.ref_trial_id);
        trials.push_back(config_.ref_trial_id + 1);
        return trials;
    }

    // Read titles (cell array of strings)
    matvar_t *titles = Mat_VarGetStructFieldByName(cond, "titles", 0);
    matvar_t *table = Mat_VarGetStructFieldByName(cond, "table", 0);
    if (!titles || titles->class_type != MAT_C_CELL || !table || table->class_type != MAT_C_CELL) {
        if (cond) Mat_VarFree(cond);
        Mat_Close(matfp);
        std::cerr << "Fields 'titles' or 'table' missing or of wrong type in 'cond'" << std::endl;
        trials.push_back(config_.ref_trial_id);
        trials.push_back(config_.ref_trial_id + 1);
        return trials;
    }

    // Extract column names
    std::vector<std::string> col_names;
    size_t ncols = titles->dims[1];
    col_names.reserve(ncols);
    for (size_t j = 0; j < ncols; ++j) {
        matvar_t *cell = static_cast<matvar_t **>(titles->data)[j];
        std::string name;
        if (cell && cell->class_type == MAT_C_CHAR && cell->data) {
            size_t len = cell->nbytes / cell->data_size;
            name.assign(static_cast<const char *>(cell->data), len);
            // Titles might have trailing nulls; trim
            while (!name.empty() && (name.back() == '\0' || name.back() == ' ')) name.pop_back();
        }
        col_names.push_back(name);
    }

    // Identify indices for 'dir', 'nf', 'spddxl'
    auto find_col = [&](const std::string &key) -> int {
        for (size_t j = 0; j < col_names.size(); ++j) {
            if (col_names[j] == key) return static_cast<int>(j);
        }
        return -1;
    };
    int dir_idx = find_col("dir");
    int nf_idx = find_col("nf");
    int spd_idx = find_col("spddxl");
    if (dir_idx < 0 || nf_idx < 0 || spd_idx < 0) {
        if (cond) Mat_VarFree(cond);
        Mat_Close(matfp);
        std::cerr << "Required columns not found in titles (need 'dir','nf','spddxl')" << std::endl;
        trials.push_back(config_.ref_trial_id);
        trials.push_back(config_.ref_trial_id + 1);
        return trials;
    }

    // Table dimensions: Ntrial x Ncond
    size_t ntrial = table->dims[0];
    size_t ncond = table->dims[1];

    // Pre-extract columns from cell table
    auto cell_at = [&](size_t i, size_t j) -> matvar_t * {
        size_t idx = i + j * ntrial; // column-major
        return static_cast<matvar_t **>(table->data)[idx];
    };

    std::vector<std::string> dircol(ntrial);
    std::vector<double> nfcol(ntrial, std::numeric_limits<double>::quiet_NaN());
    std::vector<double> spdcol(ntrial, std::numeric_limits<double>::quiet_NaN());

    for (size_t i = 0; i < ntrial; ++i) {
        // dir as string
        if (dir_idx < static_cast<int>(ncond)) {
            matvar_t *c = cell_at(i, static_cast<size_t>(dir_idx));
            if (c && c->class_type == MAT_C_CHAR && c->data) {
                size_t len = c->nbytes / c->data_size;
                std::string s(static_cast<const char *>(c->data), len);
                while (!s.empty() && (s.back() == '\0' || s.back() == ' ')) s.pop_back();
                dircol[i] = s;
            }
        }
        // nf numeric
        if (nf_idx < static_cast<int>(ncond)) {
            matvar_t *c = cell_at(i, static_cast<size_t>(nf_idx));
            if (c && c->data) {
                if (c->class_type == MAT_C_DOUBLE) {
                    nfcol[i] = static_cast<const double *>(c->data)[0];
                } else if (c->class_type == MAT_C_SINGLE) {
                    nfcol[i] = static_cast<const float *>(c->data)[0];
                } else if (c->class_type == MAT_C_INT32) {
                    nfcol[i] = static_cast<const int32_t *>(c->data)[0];
                }
            }
        }
        // spddxl numeric
        if (spd_idx < static_cast<int>(ncond)) {
            matvar_t *c = cell_at(i, static_cast<size_t>(spd_idx));
            if (c && c->data) {
                if (c->class_type == MAT_C_DOUBLE) {
                    spdcol[i] = static_cast<const double *>(c->data)[0];
                } else if (c->class_type == MAT_C_SINGLE) {
                    spdcol[i] = static_cast<const float *>(c->data)[0];
                } else if (c->class_type == MAT_C_INT32) {
                    spdcol[i] = static_cast<const int32_t *>(c->data)[0];
                }
            }
        }
    }

    // Subject numeric id
    int subj_num = 0;
    {
        // extract digits from subject_id
        for (char ch : config_.subject_id) {
            if (std::isdigit(static_cast<unsigned char>(ch))) {
                subj_num = subj_num * 10 + (ch - '0');
            }
        }
    }

    // Correction if subject < 8
    if (subj_num < 8 && ntrial > 1) {
        size_t half = ntrial / 2;
        for (size_t i = 0; i < ntrial; ++i) {
            spdcol[i] = (i < half) ? 0.04 : 0.08;
        }
    }

    // Build trial indices 1..Ntrial (Matlab-style) based on filters

    bool is_loading = (config_.phase_id == "loading");
    std::vector<int> trialnum;
    trialnum.reserve(ntrial);
    for (size_t i = 0; i < ntrial; ++i) trialnum.push_back(static_cast<int>(i + 1));

    for (size_t ii = 0; ii < config_.nfcond_set.size(); ++ii) {
        int nf_set = config_.nfcond_set[ii];
        for (size_t jj = 0; jj < config_.spddxlcond_set.size(); ++jj) {
            double spd_set = config_.spddxlcond_set[jj];

            for (size_t i = 0; i < ntrial; ++i) {
                bool pass = true;
                if (is_loading) {
                    bool dir_ok = (dircol[i] == "Ubnf" || dircol[i] == "Rbnf");
                    bool nf_ok = std::fabs(nfcol[i] - nf_set) < 1e-6;
                    bool spd_ok = std::fabs(spdcol[i] - spd_set) < 1e-9;
                    pass = dir_ok && nf_ok && spd_ok;
                }
                if (pass) trials.push_back(trialnum[i]);
            }
        }
    }

    // Clean up
    if (cond) Mat_VarFree(cond);
    Mat_Close(matfp);

    return trials;
}

bool DicAnalysis::dic2DAnalysis(const std::vector<int>& trial_target) {
    std::cout << "Starting 2D DIC Analysis (using StepDWorkflow)..." << std::endl;
    
    try {
        // Create workflow instance
        StepDWorkflow workflow(config_);
        
        // Process each trial and stereo pair
        for (int trial : trial_target) {
            for (int pair = 1; pair <= config_.num_pair; ++pair) {
                // Format trial as 3-digit string (e.g., "005")
                std::ostringstream trial_str;
                trial_str << std::setw(3) << std::setfill('0') << trial;
                
                std::cout << "\n========================================" << std::endl;
                std::cout << "Processing Trial " << trial << ", Pair " << pair << std::endl;
                std::cout << "========================================" << std::endl;
                
                // Execute workflow
                auto [outputPath, pairOrder, pairForced] = workflow.execute(trial_str.str(), pair);
                
                if (outputPath.empty()) {
                    std::cerr << "Workflow failed for trial " << trial << ", pair " << pair << std::endl;
                    return false;
                }
                
                std::cout << "✓ Trial " << trial << ", pair " << pair << " completed successfully." << std::endl;
                std::cout << "  Output path: " << outputPath << std::endl;
                std::cout << "  Pair order: [" << pairOrder[0] << ", " << pairOrder[1] << "]" << std::endl;
                std::cout << "  Pair forced: " << (pairForced ? "true" : "false") << std::endl;
            }
        }
        
        std::cout << "\n✓ All 2D DIC Analysis completed successfully!" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in 2D DIC Analysis: " << e.what() << std::endl;
        return false;
    }
}


std::vector<std::string> DicAnalysis::loadImageSequence(const std::string& trial_path) {
    std::vector<std::string> images;
    
    // Look for common image formats
    std::vector<std::string> extensions = {".png", ".jpg", ".jpeg", ".tiff", ".bmp"};
    
    try {
        if (std::filesystem::exists(trial_path)) {
            for (const auto& entry : std::filesystem::directory_iterator(trial_path)) {
                if (entry.is_regular_file()) {
                    std::string filename = entry.path().string();
                    for (const auto& ext : extensions) {
                        if (filename.size() >= ext.size() && 
                            filename.compare(filename.size() - ext.size(), ext.size(), ext) == 0) {
                            images.push_back(filename);
                            break;
                        }
                    }
                }
            }
        }
        
        // Sort images to ensure proper sequence
        std::sort(images.begin(), images.end());
        
    } catch (const std::exception& e) {
        std::cerr << "Error loading image sequence: " << e.what() << std::endl;
    }
    
    return images;
}

bool DicAnalysis::setupNcorrAnalysis(const std::vector<std::string>& images, 
                                   DIC_analysis_input& dic_input) {
    try {
        if (images.size() < 2) {
            std::cerr << "Need at least 2 images for DIC analysis" << std::endl;
            return false;
        }
        
        // Convert string paths to Image2D objects
        std::vector<Image2D> ncorr_images;
        for (const auto& img_path : images) {
            ncorr_images.emplace_back(img_path);
        }
        
        Image2D first_image(images[0]);
        ROI2D roi(first_image.get_gs() > 0.1);
        
        // Setup DIC input with parameters similar to the ncorr test
        dic_input = DIC_analysis_input(
            ncorr_images,                                    // Images
            roi,                                            // ROI
            3,                                              // scalefactor
            INTERP::QUINTIC_BSPLINE_PRECOMPUTE,            // Interpolation
            SUBREGION::CIRCLE,                             // Subregion shape
            config_.subregion_radius,                      // Subregion radius
            4,                                             // # of threads
            DIC_analysis_config::NO_UPDATE,                // DIC configuration
            config_.debug_mode                             // Debugging enabled/disabled
        );
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Error setting up ncorr analysis: " << e.what() << std::endl;
        return false;
    }
}

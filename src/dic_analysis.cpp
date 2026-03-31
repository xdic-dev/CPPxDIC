/**
 * DIC Analysis implementation for CPPXDIC
 */

#include "dic_analysis.h"
#include "mat_reader.h"
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
#include <algorithm>
#include <array>
#include <numeric>
#include <set>

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
            std::string output_dir = Utils::buildOutputUntilPhaseDir(config_, trial);
            
            // Load DIC3Dcombined from Step E output using configured format
            auto serializer = cppxdic::DataSerializer::create(config_.data_format);
            std::string dic3d_file = Utils::buildDic3DCombinedFilePath(output_dir, config_.num_pair, serializer->extension());
            
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
            std::cout << "  corrComb: " << dic3d.corrComb.size() << " frames"
                      << (dic3d.corrComb.empty() ? "" : " (" + std::to_string(dic3d.corrComb[0].size()) + " per frame)")
                      << std::endl;
            std::cout << "  FaceCorrComb: " << dic3d.FaceCorrComb.size() << " frames" << std::endl;
            std::cout << "  FaceCentroids: " << dic3d.FaceCentroids.size() << " frames" << std::endl;
            std::cout << "  Disp.DispVec: " << dic3d.Disp.DispVec.size() << " frames" << std::endl;
            std::cout << "  Disp.DispMgn: " << dic3d.Disp.DispMgn.size() << " frames" << std::endl;
            
            // Diagnostic: check for NaN/Inf in Points3D and face validity
            {
                size_t inf_pts_total = 0;
                size_t bad_face_total = 0;
                size_t nP = dic3d.Points3D.empty() ? 0 : dic3d.Points3D[0].x.size();
                for (size_t f = 0; f < std::min(dic3d.Points3D.size(), size_t(3)); ++f) {
                    size_t nan_pts = 0;
                    for (size_t i = 0; i < nP; ++i) {
                        if (std::isnan(dic3d.Points3D[f].x[i]) || std::isnan(dic3d.Points3D[f].y[i]) || std::isnan(dic3d.Points3D[f].z[i]))
                            nan_pts++;
                        if (std::isinf(dic3d.Points3D[f].x[i]) || std::isinf(dic3d.Points3D[f].y[i]) || std::isinf(dic3d.Points3D[f].z[i]))
                            inf_pts_total++;
                    }
                    std::cout << "  [diag] Frame " << f << ": " << nan_pts << "/" << nP << " NaN points" << std::endl;
                }
                size_t nF = dic3d.Faces.size() / 3;
                for (size_t i = 0; i < nF; ++i) {
                    int v0 = dic3d.Faces[i*3], v1 = dic3d.Faces[i*3+1], v2 = dic3d.Faces[i*3+2];
                    if (v0 < 0 || v1 < 0 || v2 < 0 || (size_t)v0 >= nP || (size_t)v1 >= nP || (size_t)v2 >= nP)
                        bad_face_total++;
                }
                if (bad_face_total > 0)
                    std::cerr << "  [diag] WARNING: " << bad_face_total << "/" << nF << " faces have out-of-bounds vertex indices!" << std::endl;
                if (inf_pts_total > 0)
                    std::cerr << "  [diag] WARNING: " << inf_pts_total << " Inf values in Points3D!" << std::endl;
            }
            
            // Convert Points3D to Eigen::Vector3d format for deformation computation
            std::cout << "\nConverting data to Eigen format..." << std::endl;
            
            // Reference frame (frame 0)
            std::vector<Eigen::Vector3d> vertices_ref;
            size_t nPoints = dic3d.Points3D[0].x.size();
            size_t nFaces = dic3d.Faces.size() / 3;
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
            
            auto count_nan_points = [](const std::vector<std::vector<Eigen::Vector3d>>& frames) {
                std::vector<size_t> counts;
                counts.reserve(frames.size());
                for (const auto& frame : frames) {
                    size_t nan_count = 0;
                    for (const auto& p : frame) {
                        if (p.hasNaN()) {
                            nan_count++;
                        }
                    }
                    counts.push_back(nan_count);
                }
                return counts;
            };

            auto count_points_with_any_nan = [](const std::vector<std::vector<Eigen::Vector3d>>& frames) {
                if (frames.empty()) {
                    return size_t(0);
                }
                size_t n_points_local = frames.front().size();
                size_t bad_points = 0;
                for (size_t ipt = 0; ipt < n_points_local; ++ipt) {
                    bool any_nan = false;
                    for (const auto& frame : frames) {
                        if (ipt >= frame.size() || frame[ipt].hasNaN()) {
                            any_nan = true;
                            break;
                        }
                    }
                    if (any_nan) {
                        bad_points++;
                    }
                }
                return bad_points;
            };

            auto count_valid_faces = [&](const std::vector<Eigen::Vector3d>& verts) {
                size_t valid_faces = 0;
                size_t total_faces_local = dic3d.Faces.size() / 3;
                for (size_t iface = 0; iface < total_faces_local; ++iface) {
                    int v0 = dic3d.Faces[iface * 3 + 0];
                    int v1 = dic3d.Faces[iface * 3 + 1];
                    int v2 = dic3d.Faces[iface * 3 + 2];
                    if (v0 < 0 || v1 < 0 || v2 < 0) {
                        continue;
                    }
                    if (static_cast<size_t>(v0) >= verts.size() ||
                        static_cast<size_t>(v1) >= verts.size() ||
                        static_cast<size_t>(v2) >= verts.size()) {
                        continue;
                    }
                    if (verts[v0].hasNaN() || verts[v1].hasNaN() || verts[v2].hasNaN()) {
                        continue;
                    }
                    valid_faces++;
                }
                return valid_faces;
            };

            auto compute_max_displacement = [](const std::vector<std::vector<Eigen::Vector3d>>& frames,
                                               const std::vector<Eigen::Vector3d>& ref) {
                double max_disp = 0.0;
                for (const auto& frame : frames) {
                    size_t n_local = std::min(frame.size(), ref.size());
                    for (size_t ipt = 0; ipt < n_local; ++ipt) {
                        if (!frame[ipt].allFinite() || !ref[ipt].allFinite()) {
                            continue;
                        }
                        max_disp = std::max(max_disp, (frame[ipt] - ref[ipt]).norm());
                    }
                }
                return max_disp;
            };

            const size_t raw_any_nan_points = count_points_with_any_nan(vertices_all_frames);
            const auto raw_nan_counts = count_nan_points(vertices_all_frames);
            const double raw_max_disp = compute_max_displacement(vertices_all_frames, vertices_ref);
            std::cout << "  Raw points with any NaN over time: " << raw_any_nan_points
                      << "/" << nPoints << std::endl;
            if (!raw_nan_counts.empty()) {
                std::cout << "  Raw NaN points in frame 0: " << raw_nan_counts[0]
                          << "/" << nPoints << std::endl;
            }
            std::cout << "  Raw valid faces in frame 0: "
                      << count_valid_faces(vertices_all_frames[0])
                      << "/" << nFaces << std::endl;
            std::cout << "  Raw max displacement magnitude: " << raw_max_disp << std::endl;

            // Apply temporal filtering to displacement fields
            bool used_temporal_filtering = false;
            if (config_.step_f_temporal_filtering && vertices_all_frames.size() > 3) {
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
                double freq_filt = config_.step_f_freq_filt;
                double freq_acq = config_.vid_sample_freq;
                auto [filt_x, filt_y, filt_z] = filterTime3D(disp_x, disp_y, disp_z, freq_filt, freq_acq);
                
                // Reconstruct filtered vertex positions
                auto vertices_filtered = vertices_all_frames;
                for (size_t iframe = 0; iframe < nFrames; ++iframe) {
                    for (size_t ipt = 0; ipt < nPoints; ++ipt) {
                        vertices_filtered[iframe][ipt].x() = vertices_ref[ipt].x() + filt_x[ipt][iframe];
                        vertices_filtered[iframe][ipt].y() = vertices_ref[ipt].y() + filt_y[ipt][iframe];
                        vertices_filtered[iframe][ipt].z() = vertices_ref[ipt].z() + filt_z[ipt][iframe];
                    }
                }

                const size_t filtered_any_nan_points = count_points_with_any_nan(vertices_filtered);
                const auto filtered_nan_counts = count_nan_points(vertices_filtered);
                const size_t filtered_valid_faces = count_valid_faces(vertices_filtered[0]);
                const double filtered_max_disp = compute_max_displacement(vertices_filtered, vertices_ref);
                const bool filtered_exploded =
                    !std::isfinite(filtered_max_disp) ||
                    filtered_max_disp > std::max(1.0, raw_max_disp) * 100.0;

                std::cout << "  Filtered points with any NaN over time: " << filtered_any_nan_points
                          << "/" << nPoints << std::endl;
                if (!filtered_nan_counts.empty()) {
                    std::cout << "  Filtered NaN points in frame 0: " << filtered_nan_counts[0]
                              << "/" << nPoints << std::endl;
                }
                std::cout << "  Filtered valid faces in frame 0: "
                          << filtered_valid_faces << "/" << nFaces << std::endl;
                std::cout << "  Filtered max displacement magnitude: " << filtered_max_disp << std::endl;

                // MATLAB's filter only keeps point tracks that are valid for the whole time series.
                // On stitched C++ reconstructions this can erase nearly the entire mesh, leaving
                // TCPE with no valid triangles. The current C++ filter can also become numerically
                // unstable and amplify otherwise small motions into absurd coordinates. In either
                // case we keep the raw geometry instead of feeding corrupted data into Step F.
                if (filtered_valid_faces == 0 || filtered_valid_faces * 20 < nFaces || filtered_exploded) {
                    std::cout << "  Warning: Temporal filtering produced unusable geometry; "
                              << "keeping unfiltered geometry for Step F" << std::endl;
                } else {
                    vertices_all_frames = std::move(vertices_filtered);
                    for (size_t ipt = 0; ipt < nPoints; ++ipt) {
                        vertices_ref[ipt] = vertices_all_frames[0][ipt];
                    }
                    used_temporal_filtering = true;
                    std::cout << "  ✓ Temporal filtering applied (freqFilt=" << freq_filt
                              << " Hz, freqAcq=" << freq_acq << " Hz)" << std::endl;
                }
            } else if (config_.step_f_temporal_filtering) {
                std::cout << "\nSkipping temporal filtering (too few frames: " 
                          << vertices_all_frames.size() << ")" << std::endl;
            } else {
                std::cout << "\nTemporal filtering disabled (step_f_temporal_filtering=false)" << std::endl;
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
            if (used_temporal_filtering) {
                std::cout << "  ✓ Points3D updated with filtered data" << std::endl;
            } else {
                std::cout << "  ✓ Points3D kept from unfiltered geometry" << std::endl;
            }
            
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
            
            for (size_t iframe = 0; iframe < vertices_all_frames.size(); ++iframe) {
                std::vector<double>& centroids = dic3d.FaceCentroids[iframe];
                centroids.resize(nFaces * 3);
                
                size_t nVerts = vertices_all_frames[iframe].size();
                for (size_t iface = 0; iface < nFaces; ++iface) {
                    int v0 = dic3d.Faces[iface * 3 + 0];
                    int v1 = dic3d.Faces[iface * 3 + 1];
                    int v2 = dic3d.Faces[iface * 3 + 2];
                    
                    if (v0 >= 0 && static_cast<size_t>(v0) < nVerts &&
                        v1 >= 0 && static_cast<size_t>(v1) < nVerts &&
                        v2 >= 0 && static_cast<size_t>(v2) < nVerts) {
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
                    } else {
                        centroids[iface * 3 + 0] = std::numeric_limits<double>::quiet_NaN();
                        centroids[iface * 3 + 1] = std::numeric_limits<double>::quiet_NaN();
                        centroids[iface * 3 + 2] = std::numeric_limits<double>::quiet_NaN();
                    }
                }
            }
            std::cout << "  ✓ Face centroids recomputed for " << vertices_all_frames.size() << " frames" << std::endl;
            
            // Recompute face correlation (worst of 3 vertices) (matching MATLAB line 61)
            // MATLAB: DIC3D.FaceCorrComb{ii} = max(DIC3D.corrComb{ii}(F), [], 2);
            std::cout << "\nRecomputing face correlation after filtering..." << std::endl;
            if (!dic3d.corrComb.empty() && dic3d.corrComb.size() >= vertices_all_frames.size()) {
                dic3d.FaceCorrComb.clear();
                dic3d.FaceCorrComb.resize(vertices_all_frames.size());
                
                for (size_t iframe = 0; iframe < vertices_all_frames.size(); ++iframe) {
                    std::vector<double>& face_corr = dic3d.FaceCorrComb[iframe];
                    face_corr.resize(nFaces);
                    
                    const auto& point_corr = dic3d.corrComb[iframe];
                    size_t nCorr = point_corr.size();
                    
                    for (size_t iface = 0; iface < nFaces; ++iface) {
                        int v0 = dic3d.Faces[iface * 3 + 0];
                        int v1 = dic3d.Faces[iface * 3 + 1];
                        int v2 = dic3d.Faces[iface * 3 + 2];
                        
                        if (v0 >= 0 && static_cast<size_t>(v0) < nCorr &&
                            v1 >= 0 && static_cast<size_t>(v1) < nCorr &&
                            v2 >= 0 && static_cast<size_t>(v2) < nCorr) {
                            face_corr[iface] = std::max({point_corr[v0], point_corr[v1], point_corr[v2]});
                        } else {
                            face_corr[iface] = std::numeric_limits<double>::quiet_NaN();
                        }
                    }
                }
                std::cout << "  ✓ Face correlation recomputed for " << vertices_all_frames.size() << " frames" << std::endl;
            } else {
                std::cout << "  ⚠ corrComb not available (empty or frame count mismatch), skipping face correlation" << std::endl;
            }
            
            // Compute RBM if enabled
            std::vector<Utils::RigidTransform> rbm_transforms;
            std::vector<std::vector<Eigen::Vector3d>> vertices_all_frames_ARBM;
            
            if (config_.step_f_compute_rbm) {
                std::cout << "\nComputing rigid body motion (RBM) transformations..." << std::endl;
                rbm_transforms.resize(vertices_all_frames.size());
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
                        auto verts_arbm = Utils::applyRigidTransform(vertices_all_frames[iframe], transform);
                        vertices_all_frames_ARBM.push_back(verts_arbm);
                    } else {
                        std::cerr << "  Warning: RBM computation failed for frame " << iframe << std::endl;
                        vertices_all_frames_ARBM.push_back(vertices_all_frames[iframe]);
                    }
                }
                std::cout << "  ✓ RBM transformations computed for " << vertices_all_frames.size() << " frames" << std::endl;
            } else {
                std::cout << "\nRBM/ARBM computation disabled (step_f_compute_rbm=false)" << std::endl;
            }
            
            // Compute 3D deformation and strain
            std::cout << "\nComputing 3D surface deformation..." << std::endl;
            std::cout << "  Method: Triangular Cosserat Point Elements (TCPE)" << std::endl;
            std::cout << "  Deformation type: Cumulative (reference = frame 1)" << std::endl;
            
            FrameDeformationResult deform_result = computeTriSurfaceDeformation(
                dic3d.Faces,
                vertices_ref,
                vertices_all_frames,
                true  // cumulative: use frame 1 as reference for all frames
            );
            
            std::cout << "  ✓ Deformation computation complete" << std::endl;
            
            // Diagnostic: report deformation result quality for first few frames
            {
                for (size_t iframe = 0; iframe < std::min(deform_result.n_frames, size_t(3)); ++iframe) {
                    const auto& fr = deform_result.frames[iframe];
                    size_t nan_F = 0, nan_E = 0, nan_J = 0, inf_count = 0, large_count = 0;
                    for (size_t i = 0; i < fr.Fmat.size(); ++i) {
                        if (fr.Fmat[i].array().isNaN().any()) nan_F++;
                        if (fr.Emat.size() > i && fr.Emat[i].array().isNaN().any()) nan_E++;
                        if (fr.J.size() > i && std::isnan(fr.J[i])) nan_J++;
                        if (fr.J.size() > i && std::isinf(fr.J[i])) inf_count++;
                        if (fr.Emgn.size() > i && std::isfinite(fr.Emgn[i]) && std::abs(fr.Emgn[i]) > 1.0)
                            large_count++;
                    }
                    std::cout << "  [diag] Deform frame " << iframe << ": "
                              << "NaN_F=" << nan_F << " NaN_E=" << nan_E << " NaN_J=" << nan_J
                              << " Inf=" << inf_count << " |Emgn|>1=" << large_count
                              << " / " << fr.Fmat.size() << " faces" << std::endl;
                }
            }
            
            FrameDeformationResult deform_result_ARBM;
            if (config_.step_f_compute_rbm) {
                std::cout << "\nComputing 3D surface deformation (after RBM removal)..." << std::endl;
                deform_result_ARBM = computeTriSurfaceDeformation(
                    dic3d.Faces,
                    vertices_all_frames_ARBM[0],
                    vertices_all_frames_ARBM,
                    true  // cumulative
                );
                std::cout << "  ✓ Deformation computation complete (ARBM)" << std::endl;
            }
            
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
            
            std::cout << "  Populated deformation data for " << ppresults.n_frames << " frames" << std::endl;
            
            if (config_.step_f_compute_rbm) {
                // Populate ARBM deformation data (after rigid body motion removal)
                std::cout << "  Populating ARBM deformation data..." << std::endl;
                ppresults.Deform_ARBM.F.resize(deform_result_ARBM.n_frames);
                ppresults.Deform_ARBM.strain.resize(deform_result_ARBM.n_frames);
                ppresults.Deform_ARBM.princStrain.resize(deform_result_ARBM.n_frames);
                ppresults.Deform_ARBM.maxShearStrain.resize(deform_result_ARBM.n_frames);
                
                for (size_t iframe = 0; iframe < deform_result_ARBM.n_frames; ++iframe) {
                    const auto& frame = deform_result_ARBM.frames[iframe];
                    
                    DeformGradient& F = ppresults.Deform_ARBM.F[iframe];
                    F.F11.resize(nFaces); F.F12.resize(nFaces); F.F13.resize(nFaces);
                    F.F21.resize(nFaces); F.F22.resize(nFaces); F.F23.resize(nFaces);
                    F.F31.resize(nFaces); F.F32.resize(nFaces); F.F33.resize(nFaces);
                    
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
                    
                    StrainTensor& E = ppresults.Deform_ARBM.strain[iframe];
                    E.E11.resize(nFaces); E.E12.resize(nFaces); E.E13.resize(nFaces);
                    E.E21.resize(nFaces); E.E22.resize(nFaces); E.E23.resize(nFaces);
                    E.E31.resize(nFaces); E.E32.resize(nFaces); E.E33.resize(nFaces);
                    
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
                    
                    std::vector<double>& princStrain = ppresults.Deform_ARBM.princStrain[iframe];
                    princStrain.resize(nFaces * 2);
                    for (size_t iface = 0; iface < nFaces; ++iface) {
                        princStrain[iface * 2 + 0] = frame.Epc1[iface];
                        princStrain[iface * 2 + 1] = frame.Epc2[iface];
                    }
                    ppresults.Deform_ARBM.maxShearStrain[iframe] = frame.EShearMax;
                }
                
                // Populate RBM transformation matrices
                ppresults.RBM.RotMat.resize(rbm_transforms.size());
                ppresults.RBM.TransVec.resize(rbm_transforms.size());
                for (size_t iframe = 0; iframe < rbm_transforms.size(); ++iframe) {
                    const auto& transform = rbm_transforms[iframe];
                    ppresults.RBM.RotMat[iframe].resize(9);
                    for (int i = 0; i < 3; ++i)
                        for (int j = 0; j < 3; ++j)
                            ppresults.RBM.RotMat[iframe][i * 3 + j] = transform.R(i, j);
                    ppresults.RBM.TransVec[iframe] = {transform.t(0), transform.t(1), transform.t(2)};
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
                
                std::cout << "  ✓ Populated all deformation data (with RBM and ARBM)" << std::endl;
            }
            
            // Save DIC3DPPresults using configured format
            {
                auto pp_serializer = cppxdic::DataSerializer::create(config_.data_format);
                std::string ppout = Utils::buildDic3DPPresultsFilePath(output_dir, config_.num_pair, config_.fileversion, pp_serializer->extension());
                
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
            
                    // Create viz directory if needed
                    std::filesystem::create_directories(output_dir + "viz");

                    // Export visualization data
                    auto viz_path = Utils::buildVizPath(output_dir, "trial.txt");
                    viz.exportData(vis_data, viz_path);
                    
                    // Generate summary statistics
                    auto stats_path = Utils::buildVizPath(output_dir, "trial_summary.txt");
                    viz.generateSummaryStats(ppresults, stats_path);
                    
                    // Generate videos if enabled
                    if (config_.generate_videos) {
                        std::cout << "\n--- Generating Videos ---" << std::endl;
                        for (const auto& field : config_.plotopt) {
                            std::string video_path = output_dir + "viz/" + field + "_video.avi";
                            viz.generateVideo(vis_data, video_path, field);
                        }
                        // Also generate displacement magnitude video if not in plotopt
                        if (std::find(config_.plotopt.begin(), config_.plotopt.end(), "DispMgn") == config_.plotopt.end()) {
                            if (vis_data.FaceScalars.count("DispMgn") || !ppresults.Disp.DispMgn.empty()) {
                                std::string video_path = output_dir + "viz/DispMgn_video.avi";
                                viz.generateVideo(vis_data, video_path, "DispMgn");
                            }
                        }
                    }
                    
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

        auto solve_3d = [](const std::vector<double>& L1, const std::vector<double>& L2, double x1, double y1, double x2, double y2){
            Eigen::Matrix<double, 4, 3> M;
            Eigen::Matrix<double, 4, 1> V;

            // Match MATLAB DLT11Reconstruction exactly:
            //   P3D(ii,:) = M \ V
            M <<
                x1 * L1[8]  - L1[0],  x1 * L1[9]  - L1[1],  x1 * L1[10] - L1[2],
                y1 * L1[8]  - L1[4],  y1 * L1[9]  - L1[5],  y1 * L1[10] - L1[6],
                x2 * L2[8]  - L2[0],  x2 * L2[9]  - L2[1],  x2 * L2[10] - L2[2],
                y2 * L2[8]  - L2[4],  y2 * L2[9]  - L2[5],  y2 * L2[10] - L2[6];

            V <<
                L1[3] - x1,
                L1[7] - y1,
                L2[3] - x2,
                L2[7] - y2;

            Eigen::ColPivHouseholderQR<Eigen::Matrix<double, 4, 3>> qr(M);
            if (qr.rank() < 3) {
                double nan = std::numeric_limits<double>::quiet_NaN();
                return std::array<double, 3>{nan, nan, nan};
            }

            Eigen::Matrix<double, 3, 1> X = qr.solve(V);
            return std::array<double, 3>{X[0], X[1], X[2]};
        };

        for (int trial : trial_target) {
            // Collect all individual pair results before stitching
            std::vector<DIC3DpairResults> all_pairs;
            std::vector<DIC2DPairResults> dic2d_info;
            
            // Format trial as 3-digit string (e.g., "005")
            std::ostringstream trial_str;
            trial_str << std::setw(3) << std::setfill('0') << trial;
            
            // ------------------------------------------------------------------
            // Load DLT calibrations for ALL cameras upfront (matches MATLAB step3)
            // MATLAB: DLTstructAllCams{ic} = load(DLTpath{ic}).DLTstructCam
            // ------------------------------------------------------------------
            std::string calib_dir = Utils::buildCalibDir(config_);
            
            // Collect unique camera indices across all pairs
            std::set<int> unique_cams;
            for (int p = 1; p <= config_.num_pair; ++p) {
                int c1, c2;
                Utils::getCamerasForPair(p, c1, c2);
                unique_cams.insert(c1);
                unique_cams.insert(c2);
            }
            
            // Load DLT calibration for each unique camera
            std::map<int, DLTCalibrationData> dlt_all_cams;
            for (int cam_id : unique_cams) {
                // Match MATLAB pattern: DLTstruct_cam_<id>.mat (or *cam_<id>.mat via glob)
                std::string dlt_path;
                if (std::filesystem::exists(calib_dir)) {
                    std::string search_suffix = "cam_" + std::to_string(cam_id) + ".mat";
                    for (const auto& entry : std::filesystem::directory_iterator(calib_dir)) {
                        if (!entry.is_regular_file()) continue;
                        auto name = entry.path().filename().string();
                        if (name.size() >= search_suffix.size() &&
                            name.compare(name.size() - search_suffix.size(), search_suffix.size(), search_suffix) == 0) {
                            dlt_path = entry.path().string();
                            break;
                        }
                    }
                }
                
                if (dlt_path.empty()) {
                    std::cerr << "ERROR - DLT calibration file not found for camera " << cam_id
                              << " in " << calib_dir << std::endl;
                    continue;
                }
                
                DLTCalibrationData calib;
                if (cppxdic::MatReader::loadDLTCalibration(dlt_path, calib)) {
                    dlt_all_cams[cam_id] = std::move(calib);
                } else {
                    std::cerr << "ERROR - Failed to load DLT calibration for camera " << cam_id << std::endl;
                }
            }
            
            if (dlt_all_cams.empty()) {
                std::cerr << "ERROR - No DLT calibrations loaded. Cannot proceed with 3D reconstruction." << std::endl;
                continue;
            }
            std::cout << "INFO - Loaded DLT calibrations for " << dlt_all_cams.size() << " cameras" << std::endl;
            
            // ------------------------------------------------------------------
            // Load distortion parameters for all cameras (if available)
            // ------------------------------------------------------------------
            std::map<int, Utils::CameraParameters> distortion_all_cams;
            std::map<int, std::string> distortion_paths;
            if (std::filesystem::exists(calib_dir)) {
                for (int cam_id : unique_cams) {
                    std::string search_str = "cam_" + std::to_string(cam_id);
                    for (const auto& entry : std::filesystem::directory_iterator(calib_dir)) {
                        if (!entry.is_regular_file()) continue;
                        auto name = entry.path().filename().string();
                        std::string lower = name;
                        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                        if (lower.find("cameracbparameters") != std::string::npos &&
                            lower.find(search_str) != std::string::npos) {
                            Utils::CameraParameters params;
                            if (Utils::loadCameraParameters(entry.path().string(), params)) {
                                distortion_all_cams[cam_id] = params;
                                distortion_paths[cam_id] = entry.path().string();
                            }
                            break;
                        }
                    }
                }
            }
            bool use_distortion_removal = config_.step_e_distortion_removal;
            if (use_distortion_removal) {
                std::cout << "INFO - Distortion removal enabled for all " << unique_cams.size() << " cameras" << std::endl;
                
                // ------------------------------------------------------------------
                // Recalculate DLT parameters from undistorted calibration centroids
                // Matches MATLAB step3 lines 84-98:
                //   P2Dtemp = DLTstructAllCams{ic}.imageCentroids;
                //   [P2Dtemp] = undistortPoints(P2Dtemp, distortionPar{ic});
                //   C3Dtemp = reshape(C3Dtrue(:,columns,:), nRows*nCols, 3);
                //   L = DLT11Calibration(P2Dtemp, C3Dtemp);
                //   DLTstructAllCams{ic}.DLTparams = L;
                // ------------------------------------------------------------------
                for (int cam_id : unique_cams) {
                    auto& dlt = dlt_all_cams[cam_id];
                    const auto& dist = distortion_all_cams[cam_id];
                    
                    if (dlt.imageCentroids.empty() || dlt.C3Dtrue.empty() || dlt.columns.empty()) {
                        std::cout << "ERROR - Camera " << cam_id << ": missing calibration data for DLT recalculation, "
                                  << "using original DLT params with per-point undistortion fallback" << std::endl;
                        continue;
                    }
                    
                    // Undistort calibration image centroids (Nx2 row-major)
                    size_t nCentroids = dlt.imageCentroids_rows;
                    std::vector<cv::Point2d> centroids_in(nCentroids);
                    for (size_t i = 0; i < nCentroids; ++i) {
                        centroids_in[i] = cv::Point2d(dlt.imageCentroids[i * 2 + 0],
                                                       dlt.imageCentroids[i * 2 + 1]);
                    }
                    std::vector<cv::Point2d> centroids_undist;
                    Utils::undistortPoints(centroids_in, dist, centroids_undist);
                    
                    // Convert undistorted centroids back to flat array (Nx2)
                    std::vector<double> P2D(nCentroids * 2);
                    for (size_t i = 0; i < nCentroids; ++i) {
                        P2D[i * 2 + 0] = centroids_undist[i].x;
                        P2D[i * 2 + 1] = centroids_undist[i].y;
                    }
                    
                    // Reshape C3Dtrue(:, columns, :) -> (nRows * nSelectedCols) x 3
                    // C3Dtrue is stored row-major as (dim0 x dim1 x 3)
                    // columns contains 1-based column indices used in calibration
                    size_t d0 = dlt.C3Dtrue_dim0;  // nRows
                    size_t nCols = dlt.columns.size();
                    std::vector<double> P3D(d0 * nCols * 3);
                    for (size_t ci = 0; ci < nCols; ++ci) {
                        size_t col = static_cast<size_t>(dlt.columns[ci]) - 1;  // MATLAB 1-based to 0-based
                        for (size_t row = 0; row < d0; ++row) {
                            size_t dst_idx = (ci * d0 + row);  // column-major order matching MATLAB reshape
                            size_t src_idx = (row * dlt.C3Dtrue_dim1 + col);  // row-major C3Dtrue indexing
                            P3D[dst_idx * 3 + 0] = dlt.C3Dtrue[src_idx * 3 + 0];
                            P3D[dst_idx * 3 + 1] = dlt.C3Dtrue[src_idx * 3 + 1];
                            P3D[dst_idx * 3 + 2] = dlt.C3Dtrue[src_idx * 3 + 2];
                        }
                    }
                    
                    size_t nPoints = d0 * nCols;
                    if (nPoints != nCentroids) {
                        std::cerr << "ERROR - Camera " << cam_id << ": centroid count (" << nCentroids
                                  << ") != C3D point count (" << nPoints << "), skipping DLT recalculation" << std::endl;
                        continue;
                    }
                    
                    // Recalculate DLT parameters
                    std::vector<double> L_new;
                    if (Utils::DLT11Calibration(P2D.data(), P3D.data(), nPoints, L_new)) {
                        dlt.DLTparams = L_new;
                        std::cout << "INFO - Camera " << cam_id << ": DLT params recalculated from " 
                                  << nPoints << " undistorted calibration points" << std::endl;
                    } else {
                        std::cerr << "ERROR - Camera " << cam_id << ": DLT11Calibration failed" << std::endl;
                    }
                }
            } else if (!distortion_all_cams.empty()) {
                std::cout << "Distortion parameters found for " << distortion_all_cams.size() 
                          << "/" << unique_cams.size() << " cameras (disabled - need all)" << std::endl;
                use_distortion_removal = false;
            }
            
            // ------------------------------------------------------------------
            // Process each stereo pair
            // ------------------------------------------------------------------
            for (int pair = 1; pair <= config_.num_pair; ++pair) {
                std::cout << "\n=== Processing Pair " << pair << " ===" << std::endl;
                
                // Get camera numbers for this pair
                int cam_1, cam_2;
                Utils::getCamerasForPair(pair, cam_1, cam_2);
                
                // Look up DLT parameters for this pair's cameras
                if (dlt_all_cams.find(cam_1) == dlt_all_cams.end() ||
                    dlt_all_cams.find(cam_2) == dlt_all_cams.end()) {
                    std::cerr << "Missing DLT calibration for pair " << pair 
                              << " (cam " << cam_1 << " or " << cam_2 << "). Skipping." << std::endl;
                    continue;
                }
                const auto& dlt_cam1 = dlt_all_cams[cam_1];
                const auto& dlt_cam2 = dlt_all_cams[cam_2];
                const std::vector<double>& L1 = dlt_cam1.DLTparams;
                const std::vector<double>& L2 = dlt_cam2.DLTparams;
                
                // ---------------------------------------------------------------
                // Load DIC2DpairResults (MATLAB step3 lines 39-47)
                // All 2D point extraction, matching, faces, colors, and correlation
                // are already computed by formatOutput (step2_dic_finish equivalent).
                // ---------------------------------------------------------------
                auto d_serializer = cppxdic::DataSerializer::create(config_.data_format);
                std::string output_dir = Utils::buildOutputUntilPhaseDir(config_, trial);
                std::string dic2d_file = Utils::buildDic2DPairResultsFilePath(output_dir, cam_1, cam_2, d_serializer->extension());
                
                if (!std::filesystem::exists(dic2d_file)) {
                    std::cerr << "ERROR - Missing DIC2DPairResults for trial " << trial << ", pair " << pair << ". Skipping." << std::endl;
                    std::cerr << "  Expected: " << dic2d_file << std::endl;
                    continue;
                }
                
                DIC2DPairResults DIC2D;
                if (!d_serializer->loadDIC2DPairResults(dic2d_file, DIC2D)) {
                    std::cerr << "ERROR - Failed to load DIC2DPairResults: " << dic2d_file << ". Skipping." << std::endl;
                    continue;
                }
                
                if (config_.debug_mode) {
                    std::cout << "[DEBUG] Loaded DIC2DPairResults from: " << dic2d_file << std::endl;
                    std::cout << "[DEBUG]   nImages=" << DIC2D.nImages << ", Points=" << DIC2D.Points.size()
                              << ", Faces=" << DIC2D.Faces.size()/3 << std::endl;
                    std::cout << "[DEBUG]   DLT cam" << cam_1 << ": " << dlt_cam1.filePath << std::endl;
                    std::cout << "[DEBUG]   DLT cam" << cam_2 << ": " << dlt_cam2.filePath << std::endl;
                }

                // Extract information from 2D-DIC results (MATLAB step3 lines 130-134)
                int nImages = DIC2D.nImages;
                const auto& CorCoeff = DIC2D.CorCoeffVec;
                const auto& F = DIC2D.Faces;
                const auto& FC = DIC2D.FaceColors;
                const auto& Points = DIC2D.Points;

                // Create individual pair result structure (MATLAB step3 lines 110-136)
                DIC3DpairResults pair_result;
                pair_result.cameraPairInd = {cam_1, cam_2};
                pair_result.DLTpath = {dlt_cam1.filePath, dlt_cam2.filePath};
                pair_result.DLTparameters = {L1, L2};
                pair_result.Faces = F;
                pair_result.FaceColors = FC;
                
                // Set distortion info (MATLAB step3 lines 138-186)
                if (use_distortion_removal) {
                    pair_result.distortionModel = {"distortion", "distortion"};
                    pair_result.distortionPath = {distortion_paths[cam_1], distortion_paths[cam_2]};
                } else {
                    pair_result.distortionModel = {"none", "none"};
                    pair_result.distortionPath = {"none", "none"};
                }

                size_t nFaces = F.size() / 3;
                std::vector<double> P3D_ref; // frame 1 reference for displacement

                // Loop over images/frames (MATLAB step3 lines 196-221)
                for (int ii = 0; ii < nImages; ++ii) {
                    // Correlated points from 2 cameras (MATLAB: P1=Points{ii}, P2=Points{ii+nImages})
                    const auto& P1 = Points[ii];
                    const auto& P2 = Points[ii + nImages];
                    size_t N = P1.x.size();

                    // Solve the DLT system (MATLAB: P3D = DLT11Reconstruction(P1, P2, L1, L2))
                    std::vector<double> pts3d(N * 3, std::numeric_limits<double>::quiet_NaN());
                    Points3D frame_pts;
                    frame_pts.x.resize(N); frame_pts.y.resize(N); frame_pts.z.resize(N);
                    for (size_t k = 0; k < N; ++k) {
                        double x1 = P1.x[k], y1 = P1.y[k];
                        double x2 = P2.x[k], y2 = P2.y[k];
                        if (!std::isnan(x1) && !std::isnan(y1) && !std::isnan(x2) && !std::isnan(y2)) {
                            auto X = solve_3d(L1, L2, x1, y1, x2, y2);
                            pts3d[k*3+0] = X[0]; pts3d[k*3+1] = X[1]; pts3d[k*3+2] = X[2];
                            frame_pts.x[k] = X[0]; frame_pts.y[k] = X[1]; frame_pts.z[k] = X[2];
                        } else {
                            frame_pts.x[k] = std::numeric_limits<double>::quiet_NaN();
                            frame_pts.y[k] = std::numeric_limits<double>::quiet_NaN();
                            frame_pts.z[k] = std::numeric_limits<double>::quiet_NaN();
                        }
                    }
                    pair_result.Points3D.push_back(frame_pts);

                    // Combined correlation coefficients (MATLAB: corrComb = max([CorCoeff{ii} CorCoeff{ii+nImages}], [], 2))
                    std::vector<double> corr_comb(N, 0.0);
                    if (ii < (int)CorCoeff.size() && (ii + nImages) < (int)CorCoeff.size()) {
                        for (size_t k = 0; k < N; ++k) {
                            double cc1 = (k < CorCoeff[ii].size()) ? CorCoeff[ii][k] : 0.0;
                            double cc2 = (k < CorCoeff[ii + nImages].size()) ? CorCoeff[ii + nImages][k] : 0.0;
                            corr_comb[k] = std::max(cc1, cc2);
                        }
                    }
                    pair_result.corrComb.push_back(corr_comb);

                    // Face correlation coefficient (MATLAB: FaceCorrComb = max(corrComb(F), [], 2))
                    std::vector<double> face_corr(nFaces, std::numeric_limits<double>::quiet_NaN());
                    for (size_t iface = 0; iface < nFaces; ++iface) {
                        int v0 = F[iface*3], v1 = F[iface*3+1], v2 = F[iface*3+2];
                        if (v0 >= 0 && v0 < (int)N && v1 >= 0 && v1 < (int)N && v2 >= 0 && v2 < (int)N) {
                            face_corr[iface] = std::max({corr_comb[v0], corr_comb[v1], corr_comb[v2]});
                        }
                    }
                    pair_result.FaceCorrComb.push_back(face_corr);

                    // Compute face centroids (MATLAB: FaceCentroids(iface,:) = mean(P3D(F(iface,:),:)))
                    std::vector<double> face_centroids(nFaces * 3, std::numeric_limits<double>::quiet_NaN());
                    for (size_t iface = 0; iface < nFaces; ++iface) {
                        int v0 = F[iface*3], v1 = F[iface*3+1], v2 = F[iface*3+2];
                        if (v0 >= 0 && v0 < (int)N && v1 >= 0 && v1 < (int)N && v2 >= 0 && v2 < (int)N) {
                            face_centroids[iface*3+0] = (pts3d[v0*3+0] + pts3d[v1*3+0] + pts3d[v2*3+0]) / 3.0;
                            face_centroids[iface*3+1] = (pts3d[v0*3+1] + pts3d[v1*3+1] + pts3d[v2*3+1]) / 3.0;
                            face_centroids[iface*3+2] = (pts3d[v0*3+2] + pts3d[v1*3+2] + pts3d[v2*3+2]) / 3.0;
                        }
                    }
                    pair_result.FaceCentroids.push_back(face_centroids);

                    // Compute displacements between frames (MATLAB: DispVec = Points3D{ii} - Points3D{1})
                    if (ii == 0) P3D_ref = pts3d;
                    std::vector<double> dispvec(N * 3, std::numeric_limits<double>::quiet_NaN());
                    std::vector<double> dispmgn(N, std::numeric_limits<double>::quiet_NaN());
                    for (size_t k = 0; k < N; ++k) {
                        double dx = pts3d[k*3+0] - P3D_ref[k*3+0];
                        double dy = pts3d[k*3+1] - P3D_ref[k*3+1];
                        double dz = pts3d[k*3+2] - P3D_ref[k*3+2];
                        dispvec[k*3+0] = dx; dispvec[k*3+1] = dy; dispvec[k*3+2] = dz;
                        dispmgn[k] = std::sqrt(dx*dx + dy*dy + dz*dz);
                    }
                    pair_result.Disp.DispVec.push_back(dispvec);
                    pair_result.Disp.DispMgn.push_back(dispmgn);
                }

                // Store this pair's result and DIC2D data
                all_pairs.push_back(pair_result);
                dic2d_info.push_back(std::move(DIC2D));
                
                std::cout << "✓ Pair " << pair << " complete: " 
                          << pair_result.Points3D[0].x.size() << " points, "
                          << nFaces << " faces" << std::endl;
            }
            
            // Stitch all pairs together
            std::cout << "\n=== Stitching " << all_pairs.size() << " pairs ===" << std::endl;
            DIC3Dcombined stitched;
            if (all_pairs.empty()) {
                std::cerr << "ERROR - No pairs successfully reconstructed for trial " << trial << std::endl;
                continue;
            } else {
                std::vector<int> stitch_pair_order;
                bool stitch_pair_forced = false;
                bool have_stitch_metadata = false;
                bool stitch_metadata_mismatch = false;

                for (const auto& dic2d : dic2d_info) {
                    if (dic2d.pairOrder.empty()) {
                        std::cerr << "ERROR - Empty pairOrder in DIC2D for trial " << trial << std::endl;
                        continue;
                    }
                    if (!have_stitch_metadata) {
                        std::cout << "WARNING - Using first pair's stitch metadata for trial " << trial << std::endl;
                        stitch_pair_order = dic2d.pairOrder;
                        stitch_pair_forced = dic2d.pairForced;
                        have_stitch_metadata = true;
                    } else if (dic2d.pairOrder != stitch_pair_order ||
                               dic2d.pairForced != stitch_pair_forced) {
                        std::cerr << "ERROR - Inconsistent DIC2D stitch metadata across pairs for trial "
                                  << trial << ". Skipping trial." << std::endl;
                        stitch_metadata_mismatch = true;
                        break;
                    }
                }

                if (stitch_metadata_mismatch) {
                    std::cerr << "WARNING - Skipping trial " << trial << " due to stitch metadata mismatch" << std::endl;
                    continue;
                }

                if (all_pairs.size() > 1 && all_pairs.size() == static_cast<size_t>(config_.num_pair)) {
                    if (!have_stitch_metadata) {
                        bool protocol_loaded = false;
                        bool protocol_available = false;
                        cppxdic::ProtocolFileData protocol_data;
                        stitch_pair_order.reserve(config_.num_pair);
                        for (int pair = 1; pair <= config_.num_pair; ++pair) {
                            stitch_pair_order.push_back(pair);
                        }

                        try {
                            std::string protocol_dir = Utils::buildProtocolDir(config_, true, true, true, true);
                            auto protocol_files = Utils::findFiles(protocol_dir, "*.mat");
                            if (!protocol_files.empty()) {
                                protocol_loaded = true;
                                protocol_available = MatReader::loadProtocol(protocol_files.front(), protocol_data);
                            }
                        } catch (...) {
                            protocol_available = false;
                        }

                        if (protocol_loaded && protocol_available &&
                            trial > 0 && static_cast<size_t>(trial) <= protocol_data.trials.size()) {
                            const std::string& direction = protocol_data.trials[trial - 1].direction;
                            if (direction == "Ubnf") {
                                stitch_pair_order = {2, 1};
                                stitch_pair_forced = true;
                            } else if (direction == "Rbnf") {
                                stitch_pair_order = {1, 2};
                                stitch_pair_forced = true;
                            } else {
                                stitch_pair_forced = false;
                            }
                            for (int pair = 1; pair <= config_.num_pair; ++pair) {
                                if (std::find(stitch_pair_order.begin(), stitch_pair_order.end(), pair) == stitch_pair_order.end()) {
                                    stitch_pair_order.push_back(pair);
                                }
                            }
                        }
                        std::cout << "  Stitch metadata missing from DIC2D results; using legacy fallback"
                                  << std::endl;
                    }

                    std::cout << "  Stitch order: ";
                    for (size_t i = 0; i < stitch_pair_order.size(); ++i) {
                        if (i > 0) std::cout << ", ";
                        std::cout << stitch_pair_order[i];
                    }
                    std::cout << std::endl;
                    std::cout << "  Pair forced metadata: " << (stitch_pair_forced ? "true" : "false")
                              << std::endl;
                    stitched = stitchPairsGeometric(all_pairs, stitch_pair_order);
                    std::cout << "INFO - Geometric Stitching done!" << std::endl;
                } else {
                    if (all_pairs.size() > 1) {
                        std::cout << "  Falling back to simple append stitching because only "
                                  << all_pairs.size() << "/" << config_.num_pair
                                  << " pairs were reconstructed" << std::endl;
                    }
                    stitched = stitchPairsSimple(all_pairs);
                }
                
                // Store individual pair results in stitched structure
                stitched.AllPairsResults = all_pairs;
                
                // DIC2D pair results already collected during per-pair processing (MATLAB step3 line 268)
                stitched.DIC2Dinfo = dic2d_info;
                std::cout << "  Stored " << stitched.DIC2Dinfo.size() << " DIC2D pair results" << std::endl;
            }
            
            // Save DIC3Dcombined using configured format
            {
                auto e_serializer = cppxdic::DataSerializer::create(config_.data_format);
                auto output_dir = Utils::buildOutputUntilPhaseDir(config_, trial);
                std::string e_out = Utils::buildDic3DCombinedFilePath(output_dir, config_.num_pair, e_serializer->extension());
                
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
            auto output_dir = Utils::buildOutputUntilPhaseDir(config_, trial);
            for (int pair = 1; pair <= config_.num_pair; ++pair) {
                int cam1, cam2;
                Utils::getCamerasForPair(pair, cam1, cam2);

                std::string path = Utils::buildDic2DPairResultsFilePath(output_dir, cam1, cam2, ext);
                if (!std::filesystem::exists(path)) {
                    return false;
                }
            }
        }
        return true;
    };
    
    // Helper lambda: Check if 3D reconstruction outputs exist
    auto check_3d_outputs_exist = [&]() -> bool {
        for (int trial : trial_target) {
            auto output_dir = Utils::buildOutputUntilPhaseDir(config_, trial);
            std::string dic3d_file = Utils::buildDic3DCombinedFilePath(output_dir, config_.num_pair, ext);
            if (!std::filesystem::exists(dic3d_file)) {
                return false;
            }
        }
        return true;
    };
    
    // Helper lambda: Check if deformation analysis outputs exist
    auto check_deformation_outputs_exist = [&]() -> bool {
        for (int trial : trial_target) {
            auto output_dir = Utils::buildOutputUntilPhaseDir(config_, trial);
            std::string pp_file = Utils::buildDic3DPPresultsFilePath(output_dir, config_.num_pair, config_.fileversion, ext);
            if (!std::filesystem::exists(pp_file)) {
                return false;
            }
        }
        return true;
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
    std::string protocol_dir = Utils::buildProtocolDir(config_, true, true, true, true);

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

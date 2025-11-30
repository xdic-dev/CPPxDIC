/**
 * 3D Reconstruction Workflow (Step E) implementation for CPPXDIC
 * Refactored from dic_analysis.cpp dic3DReconstruction
 */

#include "reconstruction3d_workflow.h"
#include "mat_writer.h"
#include "mat_reader.h"
#include "surface_stitching.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <matio.h>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <fstream>
#include <nlohmann/json.hpp>

namespace cppxdic {

// Forward declaration of helper function
static std::vector<double> extractCorrelation(const ncorr::Disp2D& disp,
                                              const std::vector<int>& index_lut,
                                              int width, int height);

// ============================================================================
// Input validation
// ============================================================================

bool Reconstruction3DInputs::PairInput::isValid() const {
    return cam1 > 0 && cam2 > 0 &&
           !dic2d_cache_cam1.empty() && !dic2d_cache_cam2.empty() &&
           std::filesystem::exists(dic2d_cache_cam1) &&
           std::filesystem::exists(dic2d_cache_cam2);
}

bool Reconstruction3DInputs::isValid() const {
    if (trial_id <= 0 || num_pairs <= 0 || pairs.empty()) {
        return false;
    }
    for (const auto& pair : pairs) {
        if (!pair.isValid()) {
            return false;
        }
    }
    return true;
}

// ============================================================================
// Constructor
// ============================================================================

Reconstruction3DWorkflow::Reconstruction3DWorkflow(const Config& config) 
    : config_(config) {
}

// ============================================================================
// Main Entry Points
// ============================================================================

bool Reconstruction3DWorkflow::execute(const std::vector<int>& trial_target) {
    std::cout << "Starting 3D Reconstruction (Step E)..." << std::endl;
    
    try {
        for (int trial : trial_target) {
            Reconstruction3DInputs inputs = buildInputs(trial);
            
            if (!inputs.isValid()) {
                std::cerr << "Invalid inputs for trial " << trial << std::endl;
                continue;
            }
            
            auto outputs = execute(inputs);
            
            if (!outputs.isValid()) {
                std::cerr << "3D Reconstruction failed for trial " << trial << std::endl;
                return false;
            }
            
            std::cout << "✓ Trial " << trial << " complete: " 
                      << outputs.num_points << " points, "
                      << outputs.num_faces << " faces" << std::endl;
        }
        
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in 3D Reconstruction: " << e.what() << std::endl;
        return false;
    }
}

Reconstruction3DOutputs Reconstruction3DWorkflow::execute(const Reconstruction3DInputs& inputs) {
    Reconstruction3DOutputs outputs;
    
    // Check for existing checkpoint
    std::ostringstream binout;
    binout << inputs.output_dir << "/DIC3Dcombined_" << inputs.num_pairs << "Pairs_stitched.bin";
    
    if (hasReconstructionCheckpoint(inputs.output_dir, inputs.num_pairs)) {
        std::cout << "Checkpoint found: " << binout.str() << std::endl;
        outputs.combined_binary_path = binout.str();
        
        // Load to get statistics
        DIC3Dcombined stitched = loadCheckpoint(binout.str());
        outputs.num_frames = stitched.Points3D.size();
        outputs.num_points = stitched.Points3D.empty() ? 0 : stitched.Points3D[0].x.size();
        outputs.num_faces = stitched.Faces.size() / 3;
        outputs.num_pairs_stitched = inputs.num_pairs;
        
        return outputs;
    }
    
    // Process each pair
    std::vector<DIC3DpairResults> all_pairs;
    
    for (size_t pair_idx = 0; pair_idx < inputs.pairs.size(); ++pair_idx) {
        const auto& pair_input = inputs.pairs[pair_idx];
        std::cout << "\n=== Processing Pair " << (pair_idx + 1) << " ===" << std::endl;
        
        DIC3DpairResults pair_result;
        if (!reconstructPair(pair_input, pair_result)) {
            std::cerr << "Failed to reconstruct pair " << (pair_idx + 1) << std::endl;
            continue;
        }
        
        all_pairs.push_back(pair_result);
        
        Reconstruction3DOutputs::PairStats stats;
        stats.pair_index = pair_idx + 1;
        stats.num_points = pair_result.Points3D.empty() ? 0 : pair_result.Points3D[0].x.size();
        stats.num_faces = pair_result.Faces.size() / 3;
        outputs.pair_stats.push_back(stats);
        
        std::cout << "✓ Pair " << (pair_idx + 1) << " complete: " 
                  << stats.num_points << " points, "
                  << stats.num_faces << " faces" << std::endl;
    }
    
    if (all_pairs.empty()) {
        std::cerr << "No pairs successfully reconstructed" << std::endl;
        return outputs;
    }
    
    // Stitch all pairs
    std::cout << "\n=== Stitching " << all_pairs.size() << " pairs ===" << std::endl;
    DIC3Dcombined stitched = stitchPairs(all_pairs);
    stitched.AllPairsResults = all_pairs;
    
    // Save binary output
    try {
        stitched.saveBinary(binout.str());
        outputs.combined_binary_path = binout.str();
        std::cout << "✓ Saved DIC3Dcombined binary: " << binout.str() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Failed to write binary: " << e.what() << std::endl;
    }
    
    // Optionally generate MAT file
    if (config_.generate_mat_files) {
        std::ostringstream matout;
        matout << inputs.output_dir << "/DIC3Dcombined_" << inputs.num_pairs << "Pairs_stitched.mat";
        
        if (!std::filesystem::exists(matout.str())) {
            bool success = MatWriter::write3DCombinedResults(matout.str(), stitched);
            if (success) {
                outputs.combined_mat_path = matout.str();
                std::cout << "✓ Generated MAT file: " << matout.str() << std::endl;
            }
        } else {
            outputs.combined_mat_path = matout.str();
        }
    }
    
    // Set output statistics
    outputs.num_frames = stitched.Points3D.size();
    outputs.num_points = stitched.Points3D.empty() ? 0 : stitched.Points3D[0].x.size();
    outputs.num_faces = stitched.Faces.size() / 3;
    outputs.num_pairs_stitched = all_pairs.size();
    
    return outputs;
}

// ============================================================================
// Milestone Functions
// ============================================================================

std::vector<double> Reconstruction3DWorkflow::loadDLTParameters(const std::string& calib_path) {
    std::vector<double> L;
    
    if (calib_path.empty() || !std::filesystem::exists(calib_path)) {
        return L;
    }
    
    mat_t* fp = Mat_Open(calib_path.c_str(), MAT_ACC_RDONLY);
    if (!fp) {
        return L;
    }
    
    // Expect variable 'DLTstructCam' with field 'DLTparams'
    matvar_t* st = Mat_VarRead(fp, "DLTstructCam");
    if (st && st->class_type == MAT_C_STRUCT) {
        matvar_t* field = Mat_VarGetStructFieldByName(st, "DLTparams", 0);
        if (field && field->data && field->class_type == MAT_C_DOUBLE) {
            size_t n = 1;
            for (int i = 0; i < field->rank; i++) {
                n *= field->dims[i];
            }
            const double* d = static_cast<const double*>(field->data);
            L.assign(d, d + n);
        }
    }
    
    if (st) Mat_VarFree(st);
    Mat_Close(fp);
    
    return L;
}

bool Reconstruction3DWorkflow::loadDistortionParameters(const std::string& distortion_path,
                                                        Utils::CameraParameters& params) {
    if (distortion_path.empty() || !std::filesystem::exists(distortion_path)) {
        return false;
    }
    
    return Utils::loadCameraParameters(distortion_path, params);
}

void Reconstruction3DWorkflow::buildMeshFromROI(const ncorr::ROI2D& roi,
                                                std::vector<int>& faces,
                                                std::vector<int>& index_lut,
                                                int& width, int& height) {
    const auto& mask = roi.get_mask();
    height = static_cast<int>(mask.height());
    width = static_cast<int>(mask.width());
    
    index_lut.assign(height * width, -1);
    int idx = 0;
    
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (mask(y, x)) {
                index_lut[y * width + x] = idx++;
            }
        }
    }
    
    faces.clear();
    for (int y = 0; y < height - 1; ++y) {
        for (int x = 0; x < width - 1; ++x) {
            int a = index_lut[y * width + x];
            int b = index_lut[y * width + (x + 1)];
            int c = index_lut[(y + 1) * width + x];
            int d = index_lut[(y + 1) * width + (x + 1)];
            
            if (a >= 0 && b >= 0 && c >= 0) {
                faces.push_back(a);
                faces.push_back(b);
                faces.push_back(c);
            }
            if (b >= 0 && c >= 0 && d >= 0) {
                faces.push_back(b);
                faces.push_back(d);
                faces.push_back(c);
            }
        }
    }
}

std::vector<double> Reconstruction3DWorkflow::extractPoints2D(const ncorr::Disp2D& disp,
                                                              const std::vector<int>& index_lut,
                                                              int width, int height) {
    const auto& Au = disp.get_u().get_array();
    const auto& Av = disp.get_v().get_array();
    
    size_t num_points = std::count_if(index_lut.begin(), index_lut.end(), 
                                      [](int v) { return v >= 0; });
    std::vector<double> pts_xy(num_points * 2);
    
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int idx = index_lut[y * width + x];
            if (idx < 0) continue;
            
            double u = Au(y, x);
            double v = Av(y, x);
            double px = static_cast<double>(x) + u;
            double py = static_cast<double>(y) + v;
            
            pts_xy[idx * 2 + 0] = px;
            pts_xy[idx * 2 + 1] = py;
        }
    }
    
    return pts_xy;
}

std::array<double, 3> Reconstruction3DWorkflow::triangulatePoint(
    const std::vector<double>& L1,
    const std::vector<double>& L2,
    double x1, double y1,
    double x2, double y2) {
    
    // Build A X = b, A is 4x3, b is 4
    auto rows = [](const std::vector<double>& L, double x, double y) {
        double l1 = L[0], l2 = L[1], l3 = L[2], l4 = L[3];
        double l5 = L[4], l6 = L[5], l7 = L[6], l8 = L[7];
        double l9 = L[8], l10 = L[9], l11 = L[10];
        
        std::array<double, 3> r1{l1 - x * l9, l2 - x * l10, l3 - x * l11};
        std::array<double, 3> r2{l5 - y * l9, l6 - y * l10, l7 - y * l11};
        double b1 = x - l4;
        double b2 = y - l8;
        return std::tuple(r1, r2, b1, b2);
    };
    
    auto [r11, r12, b11, b12] = rows(L1, x1, y1);
    auto [r21, r22, b21, b22] = rows(L2, x2, y2);
    
    // Normal equations A^T A X = A^T b
    double ATA[3][3] = {{0}};
    double ATb[3] = {0};
    
    auto accum = [&](const std::array<double, 3>& r, double b) {
        for (int i = 0; i < 3; ++i) {
            ATb[i] += r[i] * b;
            for (int j = 0; j < 3; ++j) {
                ATA[i][j] += r[i] * r[j];
            }
        }
    };
    
    accum(r11, b11);
    accum(r12, b12);
    accum(r21, b21);
    accum(r22, b22);
    
    // Gaussian elimination
    double A_[3][4] = {
        {ATA[0][0], ATA[0][1], ATA[0][2], ATb[0]},
        {ATA[1][0], ATA[1][1], ATA[1][2], ATb[1]},
        {ATA[2][0], ATA[2][1], ATA[2][2], ATb[2]}
    };
    
    for (int i = 0; i < 3; ++i) {
        int piv = i;
        for (int r = i + 1; r < 3; ++r) {
            if (std::fabs(A_[r][i]) > std::fabs(A_[piv][i])) {
                piv = r;
            }
        }
        if (piv != i) {
            for (int c = 0; c < 4; ++c) {
                std::swap(A_[i][c], A_[piv][c]);
            }
        }
        double diag = A_[i][i];
        if (std::fabs(diag) < 1e-12) continue;
        for (int c = i; c < 4; ++c) {
            A_[i][c] /= diag;
        }
        for (int r = 0; r < 3; ++r) {
            if (r == i) continue;
            double f = A_[r][i];
            for (int c = i; c < 4; ++c) {
                A_[r][c] -= f * A_[i][c];
            }
        }
    }
    
    return {A_[0][3], A_[1][3], A_[2][3]};
}

bool Reconstruction3DWorkflow::reconstructPair(
    const Reconstruction3DInputs::PairInput& pair_input,
    DIC3DpairResults& pair_result) {
    
    // Load DLT parameters
    std::vector<double> L1 = loadDLTParameters(pair_input.calib_cam1);
    std::vector<double> L2 = loadDLTParameters(pair_input.calib_cam2);
    
    if (L1.size() < 11 || L2.size() < 11) {
        std::cerr << "Invalid DLT parameters" << std::endl;
        return false;
    }
    
    // Load distortion parameters (optional)
    Utils::CameraParameters distortion_cam1, distortion_cam2;
    bool use_distortion = false;
    
    if (!pair_input.distortion_cam1.empty() && !pair_input.distortion_cam2.empty()) {
        bool cam1_loaded = loadDistortionParameters(pair_input.distortion_cam1, distortion_cam1);
        bool cam2_loaded = loadDistortionParameters(pair_input.distortion_cam2, distortion_cam2);
        use_distortion = cam1_loaded && cam2_loaded;
        if (use_distortion) {
            std::cout << "  ✓ Distortion removal enabled" << std::endl;
        }
    }
    
    // Load DIC outputs
    auto dic1 = ncorr::DIC_analysis_output::load(pair_input.dic2d_cache_cam1);
    auto dic2 = ncorr::DIC_analysis_output::load(pair_input.dic2d_cache_cam2);
    
    if (dic1.disps.size() != dic2.disps.size()) {
        std::cerr << "Frame count mismatch" << std::endl;
        return false;
    }
    
    // Build mesh from ROI
    std::vector<int> faces, index_lut;
    int W = 0, H = 0;
    buildMeshFromROI(dic1.disps.front().get_roi(), faces, index_lut, W, H);
    
    // Setup pair result
    pair_result.cameraPairInd = {pair_input.cam1, pair_input.cam2};
    pair_result.DLTpath = {pair_input.calib_cam1, pair_input.calib_cam2};
    pair_result.DLTparameters = {L1, L2};
    pair_result.Faces = faces;
    
    if (use_distortion) {
        pair_result.distortionModel = {"distortion", "distortion"};
        pair_result.distortionPath = {pair_input.distortion_cam1, pair_input.distortion_cam2};
    } else {
        pair_result.distortionModel = {"none", "none"};
        pair_result.distortionPath = {"none", "none"};
    }
    
    std::vector<double> P3D_ref;
    
    // Process each frame
    for (size_t fi = 0; fi < dic1.disps.size(); ++fi) {
        std::vector<double> pts1 = extractPoints2D(dic1.disps[fi], index_lut, W, H);
        std::vector<double> pts2 = extractPoints2D(dic2.disps[fi], index_lut, W, H);
        
        // Apply distortion removal if enabled
        if (use_distortion) {
            undistortPoints(pts1, distortion_cam1);
            undistortPoints(pts2, distortion_cam2);
        }
        
        // Triangulate 3D points
        size_t N = pts1.size() / 2;
        std::vector<double> pts3d(N * 3, std::numeric_limits<double>::quiet_NaN());
        
        for (size_t k = 0; k < N; ++k) {
            double x1 = pts1[k * 2 + 0], y1 = pts1[k * 2 + 1];
            double x2 = pts2[k * 2 + 0], y2 = pts2[k * 2 + 1];
            
            auto X = triangulatePoint(L1, L2, x1, y1, x2, y2);
            pts3d[k * 3 + 0] = X[0];
            pts3d[k * 3 + 1] = X[1];
            pts3d[k * 3 + 2] = X[2];
        }
        
        // Store Points3D
        Points3D frame_pts;
        for (size_t k = 0; k < N; ++k) {
            frame_pts.x.push_back(pts3d[k * 3 + 0]);
            frame_pts.y.push_back(pts3d[k * 3 + 1]);
            frame_pts.z.push_back(pts3d[k * 3 + 2]);
        }
        pair_result.Points3D.push_back(frame_pts);
        
        // Compute correlation (combined max of both cameras)
        std::vector<double> corr_comb = computeCombinedCorrelation(
            extractCorrelation(dic1.disps[fi], index_lut, W, H),
            extractCorrelation(dic2.disps[fi], index_lut, W, H)
        );
        pair_result.corrComb.push_back(corr_comb);
        
        // Compute face correlation
        size_t nFaces = faces.size() / 3;
        std::vector<double> face_corr(nFaces);
        for (size_t iface = 0; iface < nFaces; ++iface) {
            int v0 = faces[iface * 3];
            int v1 = faces[iface * 3 + 1];
            int v2 = faces[iface * 3 + 2];
            if (v0 < static_cast<int>(N) && v1 < static_cast<int>(N) && v2 < static_cast<int>(N)) {
                face_corr[iface] = std::max({corr_comb[v0], corr_comb[v1], corr_comb[v2]});
            } else {
                face_corr[iface] = std::numeric_limits<double>::quiet_NaN();
            }
        }
        pair_result.FaceCorrComb.push_back(face_corr);
        
        // Compute face centroids
        pair_result.FaceCentroids.push_back(computeFaceCentroids(frame_pts, faces));
        
        // Compute displacement from reference frame
        if (fi == 0) {
            P3D_ref = pts3d;
        }
        
        std::vector<double> disp_vec(N * 3), disp_mgn(N);
        for (size_t k = 0; k < N; ++k) {
            double dx = pts3d[k * 3 + 0] - P3D_ref[k * 3 + 0];
            double dy = pts3d[k * 3 + 1] - P3D_ref[k * 3 + 1];
            double dz = pts3d[k * 3 + 2] - P3D_ref[k * 3 + 2];
            disp_vec[k * 3 + 0] = dx;
            disp_vec[k * 3 + 1] = dy;
            disp_vec[k * 3 + 2] = dz;
            disp_mgn[k] = std::sqrt(dx * dx + dy * dy + dz * dz);
        }
        pair_result.Disp.DispVec.push_back(disp_vec);
        pair_result.Disp.DispMgn.push_back(disp_mgn);
    }
    
    return true;
}

DIC3Dcombined Reconstruction3DWorkflow::stitchPairs(
    const std::vector<DIC3DpairResults>& all_pairs) {
    return stitchPairsSimple(all_pairs);
}

void Reconstruction3DWorkflow::computeDisplacement(const Points3D& points3d_ref,
                                                   const Points3D& points3d_cur,
                                                   std::vector<double>& disp_vec,
                                                   std::vector<double>& disp_mgn) {
    size_t N = points3d_ref.x.size();
    disp_vec.resize(N * 3);
    disp_mgn.resize(N);
    
    for (size_t k = 0; k < N; ++k) {
        double dx = points3d_cur.x[k] - points3d_ref.x[k];
        double dy = points3d_cur.y[k] - points3d_ref.y[k];
        double dz = points3d_cur.z[k] - points3d_ref.z[k];
        
        disp_vec[k * 3 + 0] = dx;
        disp_vec[k * 3 + 1] = dy;
        disp_vec[k * 3 + 2] = dz;
        disp_mgn[k] = std::sqrt(dx * dx + dy * dy + dz * dz);
    }
}

std::vector<double> Reconstruction3DWorkflow::computeFaceCentroids(
    const Points3D& points3d,
    const std::vector<int>& faces) {
    
    size_t nFaces = faces.size() / 3;
    std::vector<double> centroids(nFaces * 3);
    
    for (size_t iface = 0; iface < nFaces; ++iface) {
        int v0 = faces[iface * 3 + 0];
        int v1 = faces[iface * 3 + 1];
        int v2 = faces[iface * 3 + 2];
        
        centroids[iface * 3 + 0] = (points3d.x[v0] + points3d.x[v1] + points3d.x[v2]) / 3.0;
        centroids[iface * 3 + 1] = (points3d.y[v0] + points3d.y[v1] + points3d.y[v2]) / 3.0;
        centroids[iface * 3 + 2] = (points3d.z[v0] + points3d.z[v1] + points3d.z[v2]) / 3.0;
    }
    
    return centroids;
}

std::vector<double> Reconstruction3DWorkflow::computeCombinedCorrelation(
    const std::vector<double>& corr_cam1,
    const std::vector<double>& corr_cam2) {
    
    std::vector<double> combined(corr_cam1.size());
    for (size_t i = 0; i < corr_cam1.size(); ++i) {
        combined[i] = std::max(corr_cam1[i], corr_cam2[i]);
    }
    return combined;
}

// ============================================================================
// Checkpoint Functions
// ============================================================================

bool Reconstruction3DWorkflow::hasReconstructionCheckpoint(const std::string& output_dir, 
                                                          int num_pairs) {
    std::ostringstream path;
    path << output_dir << "/DIC3Dcombined_" << num_pairs << "Pairs_stitched.bin";
    return std::filesystem::exists(path.str());
}

DIC3Dcombined Reconstruction3DWorkflow::loadCheckpoint(const std::string& checkpoint_path) {
    return DIC3Dcombined::loadBinary(checkpoint_path);
}

// ============================================================================
// Utility Functions
// ============================================================================

Reconstruction3DInputs Reconstruction3DWorkflow::buildInputs(int trial_id) {
    Reconstruction3DInputs inputs;
    inputs.trial_id = trial_id;
    
    std::ostringstream trial_oss;
    trial_oss << std::setw(3) << std::setfill('0') << trial_id;
    inputs.trial_str = trial_oss.str();
    
    inputs.num_pairs = config_.num_pair;
    inputs.output_dir = config_.dic_path + "/" + config_.subject_id + "/" + config_.material;
    
    // Build pair inputs
    for (int pair = 1; pair <= config_.num_pair; ++pair) {
        Reconstruction3DInputs::PairInput pair_input;
        pair_input.cam1 = (pair - 1) * 2 + 1;
        pair_input.cam2 = (pair - 1) * 2 + 2;
        
        std::string output_path = config_.dic_path + "/" + config_.subject_id + "/" + 
            config_.material + "/" + inputs.trial_str + "/" + config_.phase_id;
        
        pair_input.dic2d_cache_cam1 = output_path + "/.cache/ncorr" + 
            std::to_string(pair_input.cam1) + ".mat.bin";
        pair_input.dic2d_cache_cam2 = output_path + "/.cache/ncorr" + 
            std::to_string(pair_input.cam2) + ".mat.bin";
        
        // Find calibration files
        findCalibrationFiles(pair_input.cam1, pair_input.cam2,
                            pair_input.calib_cam1, pair_input.calib_cam2);
        
        inputs.pairs.push_back(pair_input);
    }
    
    return inputs;
}

bool Reconstruction3DWorkflow::findCalibrationFiles(int cam1, int cam2,
                                                    std::string& calib_cam1,
                                                    std::string& calib_cam2) {
    std::string calib_dir = config_.data_path + "/rawdata/" + config_.subject_id + 
        "/speckles/" + config_.material + "/calibration/";
    
    std::string cam1_str = "cam" + std::to_string(cam1);
    std::string cam2_str = "cam" + std::to_string(cam2);
    std::string camera1_str = "camera" + std::to_string(cam1);
    std::string camera2_str = "camera" + std::to_string(cam2);
    
    if (!std::filesystem::exists(calib_dir)) {
        return false;
    }
    
    for (const auto& entry : std::filesystem::directory_iterator(calib_dir)) {
        if (!entry.is_regular_file()) continue;
        
        auto name = entry.path().filename().string();
        std::string lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
        
        if (lower.find(cam1_str) != std::string::npos || 
            lower.find(camera1_str) != std::string::npos) {
            calib_cam1 = entry.path().string();
        }
        if (lower.find(cam2_str) != std::string::npos || 
            lower.find(camera2_str) != std::string::npos) {
            calib_cam2 = entry.path().string();
        }
        
        if (!calib_cam1.empty() && !calib_cam2.empty()) {
            break;
        }
    }
    
    return !calib_cam1.empty() && !calib_cam2.empty();
}

void Reconstruction3DWorkflow::undistortPoints(std::vector<double>& pts,
                                               const Utils::CameraParameters& params) {
    size_t N = pts.size() / 2;
    std::vector<cv::Point2d> pts_cv, pts_undist;
    pts_cv.reserve(N);
    
    for (size_t k = 0; k < N; ++k) {
        pts_cv.emplace_back(pts[k * 2 + 0], pts[k * 2 + 1]);
    }
    
    Utils::undistortPoints(pts_cv, params, pts_undist);
    
    for (size_t k = 0; k < N; ++k) {
        pts[k * 2 + 0] = pts_undist[k].x;
        pts[k * 2 + 1] = pts_undist[k].y;
    }
}

// Helper function to extract correlation from displacement
static std::vector<double> extractCorrelation(const ncorr::Disp2D& disp,
                                       const std::vector<int>& index_lut,
                                       int width, int height) {
    const auto& cc_array = disp.get_cc().get_array();
    const auto& roi_mask = disp.get_roi().get_mask();
    
    size_t num_points = std::count_if(index_lut.begin(), index_lut.end(), 
                                      [](int v) { return v >= 0; });
    std::vector<double> corr_out(num_points, 0.0);
    
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int idx = index_lut[y * width + x];
            if (idx < 0) continue;
            double cc = roi_mask(y, x) ? cc_array(y, x) : 0.0;
            corr_out[idx] = cc;
        }
    }
    
    return corr_out;
}

} // namespace cppxdic

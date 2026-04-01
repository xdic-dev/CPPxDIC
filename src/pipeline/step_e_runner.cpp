#include "cppxdic/pipeline/step_e_runner.h"

#include "data_serializer.h"
#include "dic_structures.h"
#include "mat_reader.h"
#include "surface_stitching.h"
#include "utils.h"
#include <Eigen/Dense>
#include <algorithm>
#include <array>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <vector>

namespace cppxdic::pipeline {

namespace {

std::array<double, 3> solve3DPoint(const std::vector<double>& l1, const std::vector<double>& l2,
                                   double x1, double y1, double x2, double y2) {
    Eigen::Matrix<double, 4, 3> m;
    Eigen::Matrix<double, 4, 1> v;

    m <<
        x1 * l1[8]  - l1[0],  x1 * l1[9]  - l1[1],  x1 * l1[10] - l1[2],
        y1 * l1[8]  - l1[4],  y1 * l1[9]  - l1[5],  y1 * l1[10] - l1[6],
        x2 * l2[8]  - l2[0],  x2 * l2[9]  - l2[1],  x2 * l2[10] - l2[2],
        y2 * l2[8]  - l2[4],  y2 * l2[9]  - l2[5],  y2 * l2[10] - l2[6];

    v <<
        l1[3] - x1,
        l1[7] - y1,
        l2[3] - x2,
        l2[7] - y2;

    Eigen::ColPivHouseholderQR<Eigen::Matrix<double, 4, 3>> qr(m);
    if (qr.rank() < 3) {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        return {nan, nan, nan};
    }

    const Eigen::Matrix<double, 3, 1> x = qr.solve(v);
    return {x[0], x[1], x[2]};
}

} // namespace

StepERunner::StepERunner(const Config& config)
    : config_(config) {}

bool StepERunner::run(const std::vector<int>& trial_target) const {
    std::cout << "Starting 3D Reconstruction (Step E)..." << std::endl;

    try {
        for (int trial : trial_target) {
            std::vector<DIC3DpairResults> all_pairs;
            std::vector<DIC2DPairResults> dic2d_info;

            std::ostringstream trial_str;
            trial_str << std::setw(3) << std::setfill('0') << trial;

            std::string calib_dir = Utils::buildCalibDir(config_);

            std::set<int> unique_cams;
            for (int pair = 1; pair <= config_.num_pair; ++pair) {
                int c1 = 0;
                int c2 = 0;
                Utils::getCamerasForPair(pair, c1, c2);
                unique_cams.insert(c1);
                unique_cams.insert(c2);
            }

            std::map<int, DLTCalibrationData> dlt_all_cams;
            for (int cam_id : unique_cams) {
                std::string dlt_path;
                if (std::filesystem::exists(calib_dir)) {
                    const std::string search_suffix = "cam_" + std::to_string(cam_id) + ".mat";
                    for (const auto& entry : std::filesystem::directory_iterator(calib_dir)) {
                        if (!entry.is_regular_file()) {
                            continue;
                        }
                        const auto name = entry.path().filename().string();
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
                if (MatReader::loadDLTCalibration(dlt_path, calib)) {
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

            std::map<int, Utils::CameraParameters> distortion_all_cams;
            std::map<int, std::string> distortion_paths;
            if (std::filesystem::exists(calib_dir)) {
                for (int cam_id : unique_cams) {
                    const std::string search_str = "cam_" + std::to_string(cam_id);
                    for (const auto& entry : std::filesystem::directory_iterator(calib_dir)) {
                        if (!entry.is_regular_file()) {
                            continue;
                        }
                        auto name = entry.path().filename().string();
                        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
                        if (name.find("cameracbparameters") != std::string::npos &&
                            name.find(search_str) != std::string::npos) {
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
                for (int cam_id : unique_cams) {
                    auto& dlt = dlt_all_cams[cam_id];
                    const auto distortion_it = distortion_all_cams.find(cam_id);
                    if (distortion_it == distortion_all_cams.end()) {
                        std::cout << "ERROR - Camera " << cam_id
                                  << ": missing distortion parameters, using original DLT params" << std::endl;
                        continue;
                    }
                    const auto& dist = distortion_it->second;

                    if (dlt.imageCentroids.empty() || dlt.C3Dtrue.empty() || dlt.columns.empty()) {
                        std::cout << "ERROR - Camera " << cam_id << ": missing calibration data for DLT recalculation, "
                                  << "using original DLT params with per-point undistortion fallback" << std::endl;
                        continue;
                    }

                    const size_t n_centroids = dlt.imageCentroids_rows;
                    std::vector<cv::Point2d> centroids_in(n_centroids);
                    for (size_t i = 0; i < n_centroids; ++i) {
                        centroids_in[i] = cv::Point2d(dlt.imageCentroids[i * 2], dlt.imageCentroids[i * 2 + 1]);
                    }

                    std::vector<cv::Point2d> centroids_undist;
                    Utils::undistortPoints(centroids_in, dist, centroids_undist);

                    std::vector<double> p2d(n_centroids * 2);
                    for (size_t i = 0; i < n_centroids; ++i) {
                        p2d[i * 2] = centroids_undist[i].x;
                        p2d[i * 2 + 1] = centroids_undist[i].y;
                    }

                    const size_t d0 = dlt.C3Dtrue_dim0;
                    const size_t n_cols = dlt.columns.size();
                    std::vector<double> p3d(d0 * n_cols * 3);
                    for (size_t ci = 0; ci < n_cols; ++ci) {
                        const size_t col = static_cast<size_t>(dlt.columns[ci]) - 1;
                        for (size_t row = 0; row < d0; ++row) {
                            const size_t dst_idx = ci * d0 + row;
                            const size_t src_idx = row * dlt.C3Dtrue_dim1 + col;
                            p3d[dst_idx * 3] = dlt.C3Dtrue[src_idx * 3];
                            p3d[dst_idx * 3 + 1] = dlt.C3Dtrue[src_idx * 3 + 1];
                            p3d[dst_idx * 3 + 2] = dlt.C3Dtrue[src_idx * 3 + 2];
                        }
                    }

                    const size_t n_points = d0 * n_cols;
                    if (n_points != n_centroids) {
                        std::cerr << "ERROR - Camera " << cam_id << ": centroid count (" << n_centroids
                                  << ") != C3D point count (" << n_points << "), skipping DLT recalculation" << std::endl;
                        continue;
                    }

                    std::vector<double> recalculated;
                    if (Utils::DLT11Calibration(p2d.data(), p3d.data(), n_points, recalculated)) {
                        dlt.DLTparams = recalculated;
                        std::cout << "INFO - Camera " << cam_id << ": DLT params recalculated from "
                                  << n_points << " undistorted calibration points" << std::endl;
                    } else {
                        std::cerr << "ERROR - Camera " << cam_id << ": DLT11Calibration failed" << std::endl;
                    }
                }
            } else if (!distortion_all_cams.empty()) {
                std::cout << "Distortion parameters found for " << distortion_all_cams.size()
                          << "/" << unique_cams.size() << " cameras (disabled - need all)" << std::endl;
                use_distortion_removal = false;
            }

            for (int pair = 1; pair <= config_.num_pair; ++pair) {
                std::cout << "\n=== Processing Pair " << pair << " ===" << std::endl;

                int cam_1 = 0;
                int cam_2 = 0;
                Utils::getCamerasForPair(pair, cam_1, cam_2);

                if (dlt_all_cams.find(cam_1) == dlt_all_cams.end() ||
                    dlt_all_cams.find(cam_2) == dlt_all_cams.end()) {
                    std::cerr << "Missing DLT calibration for pair " << pair
                              << " (cam " << cam_1 << " or " << cam_2 << "). Skipping." << std::endl;
                    continue;
                }

                const auto& dlt_cam1 = dlt_all_cams[cam_1];
                const auto& dlt_cam2 = dlt_all_cams[cam_2];
                const auto& l1 = dlt_cam1.DLTparams;
                const auto& l2 = dlt_cam2.DLTparams;

                auto serializer = DataSerializer::create(config_.data_format);
                const std::string output_dir = Utils::buildOutputUntilPhaseDir(config_, trial);
                const std::string dic2d_file =
                    Utils::buildDic2DPairResultsFilePath(output_dir, cam_1, cam_2, serializer->extension());

                if (!std::filesystem::exists(dic2d_file)) {
                    std::cerr << "ERROR - Missing DIC2DPairResults for trial " << trial << ", pair " << pair << ". Skipping." << std::endl;
                    std::cerr << "  Expected: " << dic2d_file << std::endl;
                    continue;
                }

                DIC2DPairResults dic2d;
                if (!serializer->loadDIC2DPairResults(dic2d_file, dic2d)) {
                    std::cerr << "ERROR - Failed to load DIC2DPairResults: " << dic2d_file << ". Skipping." << std::endl;
                    continue;
                }

                if (config_.debug_mode) {
                    std::cout << "[DEBUG] Loaded DIC2DPairResults from: " << dic2d_file << std::endl;
                    std::cout << "[DEBUG]   nImages=" << dic2d.nImages << ", Points=" << dic2d.Points.size()
                              << ", Faces=" << dic2d.Faces.size() / 3 << std::endl;
                    std::cout << "[DEBUG]   DLT cam" << cam_1 << ": " << dlt_cam1.filePath << std::endl;
                    std::cout << "[DEBUG]   DLT cam" << cam_2 << ": " << dlt_cam2.filePath << std::endl;
                }

                const int n_images = dic2d.nImages;
                const auto& cor_coeff = dic2d.CorCoeffVec;
                const auto& faces = dic2d.Faces;
                const auto& face_colors = dic2d.FaceColors;
                const auto& points = dic2d.Points;

                DIC3DpairResults pair_result;
                pair_result.cameraPairInd = {cam_1, cam_2};
                pair_result.DLTpath = {dlt_cam1.filePath, dlt_cam2.filePath};
                pair_result.DLTparameters = {l1, l2};
                pair_result.Faces = faces;
                pair_result.FaceColors = face_colors;

                if (use_distortion_removal) {
                    pair_result.distortionModel = {"distortion", "distortion"};
                    pair_result.distortionPath = {distortion_paths[cam_1], distortion_paths[cam_2]};
                } else {
                    pair_result.distortionModel = {"none", "none"};
                    pair_result.distortionPath = {"none", "none"};
                }

                const size_t n_faces = faces.size() / 3;
                std::vector<double> p3d_ref;

                for (int frame = 0; frame < n_images; ++frame) {
                    const auto& p1 = points[frame];
                    const auto& p2 = points[frame + n_images];
                    const size_t point_count = p1.x.size();

                    std::vector<double> pts3d(point_count * 3, std::numeric_limits<double>::quiet_NaN());
                    Points3D frame_pts;
                    frame_pts.x.resize(point_count);
                    frame_pts.y.resize(point_count);
                    frame_pts.z.resize(point_count);

                    for (size_t k = 0; k < point_count; ++k) {
                        const double x1 = p1.x[k];
                        const double y1 = p1.y[k];
                        const double x2 = p2.x[k];
                        const double y2 = p2.y[k];
                        if (!std::isnan(x1) && !std::isnan(y1) && !std::isnan(x2) && !std::isnan(y2)) {
                            const auto x = solve3DPoint(l1, l2, x1, y1, x2, y2);
                            pts3d[k * 3] = x[0];
                            pts3d[k * 3 + 1] = x[1];
                            pts3d[k * 3 + 2] = x[2];
                            frame_pts.x[k] = x[0];
                            frame_pts.y[k] = x[1];
                            frame_pts.z[k] = x[2];
                        } else {
                            frame_pts.x[k] = std::numeric_limits<double>::quiet_NaN();
                            frame_pts.y[k] = std::numeric_limits<double>::quiet_NaN();
                            frame_pts.z[k] = std::numeric_limits<double>::quiet_NaN();
                        }
                    }
                    pair_result.Points3D.push_back(frame_pts);

                    std::vector<double> corr_comb(point_count, 0.0);
                    if (frame < static_cast<int>(cor_coeff.size()) &&
                        frame + n_images < static_cast<int>(cor_coeff.size())) {
                        for (size_t k = 0; k < point_count; ++k) {
                            const double cc1 = k < cor_coeff[frame].size() ? cor_coeff[frame][k] : 0.0;
                            const double cc2 = k < cor_coeff[frame + n_images].size() ? cor_coeff[frame + n_images][k] : 0.0;
                            corr_comb[k] = std::max(cc1, cc2);
                        }
                    }
                    pair_result.corrComb.push_back(corr_comb);

                    std::vector<double> face_corr(n_faces, std::numeric_limits<double>::quiet_NaN());
                    for (size_t iface = 0; iface < n_faces; ++iface) {
                        const int v0 = faces[iface * 3];
                        const int v1 = faces[iface * 3 + 1];
                        const int v2 = faces[iface * 3 + 2];
                        if (v0 >= 0 && v0 < static_cast<int>(point_count) &&
                            v1 >= 0 && v1 < static_cast<int>(point_count) &&
                            v2 >= 0 && v2 < static_cast<int>(point_count)) {
                            face_corr[iface] = std::max({corr_comb[v0], corr_comb[v1], corr_comb[v2]});
                        }
                    }
                    pair_result.FaceCorrComb.push_back(face_corr);

                    std::vector<double> face_centroids(n_faces * 3, std::numeric_limits<double>::quiet_NaN());
                    for (size_t iface = 0; iface < n_faces; ++iface) {
                        const int v0 = faces[iface * 3];
                        const int v1 = faces[iface * 3 + 1];
                        const int v2 = faces[iface * 3 + 2];
                        if (v0 >= 0 && v0 < static_cast<int>(point_count) &&
                            v1 >= 0 && v1 < static_cast<int>(point_count) &&
                            v2 >= 0 && v2 < static_cast<int>(point_count)) {
                            face_centroids[iface * 3] = (pts3d[v0 * 3] + pts3d[v1 * 3] + pts3d[v2 * 3]) / 3.0;
                            face_centroids[iface * 3 + 1] = (pts3d[v0 * 3 + 1] + pts3d[v1 * 3 + 1] + pts3d[v2 * 3 + 1]) / 3.0;
                            face_centroids[iface * 3 + 2] = (pts3d[v0 * 3 + 2] + pts3d[v1 * 3 + 2] + pts3d[v2 * 3 + 2]) / 3.0;
                        }
                    }
                    pair_result.FaceCentroids.push_back(face_centroids);

                    if (frame == 0) {
                        p3d_ref = pts3d;
                    }
                    std::vector<double> disp_vec(point_count * 3, std::numeric_limits<double>::quiet_NaN());
                    std::vector<double> disp_mgn(point_count, std::numeric_limits<double>::quiet_NaN());
                    for (size_t k = 0; k < point_count; ++k) {
                        const double dx = pts3d[k * 3] - p3d_ref[k * 3];
                        const double dy = pts3d[k * 3 + 1] - p3d_ref[k * 3 + 1];
                        const double dz = pts3d[k * 3 + 2] - p3d_ref[k * 3 + 2];
                        disp_vec[k * 3] = dx;
                        disp_vec[k * 3 + 1] = dy;
                        disp_vec[k * 3 + 2] = dz;
                        disp_mgn[k] = std::sqrt(dx * dx + dy * dy + dz * dz);
                    }
                    pair_result.Disp.DispVec.push_back(disp_vec);
                    pair_result.Disp.DispMgn.push_back(disp_mgn);
                }

                all_pairs.push_back(pair_result);
                dic2d_info.push_back(std::move(dic2d));

                std::cout << "✓ Pair " << pair << " complete: "
                          << pair_result.Points3D[0].x.size() << " points, "
                          << n_faces << " faces" << std::endl;
            }

            std::cout << "\n=== Stitching " << all_pairs.size() << " pairs ===" << std::endl;
            DIC3Dcombined stitched;
            if (all_pairs.empty()) {
                std::cerr << "ERROR - No pairs successfully reconstructed for trial " << trial << std::endl;
                continue;
            }

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
                } else if (dic2d.pairOrder != stitch_pair_order || dic2d.pairForced != stitch_pair_forced) {
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
                    ProtocolFileData protocol_data;
                    stitch_pair_order.reserve(config_.num_pair);
                    for (int pair = 1; pair <= config_.num_pair; ++pair) {
                        stitch_pair_order.push_back(pair);
                    }

                    try {
                        const std::string protocol_dir = Utils::buildProtocolDir(config_, true, true, true, true);
                        const auto protocol_files = Utils::findFiles(protocol_dir, "*.mat");
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
                    std::cout << "  Stitch metadata missing from DIC2D results; using legacy fallback" << std::endl;
                }

                std::cout << "  Stitch order: ";
                for (size_t i = 0; i < stitch_pair_order.size(); ++i) {
                    if (i > 0) {
                        std::cout << ", ";
                    }
                    std::cout << stitch_pair_order[i];
                }
                std::cout << std::endl;
                std::cout << "  Pair forced metadata: " << (stitch_pair_forced ? "true" : "false") << std::endl;
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

            stitched.AllPairsResults = all_pairs;
            stitched.DIC2Dinfo = dic2d_info;
            std::cout << "  Stored " << stitched.DIC2Dinfo.size() << " DIC2D pair results" << std::endl;

            auto serializer = DataSerializer::create(config_.data_format);
            const auto output_dir = Utils::buildOutputUntilPhaseDir(config_, trial);
            const std::string output_file =
                Utils::buildDic3DCombinedFilePath(output_dir, config_.num_pair, serializer->extension());

            if (std::filesystem::exists(output_file)) {
                std::cout << "Checkpoint found: " << output_file << " (skipping)" << std::endl;
            } else {
                std::cout << "Writing DIC3Dcombined to: " << output_file << std::endl;
                if (!serializer->saveDIC3Dcombined(output_file, stitched)) {
                    std::cerr << "Failed to write DIC3Dcombined" << std::endl;
                } else {
                    std::cout << "✓ Saved DIC3Dcombined: " << output_file << std::endl;
                }
            }
        }
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error in 3D Reconstruction: " << e.what() << std::endl;
        return false;
    }
}

} // namespace cppxdic::pipeline

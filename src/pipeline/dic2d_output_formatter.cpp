#include "cppxdic/pipeline/dic2d_output_formatter.h"

#include "data_serializer.h"
#include "delaunay_triangulation.h"
#include "dic_structures.h"
#include "matlab_functions.h"
#include "ncorr.h"
#include "utils.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>

namespace cppxdic::pipeline {

Dic2DOutputFormatter::Dic2DOutputFormatter(const Config& config,
                                           const BaseParameters& base_params,
                                           const StepParameters& tracking_params)
    : config_(config),
      base_params_(base_params),
      tracking_params_(tracking_params) {}

void Dic2DOutputFormatter::format(int stereopair,
                                  const std::vector<int>& pair_order,
                                  bool pair_forced) const {
    std::cout << "Formatting output files (step2_dic_finish equivalent)..." << std::endl;

    int cam_1 = 0;
    int cam_2 = 0;
    Utils::getCamerasForPair(stereopair, cam_1, cam_2);

    const std::string ncorr1_bin = base_params_.outputPath + "/ncorr" + std::to_string(cam_1) + ".bin";
    const std::string ncorr2_bin = base_params_.outputPath + "/ncorr" + std::to_string(cam_2) + ".bin";
    const std::string ncorr12_bin = base_params_.outputPath + "/ncorr" + std::to_string(cam_1) + std::to_string(cam_2) + ".bin";

    if (!std::filesystem::exists(ncorr1_bin) || !std::filesystem::exists(ncorr2_bin) || !std::filesystem::exists(ncorr12_bin)) {
        std::cerr << "Warning: cached ncorr result files not found" << std::endl;
        if (!std::filesystem::exists(ncorr1_bin)) std::cerr << "  Missing: " << ncorr1_bin << std::endl;
        if (!std::filesystem::exists(ncorr2_bin)) std::cerr << "  Missing: " << ncorr2_bin << std::endl;
        if (!std::filesystem::exists(ncorr12_bin)) std::cerr << "  Missing: " << ncorr12_bin << std::endl;
        return;
    }

    auto serializer = cppxdic::DataSerializer::create(config_.data_format);
    const std::string output_file = base_params_.outputPath + "/myDIC2DpairResults_C_" +
        std::to_string(cam_1) + "_C_" + std::to_string(cam_2) + serializer->extension();

    if (std::filesystem::exists(output_file)) {
        std::cout << "Checkpoint found: " << output_file << std::endl;
        return;
    }

    std::cout << "  Loading cached DIC outputs..." << std::endl;
    ncorr::DIC_analysis_output dic1 = ncorr::DIC_analysis_output::load(ncorr1_bin);
    ncorr::DIC_analysis_output dic2 = ncorr::DIC_analysis_output::load(ncorr2_bin);
    ncorr::DIC_analysis_output dic12 = ncorr::DIC_analysis_output::load(ncorr12_bin);

    if (dic1.disps.empty() || dic2.disps.empty() || dic12.disps.empty()) {
        std::cerr << "Error: DIC outputs are empty" << std::endl;
        return;
    }

    if (config_.step_d_replacebadcorr) {
        std::cout << "  Applying replacebadcorr (MATLAB step2_dic_finish)..." << std::endl;
        cppxdic::matlab_replacebadcorr(dic1);
        cppxdic::matlab_replacebadcorr(dic2);
        cppxdic::matlab_replacebadcorr(dic12);
    }

    const size_t n_frames_cam1 = dic1.disps.size();
    const size_t n_frames_cam2 = dic2.disps.size();
    const int factor = dic1.disps[0].get_scalefactor();
    const double nan = std::numeric_limits<double>::quiet_NaN();

    std::cout << "  Processing " << n_frames_cam1 << " cam1 frames, "
              << n_frames_cam2 << " cam2 frames, Factor=" << factor << std::endl;
    if (n_frames_cam2 + 1 != n_frames_cam1) {
        std::cout << "  Warning: cam2 tracking count differs from MATLAB expectation "
                  << "(expected " << (n_frames_cam1 - 1) << ", got " << n_frames_cam2 << ")" << std::endl;
    }

    DIC2DPairResults results;
    results.nCamRef = cam_1;
    results.nCamDef = cam_2;
    results.nImages = static_cast<int>(n_frames_cam1);
    results.pairOrder = pair_order;
    results.pairForced = pair_forced;
    results.ncorrInfo.cutoff_corrcoef = {
        config_.ncorr_cutoff_corrcoef,
        config_.ncorr_cutoff_corrcoef,
        config_.ncorr_cutoff_corrcoef
    };
    results.ncorrInfo.cutoff_diffnorm = tracking_params_.cutoff_diffnorm;
    results.ncorrInfo.cutoff_iteration = tracking_params_.cutoff_iteration;
    results.ncorrInfo.imgcorr = {"reference", "current"};
    results.ncorrInfo.lenscoef = 0;
    results.ncorrInfo.pixtounits = config_.units_per_pixel;
    results.ncorrInfo.radius = tracking_params_.radius;
    results.ncorrInfo.spacing = tracking_params_.spacing;
    results.ncorrInfo.stepanalysis.enabled = tracking_params_.stepanalysis_params.enabled;
    results.ncorrInfo.stepanalysis.type = tracking_params_.stepanalysis_params.type;
    results.ncorrInfo.stepanalysis.auto_update = tracking_params_.stepanalysis_params.auto_update;
    results.ncorrInfo.stepanalysis.step = tracking_params_.stepanalysis_params.step;
    results.ncorrInfo.subsettrunc = false;
    results.ncorrInfo.total_threads = tracking_params_.total_threads;
    results.ncorrInfo.type = tracking_params_.type;
    results.ncorrInfo.units = "pixels";

    const auto& roi1 = dic1.disps[0].get_roi();
    const auto& roi_mask = roi1.get_mask();
    results.ROImask = cv::Mat(roi_mask.height(), roi_mask.width(), CV_8U);
    std::vector<std::pair<int, int>> roi_coords;
    std::vector<cv::Point2d> pref;

    for (int y = 0; y < roi_mask.height(); ++y) {
        for (int x = 0; x < roi_mask.width(); ++x) {
            results.ROImask.at<uint8_t>(y, x) = roi_mask(y, x) ? 255 : 0;
            if (roi_mask(y, x)) {
                roi_coords.emplace_back(y, x);
                pref.emplace_back(static_cast<double>(x * factor + 1),
                                  static_cast<double>(y * factor + 1));
            }
        }
    }
    std::cout << "  Reference points: " << pref.size() << std::endl;

    const size_t n_total_frames = n_frames_cam1 + 1 + n_frames_cam2;
    results.Points.resize(n_total_frames);
    results.CorCoeffVec.resize(n_total_frames);

    auto fillDirectFrame = [&](const ncorr::Disp2D& disp, size_t out_idx) {
        const auto& u_array = disp.get_u().get_array();
        const auto& v_array = disp.get_v().get_array();
        const auto& cc_array = disp.get_cc().get_array();

        Points2D pts2d;
        pts2d.x.reserve(pref.size());
        pts2d.y.reserve(pref.size());
        std::vector<double> corrcoef;
        corrcoef.reserve(pref.size());

        for (size_t idx = 0; idx < roi_coords.size(); ++idx) {
            const auto [y, x] = roi_coords[idx];
            double u = u_array(y, x);
            double v = v_array(y, x);
            double cc = cc_array(y, x);

            if (u == 0.0) u = nan;
            if (v == 0.0) v = nan;
            if (cc == 0.0) cc = nan;

            pts2d.x.push_back(std::isnan(u) ? nan : (pref[idx].x + u));
            pts2d.y.push_back(std::isnan(v) ? nan : (pref[idx].y + v));
            corrcoef.push_back(cc);
        }

        results.Points[out_idx] = std::move(pts2d);
        results.CorCoeffVec[out_idx] = std::move(corrcoef);
    };

    std::cout << "  Processing cam1 frames..." << std::endl;
    for (size_t ii = 0; ii < n_frames_cam1; ++ii) {
        fillDirectFrame(dic1.disps[ii], ii);
    }

    std::cout << "  Processing inter-camera matching frame..." << std::endl;
    fillDirectFrame(dic12.disps.front(), n_frames_cam1);

    const auto& disp12_ref = dic12.disps.front();
    const auto& u12_full = disp12_ref.get_u().get_array();
    const auto& v12_full = disp12_ref.get_v().get_array();

    std::cout << "  Processing cam2 frames (mapped through matching)..." << std::endl;
    for (size_t ii = 0; ii < n_frames_cam2; ++ii) {
        const auto& disp2 = dic2.disps[ii];
        const auto& u2_array = disp2.get_u().get_array();
        const auto& v2_array = disp2.get_v().get_array();
        const auto& cc2_array = disp2.get_cc().get_array();
        const int h2 = disp2.get_u().data_height();
        const int w2 = disp2.get_u().data_width();

        auto interpolateWeighted = [&](const auto& arr, double rx, double ry) -> double {
            const int x0 = static_cast<int>(std::floor(rx));
            const int y0 = static_cast<int>(std::floor(ry));
            const int x1 = x0 + 1;
            const int y1 = y0 + 1;

            double numerator = 0.0;
            double denominator = 0.0;
            const std::array<std::pair<int, int>, 4> samples = {{
                {x0, y0}, {x1, y0}, {x0, y1}, {x1, y1}
            }};

            for (const auto& [sx, sy] : samples) {
                if (sx < 0 || sy < 0 || sx >= w2 || sy >= h2) {
                    continue;
                }
                const double value = arr(sy, sx);
                if (value == 0.0) {
                    continue;
                }
                const double dx = rx - static_cast<double>(sx);
                const double dy = ry - static_cast<double>(sy);
                const double dist = std::sqrt(dx * dx + dy * dy);
                const double weight = dist < 1e-12 ? 1e12 : 1.0 / dist;
                numerator += value * weight;
                denominator += weight;
            }

            if (denominator > 0.0) {
                return numerator / denominator;
            }

            const int cx = std::clamp(static_cast<int>(std::round(rx)), 0, w2 - 1);
            const int cy = std::clamp(static_cast<int>(std::round(ry)), 0, h2 - 1);
            const double fallback = arr(cy, cx);
            return fallback == 0.0 ? nan : fallback;
        };

        Points2D pts2d;
        pts2d.x.reserve(pref.size());
        pts2d.y.reserve(pref.size());
        std::vector<double> corrcoef;
        corrcoef.reserve(pref.size());

        for (size_t idx = 0; idx < roi_coords.size(); ++idx) {
            const auto [y, x] = roi_coords[idx];
            const double u12 = u12_full(y, x);
            const double v12 = v12_full(y, x);
            double cc = cc2_array(y, x);
            if (cc == 0.0) {
                cc = nan;
            }
            corrcoef.push_back(cc);

            if (u12 == 0.0 && v12 == 0.0) {
                pts2d.x.push_back(nan);
                pts2d.y.push_back(nan);
                continue;
            }

            const double cam2_rx = static_cast<double>(x) + u12 / static_cast<double>(factor);
            const double cam2_ry = static_cast<double>(y) + v12 / static_cast<double>(factor);
            const double u2_mapped = interpolateWeighted(u2_array, cam2_rx, cam2_ry);
            const double v2_mapped = interpolateWeighted(v2_array, cam2_rx, cam2_ry);

            if (std::isnan(u2_mapped) || std::isnan(v2_mapped)) {
                pts2d.x.push_back(nan);
                pts2d.y.push_back(nan);
                continue;
            }

            pts2d.x.push_back(pref[idx].x + u12 + u2_mapped);
            pts2d.y.push_back(pref[idx].y + v12 + v2_mapped);
        }

        results.Points[n_frames_cam1 + 1 + ii] = std::move(pts2d);
        results.CorCoeffVec[n_frames_cam1 + 1 + ii] = std::move(corrcoef);
    }

    std::cout << "  Creating Delaunay triangulation..." << std::endl;
    std::vector<cv::Point2f> pref_float;
    pref_float.reserve(pref.size());
    for (const auto& p : pref) {
        pref_float.emplace_back(static_cast<float>(p.x), static_cast<float>(p.y));
    }
    results.Faces = DelaunayTriangulation::compute(pref_float);
    const double max_edge = 1.1 * std::sqrt(2.0) * factor;
    results.Faces = DelaunayTriangulation::filterByEdgeLength(results.Faces, pref_float, max_edge);
    results.Faces = DelaunayTriangulation::flipOrientation(results.Faces);
    std::cout << "  Triangles: " << (results.Faces.size() / 3) << std::endl;

    results.FaceColors.resize(results.Faces.size() / 3, 128.0);

    std::cout << "  Writing results..." << std::endl;
    if (serializer->saveDIC2DPairResults(output_file, results)) {
        std::cout << "Output formatting complete: " << output_file << std::endl;
    } else {
        std::cerr << "Failed to write DIC2DPairResults" << std::endl;
    }
}

} // namespace cppxdic::pipeline

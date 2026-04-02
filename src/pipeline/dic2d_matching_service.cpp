#include "cppxdic/pipeline/dic2d_matching_service.h"

#include "cppxdic/pipeline/dic2d_frame_preparer.h"
#include "cppxdic/pipeline/dic2d_ncorr_runner.h"
#include "roi_manager.h"
#include "utils.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {

cv::Mat makeDebugCanvas(const cv::Mat& gray) {
    cv::Mat normalized;
    if (gray.empty()) {
        return normalized;
    }
    if (gray.type() == CV_8UC1) {
        normalized = gray.clone();
    } else {
        cv::normalize(gray, normalized, 0, 255, cv::NORM_MINMAX, CV_8U);
    }
    cv::Mat color;
    cv::cvtColor(normalized, color, cv::COLOR_GRAY2BGR);
    return color;
}

void overlayMask(cv::Mat& canvas, const cv::Mat& mask, const cv::Scalar& color) {
    if (canvas.empty() || mask.empty()) {
        return;
    }
    cv::Mat mask_binary;
    if (mask.type() == CV_8UC1) {
        cv::threshold(mask, mask_binary, 0, 255, cv::THRESH_BINARY);
    } else {
        mask.convertTo(mask_binary, CV_8U);
        cv::threshold(mask_binary, mask_binary, 0, 255, cv::THRESH_BINARY);
    }
    cv::Mat overlay(canvas.size(), canvas.type(), color);
    cv::Mat blended;
    cv::addWeighted(canvas, 0.75, overlay, 0.25, 0.0, blended);
    blended.copyTo(canvas, mask_binary);
}

void drawSeed(cv::Mat& canvas, const cppxdic::SeedPoint& seed, const cv::Scalar& color) {
    if (canvas.empty() || seed.pw.size() < 2) {
        return;
    }
    const cv::Point center(seed.pw[0] - 1, seed.pw[1] - 1);
    cv::drawMarker(canvas, center, color, cv::MARKER_CROSS, 18, 2);
    cv::circle(canvas, center, 5, color, 2);
}

void drawSeedDisplacement(cv::Mat& canvas,
                          const cppxdic::SeedPoint& before,
                          const cppxdic::SeedPoint& after,
                          const cv::Scalar& color) {
    if (canvas.empty() || before.pw.size() < 2 || after.pw.size() < 2) {
        return;
    }
    const cv::Point p0(before.pw[0] - 1, before.pw[1] - 1);
    const cv::Point p1(after.pw[0] - 1, after.pw[1] - 1);
    cv::arrowedLine(canvas, p0, p1, color, 2, cv::LINE_AA, 0, 0.15);
}

std::string sanitizeStageName(const std::string& stage_name) {
    std::string out = stage_name;
    for (char& ch : out) {
        if (!std::isalnum(static_cast<unsigned char>(ch))) {
            ch = '_';
        }
    }
    return out;
}

} // namespace

namespace cppxdic::pipeline {

Dic2DMatchingService::Dic2DMatchingService(const Config& config,
                                           const BaseParameters& base_params,
                                           const StepParameters& tracking_params,
                                           const StepParameters& matching_params)
    : config_(config),
      base_params_(base_params),
      tracking_params_(tracking_params),
      matching_params_(matching_params) {}

bool Dic2DMatchingService::initializeROIAndSeed(const std::vector<cv::Mat>& cam_first_saturated,
                                                cv::Mat& refmask_ref,
                                                cv::Mat& refmask_trial,
                                                SeedPoint& ref_seed_point,
                                                SeedPoint& initial_seed_point_set1) const {
    std::vector<cv::Mat> reftrial_cam_first_raw;

    refmask_ref = ROIManager::loadOrCreateROI(base_params_, cam_first_saturated[0]);
    if (refmask_ref.empty()) {
        return false;
    }

    ref_seed_point = ROIManager::loadOrCreateSeed(base_params_, refmask_ref);
    ref_seed_point.sw = ROIManager::mapPixel2Subset(ref_seed_point.pw, tracking_params_.spacing);

    if (!std::filesystem::exists(base_params_.matchingfile)) {
        std::cout << "\n--> STEP: MATCHING file computation)" << std::endl;
        std::vector<cv::Mat> reftrial_cam_second_raw;
        std::cout << "Reading REF Trial video data..." << std::endl;
        if (!Dic2DFramePreparer(config_).importVideoFrames(base_params_.reftrial,
                                                           base_params_.stereopair,
                                                           reftrial_cam_first_raw,
                                                           reftrial_cam_second_raw)) {
            std::cerr << "Failed to import REF Trial video frames" << std::endl;
            return false;
        }
        std::cout << "Reading REF Trial video data done. Frames: " << reftrial_cam_first_raw.size() << std::endl;

        std::ostringstream message_oss;
        message_oss << "Matching Pair " << base_params_.stereopair
                    << ": trial " << base_params_.reftrial
                    << "'s frame 1 TO trial " << base_params_.trial << "'s frame 1";
        if (!matchingInitialFrame(reftrial_cam_first_raw,
                                  cam_first_saturated,
                                  refmask_ref,
                                  base_params_.matchingfile,
                                  message_oss.str(),
                                  ref_seed_point,
                                  refmask_trial,
                                  initial_seed_point_set1)) {
            return false;
        }
    } else {
        std::cout << "Checkpoint found: " << base_params_.matchingfile << std::endl;
        std::cout << "--> STEP: MATCHING file loaded from checkpoint (skipped computation)" << std::endl;
    }

    if (std::filesystem::exists(base_params_.matchingfile)) {
        auto dic_output = ncorr::DIC_analysis_output::load(base_params_.matchingfile);
        if (updateMaskAndSeedFromOutput(refmask_ref, ref_seed_point, dic_output,
                                        refmask_trial, initial_seed_point_set1)) {
            std::cout << "--> STEP: MATCHING transformation applied" << std::endl;
            if (config_.debug_mode) {
                if (reftrial_cam_first_raw.empty()) {
                    std::vector<cv::Mat> reftrial_cam_second_raw;
                    Dic2DFramePreparer(config_).importVideoFrames(base_params_.reftrial,
                                                                  base_params_.stereopair,
                                                                  reftrial_cam_first_raw,
                                                                  reftrial_cam_second_raw);
                }
                writeMatchingDebugPanel("ipm",
                                        reftrial_cam_first_raw.empty() ? cam_first_saturated[0] : reftrial_cam_first_raw[0],
                                        cam_first_saturated[0],
                                        refmask_ref,
                                        refmask_trial,
                                        ref_seed_point,
                                        initial_seed_point_set1,
                                        dic_output);
            }
            return true;
        }
        std::cerr << "Warning: MATCHING file has no displacements" << std::endl;
    }

    std::cerr << "Warning: Something went wrong for the MATCHING" << std::endl;
    return false;
}

bool Dic2DMatchingService::performStereoPairMatching(const std::vector<cv::Mat>& cam_first_saturated,
                                                     const std::vector<cv::Mat>& cam_second_saturated,
                                                     cv::Mat& refmask_trial,
                                                     SeedPoint& initial_seed_point_set1,
                                                     cv::Mat& refmask_trial_matched,
                                                     SeedPoint& initial_seed_point_set2) const {
    std::cout << "MATCHING STEP - RUN #1" << std::endl;

    int cam_1 = 0;
    int cam_2 = 0;
    Utils::getCamerasForPair(base_params_.stereopair, cam_1, cam_2);
    const std::string ncorr_matching_path =
        base_params_.outputPath + "/ncorr" + std::to_string(cam_1) + std::to_string(cam_2) + ".bin";

    if (!std::filesystem::exists(ncorr_matching_path)) {
        std::cout << "\n--> STEP: MATCHING file computation)" << std::endl;
        std::ostringstream message_oss;
        message_oss << "Matching Inside Camera Pair (1-2) : Cam1's frame 1 VS cam2's frame 1";
        matchingInitialFrame(cam_first_saturated,
                             cam_second_saturated,
                             refmask_trial,
                             ncorr_matching_path,
                             message_oss.str(),
                             initial_seed_point_set1,
                             refmask_trial_matched,
                             initial_seed_point_set2);
    } else {
        std::cout << "Checkpoint found: " << ncorr_matching_path << std::endl;
        std::cout << "--> STEP: MATCHING loaded from checkpoint (skipped computation)" << std::endl;
    }

    if (std::filesystem::exists(ncorr_matching_path)) {
        auto dic_output = ncorr::DIC_analysis_output::load(ncorr_matching_path);
        if (updateMaskAndSeedFromOutput(refmask_trial, initial_seed_point_set1, dic_output,
                                        refmask_trial_matched, initial_seed_point_set2)) {
            std::cout << "--> STEP: MATCHING transformation applied" << std::endl;
            if (config_.debug_mode) {
                writeMatchingDebugPanel("icm",
                                        cam_first_saturated[0],
                                        cam_second_saturated[0],
                                        refmask_trial,
                                        refmask_trial_matched,
                                        initial_seed_point_set1,
                                        initial_seed_point_set2,
                                        dic_output);
            }
            return true;
        }
        std::cerr << "Warning: Matching file has no displacements" << std::endl;
    }

    std::cerr << "Warning: Something went wrong with the matching" << std::endl;
    return false;
}

bool Dic2DMatchingService::matchingInitialFrame(const std::vector<cv::Mat>& cam_ref,
                                                const std::vector<cv::Mat>& cam_cur,
                                                const cv::Mat& refmask_ref,
                                                const std::string& ncorr_matching_path,
                                                const std::string& message,
                                                const SeedPoint& ref_seed_point,
                                                cv::Mat& refmask_cur_matched,
                                                SeedPoint& after_disp_seed_point) const {
    std::cout << message << std::endl;

    std::vector<cv::Mat> cur_imgs;
    cur_imgs.reserve(2);
    cur_imgs.push_back(cam_cur[0]);
    cur_imgs.push_back(cam_ref[0]);

    StepParameters matching_params = matching_params_;
    matching_params.initial_seed = {
        static_cast<int>(ref_seed_point.pw[0]),
        static_cast<int>(ref_seed_point.pw[1])
    };

    auto dic_output = Dic2DNcorrRunner(config_, base_params_).run(
        cam_ref[0], cur_imgs, refmask_ref,
        ref_seed_point, matching_params,
        ncorr_matching_path, false, true);

    if (!updateMaskAndSeedFromOutput(refmask_ref, ref_seed_point, dic_output,
                                     refmask_cur_matched, after_disp_seed_point)) {
        std::cerr << "No displacement output from matching" << std::endl;
        return false;
    }

    std::cout << "--> STEP: " << message << " done" << std::endl;
    return true;
}

bool Dic2DMatchingService::updateMaskAndSeedFromOutput(const cv::Mat& input_mask,
                                                       const SeedPoint& input_seed,
                                                       const ncorr::DIC_analysis_output& dic_output,
                                                       cv::Mat& output_mask,
                                                       SeedPoint& output_seed) const {
    if (dic_output.disps.empty()) {
        return false;
    }

    const auto& disp = dic_output.disps.front();
    const auto& u_data = disp.get_u();
    const auto& v_data = disp.get_v();
    const auto& u_array = u_data.get_array();
    const auto& v_array = v_data.get_array();
    const double scale = static_cast<double>(disp.get_scalefactor());

    cv::Mat U_mapped(u_data.data_height(), u_data.data_width(), CV_64F);
    cv::Mat V_mapped(v_data.data_height(), v_data.data_width(), CV_64F);
    for (size_t y = 0; y < static_cast<size_t>(u_data.data_height()); ++y) {
        for (size_t x = 0; x < static_cast<size_t>(u_data.data_width()); ++x) {
            U_mapped.at<double>(static_cast<int>(y), static_cast<int>(x)) = u_array(y, x) / scale;
            V_mapped.at<double>(static_cast<int>(y), static_cast<int>(x)) = v_array(y, x) / scale;
        }
    }

    output_seed.sw = ROIManager::mapPointCoordinate(input_seed.sw, U_mapped, V_mapped);
    output_seed.pw = ROIManager::mapSubset2Pixel(output_seed.sw, static_cast<int>(scale - 1.0));

    output_mask = input_mask.clone();
    try {
        ncorr::ROI2D roi_current = ROIManager::matToNcorrROI(input_mask);
        ncorr::ROI2D roi_updated = ncorr::update(
            roi_current, disp, ncorr::INTERP::CUBIC_KEYS, ncorr::ROI_UPDATE_MODE::SKIP_INVALID);
        output_mask = ROIManager::ncorrROIToMat(roi_updated);
    } catch (const std::exception& e) {
        std::cerr << "  Warning: failed to update ROI through displacement field: "
                  << e.what() << std::endl;
    }

    return true;
}

void Dic2DMatchingService::writeMatchingDebugPanel(const std::string& stage_name,
                                                   const cv::Mat& ref_img,
                                                   const cv::Mat& cur_img,
                                                   const cv::Mat& mask_before,
                                                   const cv::Mat& mask_after,
                                                   const SeedPoint& seed_before,
                                                   const SeedPoint& seed_after,
                                                   const ncorr::DIC_analysis_output& dic_output) const {
    if (!config_.debug_mode || ref_img.empty() || cur_img.empty()) {
        return;
    }

    cv::Mat ref_overlay = makeDebugCanvas(ref_img);
    cv::Mat cur_overlay = makeDebugCanvas(cur_img);
    cv::Mat before_mask_view = makeDebugCanvas(mask_before);
    cv::Mat after_mask_view = makeDebugCanvas(mask_after);

    overlayMask(ref_overlay, mask_before, cv::Scalar(60, 200, 255));
    overlayMask(cur_overlay, mask_after, cv::Scalar(60, 255, 120));
    overlayMask(before_mask_view, mask_before, cv::Scalar(0, 0, 255));
    overlayMask(after_mask_view, mask_after, cv::Scalar(0, 255, 0));

    drawSeed(ref_overlay, seed_before, cv::Scalar(0, 0, 255));
    drawSeed(cur_overlay, seed_after, cv::Scalar(0, 255, 255));
    drawSeedDisplacement(ref_overlay, seed_before, seed_after, cv::Scalar(255, 255, 0));

    const cv::Size panel_size(std::max(ref_overlay.cols, cur_overlay.cols),
                              std::max(ref_overlay.rows, cur_overlay.rows));
    cv::resize(ref_overlay, ref_overlay, panel_size, 0, 0, cv::INTER_NEAREST);
    cv::resize(cur_overlay, cur_overlay, panel_size, 0, 0, cv::INTER_NEAREST);
    cv::resize(before_mask_view, before_mask_view, panel_size, 0, 0, cv::INTER_NEAREST);
    cv::resize(after_mask_view, after_mask_view, panel_size, 0, 0, cv::INTER_NEAREST);

    cv::Mat top_row;
    cv::Mat bottom_row;
    cv::hconcat(ref_overlay, cur_overlay, top_row);
    cv::hconcat(before_mask_view, after_mask_view, bottom_row);

    cv::Mat panel;
    cv::vconcat(top_row, bottom_row, panel);

    const cv::Point text_origin(18, 30);
    const cv::Scalar text_color(255, 255, 255);
    cv::putText(panel, "Stage: " + stage_name, text_origin,
                cv::FONT_HERSHEY_SIMPLEX, 0.8, text_color, 2, cv::LINE_AA);

    const double dx = static_cast<double>(seed_after.pw[0] - seed_before.pw[0]);
    const double dy = static_cast<double>(seed_after.pw[1] - seed_before.pw[1]);
    std::ostringstream disp_text;
    disp_text << "Seed disp: (" << dx << ", " << dy << ")";
    cv::putText(panel, disp_text.str(), cv::Point(18, 62),
                cv::FONT_HERSHEY_SIMPLEX, 0.7, text_color, 2, cv::LINE_AA);

    if (!dic_output.disps.empty()) {
        const auto& cc_array = dic_output.disps.front().get_cc().get_array();
        const int seed_x = std::clamp(seed_before.sw[0], 0, static_cast<int>(cc_array.width()) - 1);
        const int seed_y = std::clamp(seed_before.sw[1], 0, static_cast<int>(cc_array.height()) - 1);
        std::ostringstream cc_text;
        cc_text << "Seed corrcoef: " << cc_array(seed_y, seed_x);
        cv::putText(panel, cc_text.str(), cv::Point(18, 94),
                    cv::FONT_HERSHEY_SIMPLEX, 0.7, text_color, 2, cv::LINE_AA);
    }

    const std::filesystem::path debug_dir = std::filesystem::path(base_params_.outputPath) / "debug_stepd";
    std::filesystem::create_directories(debug_dir);
    const std::filesystem::path debug_file = debug_dir / (sanitizeStageName(stage_name) + ".png");
    cv::imwrite(debug_file.string(), panel);
}

} // namespace cppxdic::pipeline

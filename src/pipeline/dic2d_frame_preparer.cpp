#include "cppxdic/pipeline/dic2d_frame_preparer.h"

#include "image_processor.h"
#include "utils.h"
#include <iostream>
#include <opencv2/imgcodecs.hpp>

namespace cppxdic::pipeline {

Dic2DFramePreparer::Dic2DFramePreparer(const Config& config)
    : config_(config) {}

bool Dic2DFramePreparer::importVideoFrames(const std::string& trial,
                                           int stereopair,
                                           std::vector<cv::Mat>& cam_first_raw,
                                           std::vector<cv::Mat>& cam_second_raw) const {
    std::vector<std::string> cam1_frames;
    std::vector<std::string> cam2_frames;
    const int trial_num = std::stoi(trial);

    if (!Utils::importVid(config_, trial_num, stereopair, cam1_frames, cam2_frames)) {
        std::cerr << "Failed to import video frames for trial " << trial
                  << " stereopair " << stereopair << std::endl;
        return false;
    }

    if (cam1_frames.empty() || cam2_frames.empty()) {
        std::cerr << "No frames imported for trial " << trial
                  << " stereopair " << stereopair << std::endl;
        return false;
    }

    if (cam1_frames.size() != cam2_frames.size()) {
        std::cerr << "Frame count mismatch: cam1=" << cam1_frames.size()
                  << " cam2=" << cam2_frames.size() << std::endl;
        return false;
    }

    cam_first_raw.clear();
    cam_second_raw.clear();
    cam_first_raw.reserve(cam1_frames.size());
    cam_second_raw.reserve(cam2_frames.size());

    for (size_t i = 0; i < cam1_frames.size(); ++i) {
        cv::Mat img1 = cv::imread(cam1_frames[i], cv::IMREAD_GRAYSCALE);
        if (img1.empty()) {
            std::cerr << "Failed to load frame: " << cam1_frames[i] << std::endl;
            return false;
        }

        cv::Mat img2 = cv::imread(cam2_frames[i], cv::IMREAD_GRAYSCALE);
        if (img2.empty()) {
            std::cerr << "Failed to load frame: " << cam2_frames[i] << std::endl;
            return false;
        }

        cam_first_raw.push_back(img1);
        cam_second_raw.push_back(img2);
    }

    std::cout << "Loaded " << cam_first_raw.size() << " frames for each camera" << std::endl;
    return true;
}

void Dic2DFramePreparer::trimForPhase(PreparedDic2DFrames& frames) const {
    if (config_.phase_id != "slide1") {
        return;
    }

    const size_t keep = frames.cam_first_raw.size() / 2 + 5;
    frames.cam_first_raw.resize(keep);
    frames.cam_second_raw.resize(keep);
    std::cout << "Phase 'slide1': keeping " << keep << " frames" << std::endl;
}

void Dic2DFramePreparer::performSaturation(const std::vector<cv::Mat>& cam_first_raw,
                                           const std::vector<cv::Mat>& cam_second_raw,
                                           int grayscale_limit,
                                           std::vector<cv::Mat>& cam_first_saturated,
                                           std::vector<cv::Mat>& cam_second_saturated) const {
    cam_first_saturated = ImageProcessor::saturate(cam_first_raw, grayscale_limit);
    cam_second_saturated = ImageProcessor::saturate(cam_second_raw, grayscale_limit);
}

void Dic2DFramePreparer::applyImageFiltering(const std::vector<cv::Mat>& cam_first_saturated,
                                             const std::vector<cv::Mat>& cam_second_saturated,
                                             const cv::Mat& roi_mask,
                                             std::vector<cv::Mat>& cam_first_filtered,
                                             std::vector<cv::Mat>& cam_second_filtered) const {
    std::vector<int> param_filt = {25, 300};

    auto [filtered_first, gs_bounds] = ImageProcessor::filterLikeBen(
        cam_first_saturated, roi_mask, param_filt, nullptr);

    auto [filtered_second, unused_bounds] = ImageProcessor::filterLikeBen(
        cam_second_saturated, roi_mask, param_filt, &gs_bounds);
    (void)unused_bounds;

    cam_first_filtered = std::move(filtered_first);
    cam_second_filtered = std::move(filtered_second);
}

} // namespace cppxdic::pipeline

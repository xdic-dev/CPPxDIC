#include "cppxdic/pipeline/dic2d_tracking_service.h"

#include "utils.h"
#include <filesystem>
#include <iostream>

namespace cppxdic::pipeline {

Dic2DTrackingService::Dic2DTrackingService(const Config& config,
                                           const BaseParameters& base_params,
                                           const StepParameters& tracking_cam1,
                                           const StepParameters& tracking_cam2,
                                           const Dic2DNcorrRunner& ncorr_runner)
    : config_(config),
      base_params_(base_params),
      tracking_cam1_(tracking_cam1),
      tracking_cam2_(tracking_cam2),
      ncorr_runner_(ncorr_runner) {}

bool Dic2DTrackingService::trackCamera(int tracking_number,
                                       const std::vector<cv::Mat>& cam_frames,
                                       const cv::Mat& refmask,
                                       const SeedPoint& initial_seed_point) const {
    int cam_1 = 0;
    int cam_2 = 0;
    Utils::getCamerasForPair(base_params_.stereopair, cam_1, cam_2);

    const int cam_number = tracking_number == 1 ? cam_1 : cam_2;
    std::cout << "\nTRACKING STEP " << tracking_number << std::endl;

    const std::string output_path = base_params_.outputPath + "/ncorr" + std::to_string(cam_number) + ".bin";
    if (std::filesystem::exists(output_path)) {
        std::cout << "Checkpoint found: " << output_path << std::endl;
        std::cout << "--> STEP: Ncorr " << cam_number << " loaded from checkpoint (skipped computation)" << std::endl;
        return true;
    }

    StepParameters step_params = tracking_number == 1 ? tracking_cam1_ : tracking_cam2_;
    step_params.initial_seed = {
        static_cast<int>(initial_seed_point.pw[0]),
        static_cast<int>(initial_seed_point.pw[1])
    };

    std::vector<cv::Mat> cur_frames = cam_frames;
    if (tracking_number == 2 && !cur_frames.empty()) {
        cur_frames.erase(cur_frames.begin());
    }
    if (cur_frames.empty()) {
        std::cerr << "No current frames available for tracking " << tracking_number << std::endl;
        return false;
    }

    ncorr_runner_.run(cam_frames[0], cur_frames, refmask,
                      initial_seed_point, step_params,
                      output_path, config_.parallel_processing, true);

    std::cout << "--> STEP: Ncorr " << cam_number << " done and saved to " << output_path << std::endl;
    return true;
}

bool Dic2DTrackingService::trackCamera1(const std::vector<cv::Mat>& cam_first,
                                        const cv::Mat& refmask_trial,
                                        const SeedPoint& initial_seed_point_set1) const {
    std::cout << "Performing tracking camera 1..."
              << initial_seed_point_set1.pw[0] << ","
              << initial_seed_point_set1.pw[1] << std::endl;
    return trackCamera(1, cam_first, refmask_trial, initial_seed_point_set1);
}

bool Dic2DTrackingService::trackCamera2(const std::vector<cv::Mat>& cam_second,
                                        const cv::Mat& refmask_trial_matched,
                                        const SeedPoint& initial_seed_point_set2) const {
    std::cout << "Performing tracking camera 2..."
              << initial_seed_point_set2.pw[0] << ","
              << initial_seed_point_set2.pw[1] << std::endl;
    return trackCamera(2, cam_second, refmask_trial_matched, initial_seed_point_set2);
}

} // namespace cppxdic::pipeline

#ifndef CPPXDIC_PIPELINE_DIC2D_TRACKING_SERVICE_H
#define CPPXDIC_PIPELINE_DIC2D_TRACKING_SERVICE_H

#include "config.h"
#include "cppxdic/pipeline/dic2d_ncorr_runner.h"
#include "parameters.h"
#include <opencv2/opencv.hpp>
#include <vector>

namespace cppxdic::pipeline {

class Dic2DTrackingService {
public:
    Dic2DTrackingService(const Config& config,
                         const BaseParameters& base_params,
                         const StepParameters& tracking_cam1,
                         const StepParameters& tracking_cam2,
                         const Dic2DNcorrRunner& ncorr_runner);

    bool trackCamera(int tracking_number,
                     const std::vector<cv::Mat>& cam_frames,
                     const cv::Mat& refmask,
                     const SeedPoint& initial_seed_point) const;

    bool trackCamera1(const std::vector<cv::Mat>& cam_first,
                      const cv::Mat& refmask_trial,
                      const SeedPoint& initial_seed_point_set1) const;

    bool trackCamera2(const std::vector<cv::Mat>& cam_second,
                      const cv::Mat& refmask_trial_matched,
                      const SeedPoint& initial_seed_point_set2) const;

private:
    const Config& config_;
    const BaseParameters& base_params_;
    const StepParameters& tracking_cam1_;
    const StepParameters& tracking_cam2_;
    const Dic2DNcorrRunner& ncorr_runner_;
};

} // namespace cppxdic::pipeline

#endif // CPPXDIC_PIPELINE_DIC2D_TRACKING_SERVICE_H

#ifndef CPPXDIC_PIPELINE_DIC2D_MATCHING_SERVICE_H
#define CPPXDIC_PIPELINE_DIC2D_MATCHING_SERVICE_H

#include "config.h"
#include "parameters.h"
#include <ncorr.h>
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

namespace cppxdic::pipeline {

class Dic2DMatchingService {
public:
    Dic2DMatchingService(const Config& config,
                         const BaseParameters& base_params,
                         const StepParameters& tracking_params,
                         const StepParameters& matching_params);

    bool initializeROIAndSeed(const std::vector<cv::Mat>& cam_first_saturated,
                              cv::Mat& refmask_ref,
                              cv::Mat& refmask_trial,
                              SeedPoint& ref_seed_point,
                              SeedPoint& initial_seed_point_set1) const;

    bool performStereoPairMatching(const std::vector<cv::Mat>& cam_first_saturated,
                                   const std::vector<cv::Mat>& cam_second_saturated,
                                   cv::Mat& refmask_trial,
                                   SeedPoint& initial_seed_point_set1,
                                   cv::Mat& refmask_trial_matched,
                                   SeedPoint& initial_seed_point_set2) const;

private:
    bool matchingInitialFrame(const std::vector<cv::Mat>& cam_ref,
                              const std::vector<cv::Mat>& cam_cur,
                              const cv::Mat& refmask_ref,
                              const std::string& ncorr_matching_path,
                              const std::string& message,
                              const SeedPoint& ref_seed_point,
                              cv::Mat& refmask_cur_matched,
                              SeedPoint& after_disp_seed_point) const;

    bool updateMaskAndSeedFromOutput(const cv::Mat& input_mask,
                                     const SeedPoint& input_seed,
                                     const ncorr::DIC_analysis_output& dic_output,
                                     cv::Mat& output_mask,
                                     SeedPoint& output_seed) const;

    void writeMatchingDebugPanel(const std::string& stage_name,
                                 const cv::Mat& ref_img,
                                 const cv::Mat& cur_img,
                                 const cv::Mat& mask_before,
                                 const cv::Mat& mask_after,
                                 const SeedPoint& seed_before,
                                 const SeedPoint& seed_after,
                                 const ncorr::DIC_analysis_output& dic_output) const;

    const Config& config_;
    const BaseParameters& base_params_;
    const StepParameters& tracking_params_;
    const StepParameters& matching_params_;
};

} // namespace cppxdic::pipeline

#endif // CPPXDIC_PIPELINE_DIC2D_MATCHING_SERVICE_H

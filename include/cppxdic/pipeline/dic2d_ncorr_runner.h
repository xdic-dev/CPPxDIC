#ifndef CPPXDIC_PIPELINE_DIC2D_NCORR_RUNNER_H
#define CPPXDIC_PIPELINE_DIC2D_NCORR_RUNNER_H

#include "config.h"
#include "parameters.h"
#include <ncorr.h>
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

namespace cppxdic::pipeline {

class Dic2DNcorrRunner {
public:
    Dic2DNcorrRunner(const Config& config, const BaseParameters& base_params);

    ncorr::DIC_analysis_output run(const cv::Mat& ref_img,
                                   const std::vector<cv::Mat>& cur_imgs,
                                   const cv::Mat& roi_mask,
                                   const SeedPoint& seed_point,
                                   const StepParameters& step_params,
                                   const std::string& output_path,
                                   bool go_parallel,
                                   bool use_no_update) const;

private:
    void writeMatSidecar(const std::string& output_path,
                         const cv::Mat& ref_img,
                         const std::vector<cv::Mat>& cur_imgs,
                         const cv::Mat& roi_mask,
                         const StepParameters& step_params,
                         const ncorr::DIC_analysis_output& dic_output) const;

    const Config& config_;
    const BaseParameters& base_params_;
};

} // namespace cppxdic::pipeline

#endif // CPPXDIC_PIPELINE_DIC2D_NCORR_RUNNER_H

#ifndef CPPXDIC_PIPELINE_DIC2D_FRAME_PREPARER_H
#define CPPXDIC_PIPELINE_DIC2D_FRAME_PREPARER_H

#include "config.h"
#include "parameters.h"
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

namespace cppxdic::pipeline {

struct PreparedDic2DFrames {
    std::vector<cv::Mat> cam_first_raw;
    std::vector<cv::Mat> cam_second_raw;
    std::vector<cv::Mat> cam_first_saturated;
    std::vector<cv::Mat> cam_second_saturated;
    std::vector<cv::Mat> cam_first_filtered;
    std::vector<cv::Mat> cam_second_filtered;
};

class Dic2DFramePreparer {
public:
    explicit Dic2DFramePreparer(const Config& config);

    bool importVideoFrames(const std::string& trial,
                           int stereopair,
                           std::vector<cv::Mat>& cam_first_raw,
                           std::vector<cv::Mat>& cam_second_raw) const;

    void trimForPhase(PreparedDic2DFrames& frames) const;

    void performSaturation(const std::vector<cv::Mat>& cam_first_raw,
                           const std::vector<cv::Mat>& cam_second_raw,
                           int grayscale_limit,
                           std::vector<cv::Mat>& cam_first_saturated,
                           std::vector<cv::Mat>& cam_second_saturated) const;

    void applyImageFiltering(const std::vector<cv::Mat>& cam_first_saturated,
                             const std::vector<cv::Mat>& cam_second_saturated,
                             const cv::Mat& roi_mask,
                             std::vector<cv::Mat>& cam_first_filtered,
                             std::vector<cv::Mat>& cam_second_filtered) const;

private:
    const Config& config_;
};

} // namespace cppxdic::pipeline

#endif // CPPXDIC_PIPELINE_DIC2D_FRAME_PREPARER_H

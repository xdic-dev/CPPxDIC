#ifndef CPPXDIC_IO_MAT_MAT_NCORR_WRITER_H
#define CPPXDIC_IO_MAT_MAT_NCORR_WRITER_H

#include <matio.h>
#include <ncorr.h>
#include <opencv2/opencv.hpp>
#include <map>
#include <string>
#include <vector>

namespace cppxdic::io::mat {

class MatNcorrWriter {
public:
    static bool writeMatchingFile(const std::string& filename,
                                  const cv::Mat& ref_img,
                                  const cv::Mat& cur_img,
                                  const cv::Mat& ref_roi,
                                  const cv::Mat& cur_roi,
                                  const ncorr::DIC_analysis_output& dic_output,
                                  const std::map<std::string, double>& dispinfo);

    static bool writeMatchingFile(const std::string& filename,
                                  const cv::Mat& ref_img,
                                  const cv::Mat& cur_img,
                                  const cv::Mat& ref_roi,
                                  const cv::Mat& cur_roi,
                                  const ncorr::DIC_analysis_output& dic_lagrangian,
                                  const ncorr::DIC_analysis_output& dic_eulerian,
                                  const std::map<std::string, double>& dispinfo);

    static bool writeMultiFrameNcorrFile(
        const std::string& filename,
        const cv::Mat& ref_img,
        const std::vector<cv::Mat>& cur_imgs,
        const cv::Mat& ref_roi,
        const std::vector<cv::Mat>& cur_rois,
        const std::vector<ncorr::DIC_analysis_output>& dic_outputs,
        const std::map<std::string, double>& dispinfo,
        const std::string& type_str = "load",
        const std::string& ref_name = "reference");

private:
    static cv::Mat updateRoiWithDisplacement(const cv::Mat& roi,
                                            const ncorr::Disp2D* disp,
                                            const std::string& warning_context);

    static bool aggregateOutputs(const std::vector<ncorr::DIC_analysis_output>& dic_outputs,
                                 size_t n_frames,
                                 ncorr::DIC_analysis_output& aggregated_output);
};

} // namespace cppxdic::io::mat

#endif // CPPXDIC_IO_MAT_MAT_NCORR_WRITER_H

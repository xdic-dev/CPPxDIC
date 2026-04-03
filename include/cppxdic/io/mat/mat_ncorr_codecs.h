#ifndef CPPXDIC_IO_MAT_MAT_NCORR_CODECS_H
#define CPPXDIC_IO_MAT_MAT_NCORR_CODECS_H

#include <matio.h>
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

namespace cppxdic::io::mat::ncorr_codecs {

matvar_t* createStringVar(const std::string& name, const std::string& value);

matvar_t* buildReferenceSave(const cv::Mat& ref_img, const cv::Mat& ref_roi);
matvar_t* buildReferenceSaveWithMetadata(const cv::Mat& ref_img,
                                         const cv::Mat& ref_roi,
                                         const std::string& type_str,
                                         const std::string& ref_name);
matvar_t* buildCurrentSave(const cv::Mat& cur_img, const cv::Mat& cur_roi);
matvar_t* buildCurrentSaveMultiFrame(const std::vector<cv::Mat>& cur_imgs,
                                     const std::vector<cv::Mat>& cur_rois,
                                     const std::string& type_str);
matvar_t* buildDataDicSave(matvar_t* dispinfo_var,
                           matvar_t* displacements_var,
                           bool skip_missing_displacements = false);

} // namespace cppxdic::io::mat::ncorr_codecs

#endif // CPPXDIC_IO_MAT_MAT_NCORR_CODECS_H

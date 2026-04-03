#include "cppxdic/io/mat/mat_ncorr_writer.h"
#include "cppxdic/io/mat/mat_ncorr_codecs.h"

#include "mat_writer.h"

#include <iostream>

namespace cppxdic::io::mat {

bool MatNcorrWriter::writeMatchingFile(
    const std::string& filename,
    const cv::Mat& ref_img,
    const cv::Mat& cur_img,
    const cv::Mat& ref_roi,
    const cv::Mat& cur_roi,
    const ncorr::DIC_analysis_output& dic_output,
    const std::map<std::string, double>& dispinfo) {
    matvar_t* dispinfo_var = cppxdic::MatWriter::formatDispInfo(dispinfo);
    matvar_t* displacements_var = cppxdic::MatWriter::formatDisplacements(dic_output);

    const bool success = cppxdic::MatWriter::writeDicNcorrFile(
        filename,
        ref_img,
        cur_img,
        ref_roi,
        cur_roi,
        dispinfo_var,
        displacements_var,
        dic_output);

    if (success) {
        std::cout << "Wrote MATCHING file: " << filename << std::endl;
    }

    return success;
}

bool MatNcorrWriter::writeMatchingFile(
    const std::string& filename,
    const cv::Mat& ref_img,
    const cv::Mat& cur_img,
    const cv::Mat& ref_roi,
    const cv::Mat& cur_roi,
    const ncorr::DIC_analysis_output& dic_lagrangian,
    const ncorr::DIC_analysis_output& dic_eulerian,
    const std::map<std::string, double>& dispinfo) {
    matvar_t* dispinfo_var = cppxdic::MatWriter::formatDispInfo(dispinfo);
    matvar_t* displacements_var = cppxdic::MatWriter::formatDisplacements(
        dic_lagrangian, dic_eulerian);

    const bool success = cppxdic::MatWriter::writeDicNcorrFile(
        filename,
        ref_img,
        cur_img,
        ref_roi,
        cur_roi,
        dispinfo_var,
        displacements_var,
        dic_lagrangian);

    if (success) {
        std::cout << "Wrote MATCHING file with both perspectives: " << filename << std::endl;
    }

    return success;
}

bool MatNcorrWriter::writeMultiFrameNcorrFile(
    const std::string& filename,
    const cv::Mat& ref_img,
    const std::vector<cv::Mat>& cur_imgs,
    const cv::Mat& ref_roi,
    const std::vector<cv::Mat>& cur_rois,
    const std::vector<ncorr::DIC_analysis_output>& dic_outputs,
    const std::map<std::string, double>& dispinfo,
    const std::string& type_str,
    const std::string& ref_name) {
    if (cur_imgs.empty()) {
        std::cerr << "Error: No current images provided" << std::endl;
        return false;
    }

    const size_t n_frames = cur_imgs.size();
    std::cout << "Writing multi-frame ncorr file with " << n_frames << " frames..." << std::endl;

    ncorr::DIC_analysis_output aggregated_output;
    const bool have_aggregated_output = aggregateOutputs(
        dic_outputs, n_frames, aggregated_output);

    mat_t* matfp = cppxdic::MatWriter::createMatFileHDF5(filename);
    if (!matfp) {
        std::cerr << "Failed to create MAT file: " << filename << std::endl;
        return false;
    }

    std::vector<cv::Mat> updated_rois;
    updated_rois.reserve(n_frames);
    for (size_t i = 0; i < n_frames; ++i) {
        cv::Mat roi_to_use = (i < cur_rois.size()) ? cur_rois[i] : ref_roi.clone();

        const ncorr::Disp2D* roi_disp = nullptr;
        if (have_aggregated_output && i < aggregated_output.disps.size()) {
            roi_disp = &aggregated_output.disps[i];
        } else if (i < dic_outputs.size() && !dic_outputs[i].disps.empty()) {
            roi_disp = &dic_outputs[i].disps.front();
        }

        roi_to_use = updateRoiWithDisplacement(
            roi_to_use,
            roi_disp,
            "frame " + std::to_string(i));
        updated_rois.push_back(std::move(roi_to_use));
    }
    matvar_t* reference_save =
        ncorr_codecs::buildReferenceSaveWithMetadata(ref_img, ref_roi, type_str, ref_name);
    matvar_t* current_save =
        ncorr_codecs::buildCurrentSaveMultiFrame(cur_imgs, updated_rois, type_str);

    matvar_t* dispinfo_var = cppxdic::MatWriter::formatDispInfo(dispinfo);
    matvar_t* displacements_var = nullptr;
    if (have_aggregated_output) {
        displacements_var = cppxdic::MatWriter::formatDisplacements(aggregated_output);
    }
    matvar_t* data_dic_save =
        ncorr_codecs::buildDataDicSave(dispinfo_var, displacements_var, true);

    Mat_VarWrite(matfp, reference_save, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp, current_save, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp, data_dic_save, MAT_COMPRESSION_NONE);

    Mat_VarFree(reference_save);
    Mat_VarFree(current_save);
    Mat_VarFree(data_dic_save);
    Mat_Close(matfp);

    std::cout << "Successfully wrote multi-frame ncorr file: " << filename << std::endl;
    return true;
}

cv::Mat MatNcorrWriter::updateRoiWithDisplacement(const cv::Mat& roi,
                                                  const ncorr::Disp2D* disp,
                                                  const std::string& warning_context) {
    cv::Mat updated_roi = roi.clone();
    if (!disp) {
        return updated_roi;
    }

    try {
        ncorr::ROI2D roi_current = cppxdic::MatWriter::convertMatToROI2D(roi);
        ncorr::ROI2D roi_updated = ncorr::update(
            roi_current,
            *disp,
            ncorr::INTERP::CUBIC_KEYS,
            ncorr::ROI_UPDATE_MODE::SKIP_INVALID);
        updated_roi = cppxdic::MatWriter::convertROI2DToMat(roi_updated);
    } catch (const std::exception& e) {
        std::cerr << "  Warning: ROI update failed";
        if (!warning_context.empty()) {
            std::cerr << " for " << warning_context;
        }
        std::cerr << ": " << e.what() << std::endl;
    }

    return updated_roi;
}

bool MatNcorrWriter::aggregateOutputs(const std::vector<ncorr::DIC_analysis_output>& dic_outputs,
                                      size_t n_frames,
                                      ncorr::DIC_analysis_output& aggregated_output) {
    if (dic_outputs.size() == 1 && dic_outputs[0].disps.size() == n_frames) {
        aggregated_output = dic_outputs[0];
        return true;
    }

    if (dic_outputs.size() != n_frames || dic_outputs.empty()) {
        return false;
    }

    std::vector<ncorr::Disp2D> aggregated_disps;
    aggregated_disps.reserve(n_frames);
    for (const auto& output : dic_outputs) {
        if (output.disps.empty()) {
            return false;
        }
        aggregated_disps.push_back(output.disps.front());
    }

    aggregated_output = ncorr::DIC_analysis_output(
        aggregated_disps,
        dic_outputs.front().perspective_type,
        dic_outputs.front().units,
        dic_outputs.front().units_per_pixel);
    return true;
}

} // namespace cppxdic::io::mat

namespace cppxdic {

bool MatWriter::writeDicNcorrFile(const std::string& filename,
                                  const cv::Mat& ref_img,
                                  const cv::Mat& cur_img,
                                  const cv::Mat& ref_roi,
                                  const cv::Mat& cur_roi,
                                  matvar_t* dispinfo_var,
                                  matvar_t* displacements_var,
                                  const ncorr::DIC_analysis_output& dic_output) {
    mat_t* matfp = createMatFileHDF5(filename);
    if (!matfp) {
        std::cerr << "Failed to create MAT file: " << filename << std::endl;
        return false;
    }

    matvar_t* reference_save = io::mat::ncorr_codecs::buildReferenceSave(ref_img, ref_roi);

    cv::Mat cur_roi_updated = cur_roi.clone();
    if (!dic_output.disps.empty()) {
        try {
            ncorr::ROI2D roi_current = convertMatToROI2D(cur_roi);
            ncorr::ROI2D roi_updated = ncorr::update(
                roi_current,
                dic_output.disps.front(),
                ncorr::INTERP::CUBIC_KEYS,
                ncorr::ROI_UPDATE_MODE::SKIP_INVALID);
            cur_roi_updated = convertROI2DToMat(roi_updated);
        } catch (const std::exception& e) {
            std::cerr << "  Warning: ROI update failed: " << e.what() << std::endl;
            std::cerr << "  Using original ROI instead" << std::endl;
        }
    }

    matvar_t* current_save = io::mat::ncorr_codecs::buildCurrentSave(cur_img, cur_roi_updated);
    matvar_t* data_dic_save =
        io::mat::ncorr_codecs::buildDataDicSave(dispinfo_var, displacements_var);

    Mat_VarWrite(matfp, reference_save, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp, current_save, MAT_COMPRESSION_NONE);
    Mat_VarWrite(matfp, data_dic_save, MAT_COMPRESSION_NONE);

    Mat_VarFree(reference_save);
    Mat_VarFree(current_save);
    Mat_VarFree(data_dic_save);
    Mat_Close(matfp);

    return true;
}

} // namespace cppxdic

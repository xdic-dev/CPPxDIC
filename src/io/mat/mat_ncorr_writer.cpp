#include "cppxdic/io/mat/mat_ncorr_writer.h"

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

    const std::vector<std::string> ref_fields = {"gs", "name", "path", "roi", "type"};
    matvar_t* reference_save = cppxdic::MatWriter::createStructVariable("reference_save", ref_fields);

    cppxdic::MatWriter::writeMatVariable(matfp, "ref_gs_temp", ref_img);
    matvar_t* ref_gs = Mat_VarRead(matfp, "ref_gs_temp");
    cppxdic::MatWriter::addFieldToStruct(reference_save, "gs", ref_gs, 0);

    const std::vector<std::string> roi_fields = {"mask"};
    matvar_t* ref_roi_struct = cppxdic::MatWriter::createStructVariable("roi", roi_fields);
    cppxdic::MatWriter::writeMatVariable(matfp, "ref_roi_temp", ref_roi);
    matvar_t* ref_roi_mask = Mat_VarRead(matfp, "ref_roi_temp");
    cppxdic::MatWriter::addFieldToStruct(ref_roi_struct, "mask", ref_roi_mask, 0);
    cppxdic::MatWriter::addFieldToStruct(reference_save, "roi", ref_roi_struct, 0);

    matvar_t* current_save = cppxdic::MatWriter::createStructVariable("current_save", ref_fields);
    std::vector<size_t> cell_dims = {n_frames, 1};

    matvar_t* gs_cell = Mat_VarCreate("gs", MAT_C_CELL, MAT_T_CELL, 2, cell_dims.data(), nullptr, 0);
    for (size_t i = 0; i < n_frames; ++i) {
        const std::string temp_name = "cur_gs_temp_" + std::to_string(i);
        cppxdic::MatWriter::writeMatVariable(matfp, temp_name, cur_imgs[i]);
        matvar_t* img_var = Mat_VarRead(matfp, temp_name.c_str());
        Mat_VarSetCell(gs_cell, i, img_var);
    }
    cppxdic::MatWriter::addFieldToStruct(current_save, "gs", gs_cell, 0);

    matvar_t* roi_cell = Mat_VarCreate("roi", MAT_C_CELL, MAT_T_CELL, 2, cell_dims.data(), nullptr, 0);
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

        matvar_t* frame_roi_struct = cppxdic::MatWriter::createStructVariable("roi", roi_fields);
        const std::string temp_roi_name = "cur_roi_temp_" + std::to_string(i);
        cppxdic::MatWriter::writeMatVariable(matfp, temp_roi_name, roi_to_use);
        matvar_t* roi_mask_var = Mat_VarRead(matfp, temp_roi_name.c_str());
        cppxdic::MatWriter::addFieldToStruct(frame_roi_struct, "mask", roi_mask_var, 0);
        Mat_VarSetCell(roi_cell, i, frame_roi_struct);
    }
    cppxdic::MatWriter::addFieldToStruct(current_save, "roi", roi_cell, 0);

    matvar_t* name_cell = Mat_VarCreate("name", MAT_C_CELL, MAT_T_CELL, 2, cell_dims.data(), nullptr, 0);
    for (size_t i = 0; i < n_frames; ++i) {
        matvar_t* name_var = createStringVar("", "current_" + std::to_string(i + 1));
        Mat_VarSetCell(name_cell, i, name_var);
    }
    cppxdic::MatWriter::addFieldToStruct(current_save, "name", name_cell, 0);

    matvar_t* path_cell = Mat_VarCreate("path", MAT_C_CELL, MAT_T_CELL, 2, cell_dims.data(), nullptr, 0);
    for (size_t i = 0; i < n_frames; ++i) {
        Mat_VarSetCell(path_cell, i, nullptr);
    }
    cppxdic::MatWriter::addFieldToStruct(current_save, "path", path_cell, 0);

    matvar_t* type_cell = Mat_VarCreate("type", MAT_C_CELL, MAT_T_CELL, 2, cell_dims.data(), nullptr, 0);
    for (size_t i = 0; i < n_frames; ++i) {
        matvar_t* type_var = createStringVar("", type_str);
        Mat_VarSetCell(type_cell, i, type_var);
    }
    cppxdic::MatWriter::addFieldToStruct(current_save, "type", type_cell, 0);

    matvar_t* ref_name_var = createStringVar("name", ref_name);
    cppxdic::MatWriter::addFieldToStruct(reference_save, "name", ref_name_var, 0);

    matvar_t* ref_type_var = createStringVar("type", type_str);
    cppxdic::MatWriter::addFieldToStruct(reference_save, "type", ref_type_var, 0);
    cppxdic::MatWriter::addFieldToStruct(reference_save, "path", nullptr, 0);

    matvar_t* dispinfo_var = cppxdic::MatWriter::formatDispInfo(dispinfo);
    matvar_t* displacements_var = nullptr;
    if (have_aggregated_output) {
        displacements_var = cppxdic::MatWriter::formatDisplacements(aggregated_output);
    }

    const std::vector<std::string> data_fields = {"dispinfo", "displacements", "straininfo", "strains"};
    matvar_t* data_dic_save = cppxdic::MatWriter::createStructVariable("data_dic_save", data_fields);
    cppxdic::MatWriter::addFieldToStruct(data_dic_save, "dispinfo", dispinfo_var, 0);
    if (displacements_var) {
        cppxdic::MatWriter::addFieldToStruct(data_dic_save, "displacements", displacements_var, 0);
    }
    addPlaceholderFields(data_dic_save);

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

void MatNcorrWriter::addPlaceholderFields(matvar_t* data_dic_save) {
    const std::vector<std::string> straininfo_fields = {"radius", "subsettrunc"};
    matvar_t* straininfo_var = cppxdic::MatWriter::createStructVariable("straininfo", straininfo_fields);
    cppxdic::MatWriter::addFieldToStruct(data_dic_save, "straininfo", straininfo_var, 0);

    const std::vector<std::string> strains_fields = {
        "plot_exx_ref_formatted",
        "plot_exy_ref_formatted",
        "plot_eyy_ref_formatted",
        "roi_ref_formatted",
        "plot_exx_cur_formatted",
        "plot_exy_cur_formatted",
        "plot_eyy_cur_formatted",
        "roi_cur_formatted"
    };
    matvar_t* strains_var = cppxdic::MatWriter::createStructVariable("strains", strains_fields);
    cppxdic::MatWriter::addFieldToStruct(data_dic_save, "strains", strains_var, 0);
}

matvar_t* MatNcorrWriter::createStringVar(const std::string& name,
                                          const std::string& value) {
    const std::vector<size_t> dims = {1, value.length()};
    return Mat_VarCreate(
        name.empty() ? nullptr : name.c_str(),
        MAT_C_CHAR,
        MAT_T_UINT8,
        2,
        dims.data(),
        const_cast<char*>(value.c_str()),
        0);
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

    const std::vector<std::string> ref_fields = {"gs", "name", "path", "roi", "type"};
    matvar_t* reference_save = createStructVariable("reference_save", ref_fields);

    writeMatVariable(matfp, "ref_gs_temp", ref_img);
    matvar_t* ref_gs = Mat_VarRead(matfp, "ref_gs_temp");
    addFieldToStruct(reference_save, "gs", ref_gs, 0);

    const std::vector<std::string> roi_fields = {"mask"};
    matvar_t* ref_roi_struct = createStructVariable("roi", roi_fields);
    writeMatVariable(matfp, "ref_roi_temp", ref_roi);
    matvar_t* ref_roi_mask = Mat_VarRead(matfp, "ref_roi_temp");
    addFieldToStruct(ref_roi_struct, "mask", ref_roi_mask, 0);
    addFieldToStruct(reference_save, "roi", ref_roi_struct, 0);

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

    matvar_t* current_save = createStructVariable("current_save", ref_fields);
    writeMatVariable(matfp, "cur_gs_temp", cur_img);
    matvar_t* cur_gs = Mat_VarRead(matfp, "cur_gs_temp");
    addFieldToStruct(current_save, "gs", cur_gs, 0);

    matvar_t* cur_roi_struct = createStructVariable("roi", roi_fields);
    writeMatVariable(matfp, "cur_roi_temp", cur_roi_updated);
    matvar_t* cur_roi_mask = Mat_VarRead(matfp, "cur_roi_temp");
    addFieldToStruct(cur_roi_struct, "mask", cur_roi_mask, 0);
    addFieldToStruct(current_save, "roi", cur_roi_struct, 0);

    const std::vector<std::string> data_fields = {"dispinfo", "displacements", "straininfo", "strains"};
    matvar_t* data_dic_save = createStructVariable("data_dic_save", data_fields);
    addFieldToStruct(data_dic_save, "dispinfo", dispinfo_var, 0);
    addFieldToStruct(data_dic_save, "displacements", displacements_var, 0);

    const std::vector<std::string> straininfo_fields = {"radius", "subsettrunc"};
    matvar_t* straininfo_var = createStructVariable("straininfo", straininfo_fields);
    addFieldToStruct(data_dic_save, "straininfo", straininfo_var, 0);

    const std::vector<std::string> strains_fields = {
        "plot_exx_ref_formatted",
        "plot_exy_ref_formatted",
        "plot_eyy_ref_formatted",
        "roi_ref_formatted",
        "plot_exx_cur_formatted",
        "plot_exy_cur_formatted",
        "plot_eyy_cur_formatted",
        "roi_cur_formatted"
    };
    matvar_t* strains_var = createStructVariable("strains", strains_fields);
    addFieldToStruct(data_dic_save, "strains", strains_var, 0);

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

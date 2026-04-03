#include "cppxdic/io/mat/mat_ncorr_codecs.h"

#include "cppxdic/io/mat/mat_result_codecs.h"
#include "cppxdic/io/mat/matio_helpers.h"

#include <iostream>

namespace {

std::vector<std::string> saveStructFields() {
    return {"gs", "name", "path", "roi", "type"};
}

matvar_t* buildRoiStruct(const cv::Mat& roi) {
    const std::vector<std::string> roi_fields = {"mask"};
    matvar_t* roi_struct = cppxdic::io::mat::createStructVariable("roi", roi_fields);
    if (!roi_struct) {
        return nullptr;
    }

    matvar_t* roi_mask = cppxdic::io::mat::codecs::createCvMatVar("mask", roi);
    if (roi_mask) {
        cppxdic::io::mat::addFieldToStruct(roi_struct, "mask", roi_mask, 0);
    }

    return roi_struct;
}

void addPlaceholderFields(matvar_t* data_dic_save) {
    const std::vector<std::string> straininfo_fields = {"radius", "subsettrunc"};
    matvar_t* straininfo_var =
        cppxdic::io::mat::createStructVariable("straininfo", straininfo_fields);
    cppxdic::io::mat::addFieldToStruct(data_dic_save, "straininfo", straininfo_var, 0);

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
    matvar_t* strains_var =
        cppxdic::io::mat::createStructVariable("strains", strains_fields);
    cppxdic::io::mat::addFieldToStruct(data_dic_save, "strains", strains_var, 0);
}

} // namespace

namespace cppxdic::io::mat::ncorr_codecs {

matvar_t* createStringVar(const std::string& name, const std::string& value) {
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

matvar_t* buildReferenceSave(const cv::Mat& ref_img, const cv::Mat& ref_roi) {
    matvar_t* reference_save = cppxdic::io::mat::createStructVariable("reference_save", saveStructFields());
    if (!reference_save) {
        return nullptr;
    }

    matvar_t* ref_gs = cppxdic::io::mat::codecs::createCvMatVar("gs", ref_img);
    if (ref_gs) {
        cppxdic::io::mat::addFieldToStruct(reference_save, "gs", ref_gs, 0);
    }

    matvar_t* ref_roi_struct = buildRoiStruct(ref_roi);
    if (ref_roi_struct) {
        cppxdic::io::mat::addFieldToStruct(reference_save, "roi", ref_roi_struct, 0);
    }

    return reference_save;
}

matvar_t* buildReferenceSaveWithMetadata(const cv::Mat& ref_img,
                                         const cv::Mat& ref_roi,
                                         const std::string& type_str,
                                         const std::string& ref_name) {
    matvar_t* reference_save = buildReferenceSave(ref_img, ref_roi);
    if (!reference_save) {
        return nullptr;
    }

    matvar_t* ref_name_var = createStringVar("name", ref_name);
    cppxdic::io::mat::addFieldToStruct(reference_save, "name", ref_name_var, 0);

    matvar_t* ref_type_var = createStringVar("type", type_str);
    cppxdic::io::mat::addFieldToStruct(reference_save, "type", ref_type_var, 0);

    return reference_save;
}

matvar_t* buildCurrentSave(const cv::Mat& cur_img, const cv::Mat& cur_roi) {
    matvar_t* current_save = cppxdic::io::mat::createStructVariable("current_save", saveStructFields());
    if (!current_save) {
        return nullptr;
    }

    matvar_t* cur_gs = cppxdic::io::mat::codecs::createCvMatVar("gs", cur_img);
    if (cur_gs) {
        cppxdic::io::mat::addFieldToStruct(current_save, "gs", cur_gs, 0);
    }

    matvar_t* cur_roi_struct = buildRoiStruct(cur_roi);
    if (cur_roi_struct) {
        cppxdic::io::mat::addFieldToStruct(current_save, "roi", cur_roi_struct, 0);
    }

    return current_save;
}

matvar_t* buildCurrentSaveMultiFrame(const std::vector<cv::Mat>& cur_imgs,
                                     const std::vector<cv::Mat>& cur_rois,
                                     const std::string& type_str) {
    matvar_t* current_save = cppxdic::io::mat::createStructVariable("current_save", saveStructFields());
    if (!current_save) {
        return nullptr;
    }

    const std::vector<size_t> cell_dims = {cur_imgs.size(), 1};

    matvar_t* gs_cell = Mat_VarCreate("gs", MAT_C_CELL, MAT_T_CELL, 2, cell_dims.data(), nullptr, 0);
    for (size_t i = 0; i < cur_imgs.size(); ++i) {
        matvar_t* img_var = cppxdic::io::mat::codecs::createCvMatVar(nullptr, cur_imgs[i]);
        Mat_VarSetCell(gs_cell, i, img_var);
    }
    cppxdic::io::mat::addFieldToStruct(current_save, "gs", gs_cell, 0);

    matvar_t* roi_cell = Mat_VarCreate("roi", MAT_C_CELL, MAT_T_CELL, 2, cell_dims.data(), nullptr, 0);
    for (size_t i = 0; i < cur_rois.size(); ++i) {
        Mat_VarSetCell(roi_cell, i, buildRoiStruct(cur_rois[i]));
    }
    cppxdic::io::mat::addFieldToStruct(current_save, "roi", roi_cell, 0);

    matvar_t* name_cell = Mat_VarCreate("name", MAT_C_CELL, MAT_T_CELL, 2, cell_dims.data(), nullptr, 0);
    for (size_t i = 0; i < cur_imgs.size(); ++i) {
        Mat_VarSetCell(name_cell, i, createStringVar("", "current_" + std::to_string(i + 1)));
    }
    cppxdic::io::mat::addFieldToStruct(current_save, "name", name_cell, 0);

    matvar_t* path_cell = Mat_VarCreate("path", MAT_C_CELL, MAT_T_CELL, 2, cell_dims.data(), nullptr, 0);
    for (size_t i = 0; i < cur_imgs.size(); ++i) {
        Mat_VarSetCell(path_cell, i, nullptr);
    }
    cppxdic::io::mat::addFieldToStruct(current_save, "path", path_cell, 0);

    matvar_t* type_cell = Mat_VarCreate("type", MAT_C_CELL, MAT_T_CELL, 2, cell_dims.data(), nullptr, 0);
    for (size_t i = 0; i < cur_imgs.size(); ++i) {
        Mat_VarSetCell(type_cell, i, createStringVar("", type_str));
    }
    cppxdic::io::mat::addFieldToStruct(current_save, "type", type_cell, 0);

    return current_save;
}

matvar_t* buildDataDicSave(matvar_t* dispinfo_var,
                           matvar_t* displacements_var,
                           bool skip_missing_displacements) {
    const std::vector<std::string> data_fields = {"dispinfo", "displacements", "straininfo", "strains"};
    matvar_t* data_dic_save = cppxdic::io::mat::createStructVariable("data_dic_save", data_fields);
    if (!data_dic_save) {
        return nullptr;
    }

    cppxdic::io::mat::addFieldToStruct(data_dic_save, "dispinfo", dispinfo_var, 0);
    if (skip_missing_displacements) {
        if (displacements_var) {
            cppxdic::io::mat::addFieldToStruct(data_dic_save, "displacements", displacements_var, 0);
        }
    } else {
        cppxdic::io::mat::addFieldToStruct(data_dic_save, "displacements", displacements_var, 0);
    }

    addPlaceholderFields(data_dic_save);
    return data_dic_save;
}

} // namespace cppxdic::io::mat::ncorr_codecs

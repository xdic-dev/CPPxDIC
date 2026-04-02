#include "cppxdic/io/mat/mat_results_writer.h"

#include "mat_writer.h"

#include <Eigen/Dense>

#include <array>
#include <iostream>
#include <vector>

namespace {

constexpr std::array<size_t, 2> kScalarDims = {1, 1};

matvar_t* createDoubleScalarVar(const char* name, double value) {
    return Mat_VarCreate(name, MAT_C_DOUBLE, MAT_T_DOUBLE, 2, kScalarDims.data(), &value, 0);
}

matvar_t* createInt32ScalarVar(const char* name, int value) {
    return Mat_VarCreate(name, MAT_C_INT32, MAT_T_INT32, 2, kScalarDims.data(), &value, 0);
}

matvar_t* createStringVar(const char* name, const std::string& value) {
    std::vector<size_t> dims = {1, value.length()};
    return Mat_VarCreate(name, MAT_C_CHAR, MAT_T_UINT8, 2, dims.data(),
                         const_cast<char*>(value.c_str()), 0);
}

matvar_t* createStructVariableLocal(const std::string& struct_name,
                                    const std::vector<std::string>& field_names) {
    std::vector<size_t> dims = {1, 1};
    std::vector<const char*> c_field_names;
    c_field_names.reserve(field_names.size());
    for (const auto& field_name : field_names) {
        c_field_names.push_back(field_name.c_str());
    }

    return Mat_VarCreateStruct(struct_name.c_str(), 2, dims.data(),
                               c_field_names.data(), field_names.size());
}

matvar_t* createCvMatVar(const char* name, const cv::Mat& mat) {
    if (mat.empty()) {
        return nullptr;
    }

    std::vector<size_t> dims = {static_cast<size_t>(mat.rows), static_cast<size_t>(mat.cols)};
    matio_types mat_type;
    matio_classes mat_class;

    switch (mat.type()) {
        case CV_8UC1:
            mat_type = MAT_T_UINT8;
            mat_class = MAT_C_UINT8;
            break;
        case CV_16UC1:
            mat_type = MAT_T_UINT16;
            mat_class = MAT_C_UINT16;
            break;
        case CV_32FC1:
            mat_type = MAT_T_SINGLE;
            mat_class = MAT_C_SINGLE;
            break;
        case CV_64FC1:
            mat_type = MAT_T_DOUBLE;
            mat_class = MAT_C_DOUBLE;
            break;
        default:
            std::cerr << "Unsupported Mat type for MAT struct embedding: " << mat.type() << std::endl;
            return nullptr;
    }

    cv::Mat transposed = mat.t();
    return Mat_VarCreate(name, mat_class, mat_type, 2, dims.data(), transposed.data, 0);
}

matvar_t* createIndexedFacesVar(const char* name, const std::vector<int>& faces) {
    if (faces.empty()) {
        return nullptr;
    }

    const size_t n_faces = faces.size() / 3;
    std::vector<size_t> dims = {n_faces, 3};
    std::vector<double> faces_dbl(n_faces * 3);
    for (size_t i = 0; i < n_faces; ++i) {
        faces_dbl[i] = static_cast<double>(faces[i * 3 + 0] + 1);
        faces_dbl[i + n_faces] = static_cast<double>(faces[i * 3 + 1] + 1);
        faces_dbl[i + 2 * n_faces] = static_cast<double>(faces[i * 3 + 2] + 1);
    }
    return Mat_VarCreate(name, MAT_C_DOUBLE, MAT_T_DOUBLE, 2, dims.data(), faces_dbl.data(), 0);
}

matvar_t* createDoubleArrayVar(const char* name,
                               const std::vector<double>& values,
                               const std::vector<size_t>& dims) {
    if (values.empty()) {
        return nullptr;
    }
    return Mat_VarCreate(name, MAT_C_DOUBLE, MAT_T_DOUBLE, static_cast<int>(dims.size()),
                         dims.data(), const_cast<double*>(values.data()), 0);
}

matvar_t* buildPoints2DCell(const char* name, const std::vector<::cppxdic::Points2D>& points) {
    std::vector<size_t> cell_dims = {1, points.size()};
    matvar_t* points_cell = Mat_VarCreate(name, MAT_C_CELL, MAT_T_CELL, 2, cell_dims.data(), nullptr, 0);
    if (!points_cell) {
        return nullptr;
    }

    for (size_t i = 0; i < points.size(); ++i) {
        const auto& pts = points[i];
        const size_t n_points = pts.x.size();
        if (n_points == 0) {
            continue;
        }

        std::vector<double> xy_data(n_points * 2);
        for (size_t j = 0; j < n_points; ++j) {
            xy_data[j] = pts.x[j];
            xy_data[j + n_points] = pts.y[j];
        }
        std::vector<size_t> dims = {n_points, 2};
        Mat_VarSetCell(points_cell, i,
                       Mat_VarCreate(nullptr, MAT_C_DOUBLE, MAT_T_DOUBLE, 2,
                                     dims.data(), xy_data.data(), 0));
    }

    return points_cell;
}

matvar_t* buildPoints3DCell(const char* name, const std::vector<::cppxdic::Points3D>& points) {
    std::vector<size_t> cell_dims = {1, points.size()};
    matvar_t* points_cell = Mat_VarCreate(name, MAT_C_CELL, MAT_T_CELL, 2, cell_dims.data(), nullptr, 0);
    if (!points_cell) {
        return nullptr;
    }

    for (size_t i = 0; i < points.size(); ++i) {
        const auto& pts = points[i];
        const size_t n_points = pts.x.size();
        std::vector<size_t> dims = {n_points, 3};
        std::vector<double> pts_data(n_points * 3);
        for (size_t p = 0; p < n_points; ++p) {
            pts_data[p] = pts.x[p];
            pts_data[p + n_points] = pts.y[p];
            pts_data[p + 2 * n_points] = pts.z[p];
        }
        Mat_VarSetCell(points_cell, i,
                       Mat_VarCreate(nullptr, MAT_C_DOUBLE, MAT_T_DOUBLE, 2,
                                     dims.data(), pts_data.data(), 0));
    }

    return points_cell;
}

matvar_t* buildScalarCell(const char* name,
                          const std::vector<std::vector<double>>& data,
                          bool row_vector_cells = true) {
    std::vector<size_t> cell_dims = row_vector_cells
        ? std::vector<size_t>{1, data.size()}
        : std::vector<size_t>{data.size(), 1};
    matvar_t* cell = Mat_VarCreate(name, MAT_C_CELL, MAT_T_CELL, 2, cell_dims.data(), nullptr, 0);
    if (!cell) {
        return nullptr;
    }

    for (size_t i = 0; i < data.size(); ++i) {
        if (data[i].empty()) {
            continue;
        }
        std::vector<size_t> dims = {data[i].size(), 1};
        Mat_VarSetCell(cell, i,
                       Mat_VarCreate(nullptr, MAT_C_DOUBLE, MAT_T_DOUBLE, 2,
                                     dims.data(), const_cast<double*>(data[i].data()), 0));
    }
    return cell;
}

matvar_t* buildFaceTripletCell(const char* name, const std::vector<std::vector<double>>& data) {
    std::vector<size_t> cell_dims = {1, data.size()};
    matvar_t* cell = Mat_VarCreate(name, MAT_C_CELL, MAT_T_CELL, 2, cell_dims.data(), nullptr, 0);
    if (!cell) {
        return nullptr;
    }

    for (size_t i = 0; i < data.size(); ++i) {
        const auto& triplets = data[i];
        if (triplets.empty()) {
            continue;
        }
        const size_t n_rows = triplets.size() / 3;
        std::vector<size_t> dims = {n_rows, 3};
        std::vector<double> matrix(n_rows * 3);
        for (size_t row = 0; row < n_rows; ++row) {
            matrix[row] = triplets[row * 3 + 0];
            matrix[row + n_rows] = triplets[row * 3 + 1];
            matrix[row + 2 * n_rows] = triplets[row * 3 + 2];
        }
        Mat_VarSetCell(cell, i,
                       Mat_VarCreate(nullptr, MAT_C_DOUBLE, MAT_T_DOUBLE, 2,
                                     dims.data(), matrix.data(), 0));
    }
    return cell;
}

matvar_t* buildDispStruct(const ::cppxdic::DispData& disp) {
    std::vector<std::string> disp_fields = {"DispVec", "DispMgn"};
    matvar_t* disp_struct = createStructVariableLocal("Disp", disp_fields);
    if (!disp_struct) {
        return nullptr;
    }

    if (!disp.DispVec.empty()) {
        std::vector<size_t> cell_dims = {1, disp.DispVec.size()};
        matvar_t* disp_vec_cell =
            Mat_VarCreate("DispVec", MAT_C_CELL, MAT_T_CELL, 2, cell_dims.data(), nullptr, 0);
        for (size_t i = 0; i < disp.DispVec.size(); ++i) {
            const auto& vec = disp.DispVec[i];
            if (vec.empty()) {
                continue;
            }
            const size_t n_rows = vec.size() / 3;
            std::vector<size_t> dims = {n_rows, 3};
            std::vector<double> matrix(n_rows * 3);
            for (size_t row = 0; row < n_rows; ++row) {
                matrix[row] = vec[row * 3 + 0];
                matrix[row + n_rows] = vec[row * 3 + 1];
                matrix[row + 2 * n_rows] = vec[row * 3 + 2];
            }
            Mat_VarSetCell(disp_vec_cell, i,
                           Mat_VarCreate(nullptr, MAT_C_DOUBLE, MAT_T_DOUBLE, 2,
                                         dims.data(), matrix.data(), 0));
        }
        Mat_VarSetStructFieldByName(disp_struct, "DispVec", 0, disp_vec_cell);
    }

    if (!disp.DispMgn.empty()) {
        matvar_t* disp_mgn_cell = buildScalarCell("DispMgn", disp.DispMgn);
        if (disp_mgn_cell) {
            Mat_VarSetStructFieldByName(disp_struct, "DispMgn", 0, disp_mgn_cell);
        }
    }

    return disp_struct;
}

matvar_t* buildNcorrInfoStruct(const ::cppxdic::DICInfo& info) {
    std::vector<std::string> ncorr_fields = {
        "cutoff_corrcoef", "cutoff_diffnorm", "cutoff_iteration",
        "imgcorr", "lenscoef", "pixtounits", "radius", "spacing",
        "stepanalysis", "subsettrunc", "total_threads", "type", "units"
    };
    matvar_t* ncorr_struct = createStructVariableLocal("ncorrInfo", ncorr_fields);
    if (!ncorr_struct) {
        return nullptr;
    }

    if (!info.cutoff_corrcoef.empty()) {
        std::vector<size_t> dims = {1, info.cutoff_corrcoef.size()};
        Mat_VarSetStructFieldByName(
            ncorr_struct, "cutoff_corrcoef", 0,
            Mat_VarCreate("cutoff_corrcoef", MAT_C_DOUBLE, MAT_T_DOUBLE, 2,
                          dims.data(), const_cast<double*>(info.cutoff_corrcoef.data()), 0));
    }

    Mat_VarSetStructFieldByName(ncorr_struct, "cutoff_diffnorm", 0,
                                createDoubleScalarVar("cutoff_diffnorm", info.cutoff_diffnorm));
    Mat_VarSetStructFieldByName(ncorr_struct, "cutoff_iteration", 0,
                                createInt32ScalarVar("cutoff_iteration", info.cutoff_iteration));
    Mat_VarSetStructFieldByName(ncorr_struct, "lenscoef", 0,
                                createInt32ScalarVar("lenscoef", info.lenscoef));
    Mat_VarSetStructFieldByName(ncorr_struct, "pixtounits", 0,
                                createDoubleScalarVar("pixtounits", info.pixtounits));
    Mat_VarSetStructFieldByName(ncorr_struct, "radius", 0,
                                createInt32ScalarVar("radius", info.radius));
    Mat_VarSetStructFieldByName(ncorr_struct, "spacing", 0,
                                createInt32ScalarVar("spacing", info.spacing));
    Mat_VarSetStructFieldByName(ncorr_struct, "subsettrunc", 0,
                                createInt32ScalarVar("subsettrunc", info.subsettrunc ? 1 : 0));
    Mat_VarSetStructFieldByName(ncorr_struct, "total_threads", 0,
                                createInt32ScalarVar("total_threads", info.total_threads));

    std::vector<std::string> stepanalysis_fields = {"enabled", "type", "auto", "step"};
    matvar_t* stepanalysis_struct = createStructVariableLocal("stepanalysis", stepanalysis_fields);
    if (stepanalysis_struct) {
        Mat_VarSetStructFieldByName(
            stepanalysis_struct, "enabled", 0,
            createInt32ScalarVar("enabled", info.stepanalysis.enabled ? 1 : 0));
        if (!info.stepanalysis.type.empty()) {
            Mat_VarSetStructFieldByName(
                stepanalysis_struct, "type", 0,
                createStringVar("type", info.stepanalysis.type));
        }
        Mat_VarSetStructFieldByName(
            stepanalysis_struct, "auto", 0,
            createInt32ScalarVar("auto", info.stepanalysis.auto_update ? 1 : 0));
        Mat_VarSetStructFieldByName(
            stepanalysis_struct, "step", 0,
            createInt32ScalarVar("step", info.stepanalysis.step));
        Mat_VarSetStructFieldByName(ncorr_struct, "stepanalysis", 0, stepanalysis_struct);
    }

    if (!info.type.empty()) {
        Mat_VarSetStructFieldByName(ncorr_struct, "type", 0, createStringVar("type", info.type));
    }
    if (!info.units.empty()) {
        Mat_VarSetStructFieldByName(ncorr_struct, "units", 0, createStringVar("units", info.units));
    }

    if (!info.imgcorr.empty()) {
        std::vector<size_t> cell_dims = {info.imgcorr.size(), 1};
        matvar_t* imgcorr_cell =
            Mat_VarCreate("imgcorr", MAT_C_CELL, MAT_T_CELL, 2, cell_dims.data(), nullptr, 0);
        for (size_t i = 0; i < info.imgcorr.size(); ++i) {
            if (!info.imgcorr[i].empty()) {
                Mat_VarSetCell(imgcorr_cell, i, createStringVar(nullptr, info.imgcorr[i]));
            }
        }
        Mat_VarSetStructFieldByName(ncorr_struct, "imgcorr", 0, imgcorr_cell);
    }

    return ncorr_struct;
}

matvar_t* buildDIC2DPairStruct(const ::cppxdic::DIC2DPairResults& dic2d,
                               const char* struct_name) {
    std::vector<std::string> fields = {
        "nCamRef", "nCamDef", "nImages", "pairOrder", "pairForced", "ROImask", "ncorrInfo",
        "Points", "CorCoeffVec", "Faces", "FaceColors"
    };
    matvar_t* dic2d_struct = createStructVariableLocal(struct_name, fields);
    if (!dic2d_struct) {
        return nullptr;
    }

    Mat_VarSetStructFieldByName(dic2d_struct, "nCamRef", 0,
                                createDoubleScalarVar("nCamRef", dic2d.nCamRef));
    Mat_VarSetStructFieldByName(dic2d_struct, "nCamDef", 0,
                                createDoubleScalarVar("nCamDef", dic2d.nCamDef));
    Mat_VarSetStructFieldByName(dic2d_struct, "nImages", 0,
                                createDoubleScalarVar("nImages", dic2d.nImages));
    Mat_VarSetStructFieldByName(dic2d_struct, "pairForced", 0,
                                createDoubleScalarVar("pairForced", dic2d.pairForced ? 1.0 : 0.0));

    if (!dic2d.pairOrder.empty()) {
        std::vector<double> pair_order(dic2d.pairOrder.begin(), dic2d.pairOrder.end());
        std::vector<size_t> dims = {1, pair_order.size()};
        Mat_VarSetStructFieldByName(dic2d_struct, "pairOrder", 0,
                                    createDoubleArrayVar("pairOrder", pair_order, dims));
    }

    if (!dic2d.ROImask.empty()) {
        matvar_t* roi_var = createCvMatVar("ROImask", dic2d.ROImask);
        if (roi_var) {
            Mat_VarSetStructFieldByName(dic2d_struct, "ROImask", 0, roi_var);
        }
    }

    matvar_t* ncorr_struct = buildNcorrInfoStruct(dic2d.ncorrInfo);
    if (ncorr_struct) {
        Mat_VarSetStructFieldByName(dic2d_struct, "ncorrInfo", 0, ncorr_struct);
    }

    matvar_t* points_cell = buildPoints2DCell("Points", dic2d.Points);
    if (points_cell) {
        Mat_VarSetStructFieldByName(dic2d_struct, "Points", 0, points_cell);
    }

    if (!dic2d.CorCoeffVec.empty()) {
        matvar_t* corr_cell = buildScalarCell("CorCoeffVec", dic2d.CorCoeffVec);
        if (corr_cell) {
            Mat_VarSetStructFieldByName(dic2d_struct, "CorCoeffVec", 0, corr_cell);
        }
    }

    matvar_t* faces_var = createIndexedFacesVar("Faces", dic2d.Faces);
    if (faces_var) {
        Mat_VarSetStructFieldByName(dic2d_struct, "Faces", 0, faces_var);
    }

    if (!dic2d.FaceColors.empty()) {
        std::vector<size_t> dims = {dic2d.FaceColors.size(), 1};
        Mat_VarSetStructFieldByName(dic2d_struct, "FaceColors", 0,
                                    createDoubleArrayVar("FaceColors", dic2d.FaceColors, dims));
    }

    return dic2d_struct;
}

matvar_t* buildPairStringCell(const char* name, const std::vector<std::string>& values) {
    std::vector<size_t> dims = {2, 1};
    matvar_t* cell = Mat_VarCreate(name, MAT_C_CELL, MAT_T_CELL, 2, dims.data(), nullptr, 0);
    if (!cell) {
        return nullptr;
    }

    for (size_t i = 0; i < values.size() && i < 2; ++i) {
        Mat_VarSetCell(cell, i, createStringVar(nullptr, values[i]));
    }
    return cell;
}

matvar_t* buildPairVectorCell(const char* name, const std::vector<std::vector<double>>& values) {
    std::vector<size_t> dims = {2, 1};
    matvar_t* cell = Mat_VarCreate(name, MAT_C_CELL, MAT_T_CELL, 2, dims.data(), nullptr, 0);
    if (!cell) {
        return nullptr;
    }

    for (size_t i = 0; i < values.size() && i < 2; ++i) {
        if (values[i].empty()) {
            continue;
        }
        std::vector<size_t> vec_dims = {1, values[i].size()};
        Mat_VarSetCell(cell, i,
                       Mat_VarCreate(nullptr, MAT_C_DOUBLE, MAT_T_DOUBLE, 2,
                                     vec_dims.data(), const_cast<double*>(values[i].data()), 0));
    }
    return cell;
}

matvar_t* buildDIC3DPairStruct(const ::cppxdic::DIC3DpairResults& pair) {
    std::vector<std::string> pair_fields = {
        "cameraPairInd", "calibration", "distortionModel", "distortionPath",
        "Faces", "FaceColors", "Points3D", "Disp", "FaceCentroids",
        "corrComb", "FaceCorrComb"
    };
    matvar_t* pair_struct = createStructVariableLocal("pair", pair_fields);
    if (!pair_struct) {
        return nullptr;
    }

    if (!pair.cameraPairInd.empty()) {
        std::vector<double> pair_indices(pair.cameraPairInd.begin(), pair.cameraPairInd.end());
        std::vector<size_t> dims = {1, pair_indices.size()};
        Mat_VarSetStructFieldByName(pair_struct, "cameraPairInd", 0,
                                    createDoubleArrayVar("cameraPairInd", pair_indices, dims));
    }

    std::vector<std::string> calib_fields = {"DLTpath", "DLTparameters"};
    matvar_t* calibration_struct = createStructVariableLocal("calibration", calib_fields);
    if (calibration_struct) {
        matvar_t* dlt_path = buildPairStringCell("DLTpath", pair.DLTpath);
        if (dlt_path) {
            Mat_VarSetStructFieldByName(calibration_struct, "DLTpath", 0, dlt_path);
        }
        matvar_t* dlt_params = buildPairVectorCell("DLTparameters", pair.DLTparameters);
        if (dlt_params) {
            Mat_VarSetStructFieldByName(calibration_struct, "DLTparameters", 0, dlt_params);
        }
        Mat_VarSetStructFieldByName(pair_struct, "calibration", 0, calibration_struct);
    }

    matvar_t* distortion_model = buildPairStringCell("distortionModel", pair.distortionModel);
    if (distortion_model) {
        Mat_VarSetStructFieldByName(pair_struct, "distortionModel", 0, distortion_model);
    }

    matvar_t* distortion_path = buildPairStringCell("distortionPath", pair.distortionPath);
    if (distortion_path) {
        Mat_VarSetStructFieldByName(pair_struct, "distortionPath", 0, distortion_path);
    }

    matvar_t* faces_var = createIndexedFacesVar("Faces", pair.Faces);
    if (faces_var) {
        Mat_VarSetStructFieldByName(pair_struct, "Faces", 0, faces_var);
    }

    if (!pair.FaceColors.empty()) {
        std::vector<size_t> dims = {pair.FaceColors.size(), 1};
        Mat_VarSetStructFieldByName(pair_struct, "FaceColors", 0,
                                    createDoubleArrayVar("FaceColors", pair.FaceColors, dims));
    }

    matvar_t* points_cell = buildPoints3DCell("Points3D", pair.Points3D);
    if (points_cell) {
        Mat_VarSetStructFieldByName(pair_struct, "Points3D", 0, points_cell);
    }

    matvar_t* disp_struct = buildDispStruct(pair.Disp);
    if (disp_struct) {
        Mat_VarSetStructFieldByName(pair_struct, "Disp", 0, disp_struct);
    }

    if (!pair.FaceCentroids.empty()) {
        matvar_t* centroids = buildFaceTripletCell("FaceCentroids", pair.FaceCentroids);
        if (centroids) {
            Mat_VarSetStructFieldByName(pair_struct, "FaceCentroids", 0, centroids);
        }
    }

    if (!pair.corrComb.empty()) {
        matvar_t* corr_comb = buildScalarCell("corrComb", pair.corrComb);
        if (corr_comb) {
            Mat_VarSetStructFieldByName(pair_struct, "corrComb", 0, corr_comb);
        }
    }

    if (!pair.FaceCorrComb.empty()) {
        matvar_t* face_corr = buildScalarCell("FaceCorrComb", pair.FaceCorrComb);
        if (face_corr) {
            Mat_VarSetStructFieldByName(pair_struct, "FaceCorrComb", 0, face_corr);
        }
    }

    return pair_struct;
}

matvar_t* buildAllPairsResultsCell(const std::vector<::cppxdic::DIC3DpairResults>& all_pairs) {
    std::vector<size_t> cell_dims = {1, all_pairs.size()};
    matvar_t* cell_array =
        Mat_VarCreate("AllPairsResults", MAT_C_CELL, MAT_T_CELL, 2, cell_dims.data(), nullptr, 0);
    if (!cell_array) {
        return nullptr;
    }

    for (size_t i = 0; i < all_pairs.size(); ++i) {
        Mat_VarSetCell(cell_array, i, buildDIC3DPairStruct(all_pairs[i]));
    }
    return cell_array;
}

matvar_t* buildDIC2DinfoCell(const std::vector<::cppxdic::DIC2DPairResults>& dic2d_info) {
    std::vector<size_t> dims = {dic2d_info.size(), 1};
    matvar_t* cell =
        Mat_VarCreate("DIC2Dinfo", MAT_C_CELL, MAT_T_CELL, 2, dims.data(), nullptr, 0);
    if (!cell) {
        return nullptr;
    }

    for (size_t i = 0; i < dic2d_info.size(); ++i) {
        Mat_VarSetCell(cell, i, buildDIC2DPairStruct(dic2d_info[i], "DIC2D"));
    }
    return cell;
}

} // namespace

namespace cppxdic::io::mat {

bool MatResultsWriter::writeDIC2DPairResults(const std::string& filename,
                                             const ::cppxdic::DIC2DPairResults& results) {
    mat_t* matfp = ::cppxdic::MatWriter::createMatFileHDF5(filename);
    if (!matfp) {
        std::cerr << "Failed to create DIC2DPairResults file: " << filename << std::endl;
        return false;
    }

    ::cppxdic::MatWriter::writeScalarVariable(matfp, "nCamRef", results.nCamRef);
    ::cppxdic::MatWriter::writeScalarVariable(matfp, "nCamDef", results.nCamDef);
    ::cppxdic::MatWriter::writeScalarVariable(matfp, "nImages", results.nImages);
    ::cppxdic::MatWriter::writeScalarVariable(matfp, "pairForced", results.pairForced ? 1.0 : 0.0);

    if (!results.pairOrder.empty()) {
        std::vector<double> pair_order(results.pairOrder.begin(), results.pairOrder.end());
        std::vector<size_t> dims = {1, pair_order.size()};
        ::cppxdic::MatWriter::writeArrayVariable(
            matfp, "pairOrder", pair_order.data(), dims, MAT_T_DOUBLE, MAT_C_DOUBLE);
    }

    if (!results.ROImask.empty()) {
        ::cppxdic::MatWriter::writeMatVariable(matfp, "ROImask", results.ROImask);
    }

    matvar_t* ncorr_struct = buildNcorrInfoStruct(results.ncorrInfo);
    if (ncorr_struct) {
        Mat_VarWrite(matfp, ncorr_struct, MAT_COMPRESSION_NONE);
        Mat_VarFree(ncorr_struct);
    }

    matvar_t* points_cell = buildPoints2DCell("Points", results.Points);
    if (points_cell) {
        Mat_VarWrite(matfp, points_cell, MAT_COMPRESSION_NONE);
        Mat_VarFree(points_cell);
    }

    if (!results.CorCoeffVec.empty()) {
        matvar_t* corr_cell = buildScalarCell("CorCoeffVec", results.CorCoeffVec);
        if (corr_cell) {
            Mat_VarWrite(matfp, corr_cell, MAT_COMPRESSION_NONE);
            Mat_VarFree(corr_cell);
        }
    }

    matvar_t* faces_var = createIndexedFacesVar("Faces", results.Faces);
    if (faces_var) {
        Mat_VarWrite(matfp, faces_var, MAT_COMPRESSION_NONE);
        Mat_VarFree(faces_var);
    }

    if (!results.FaceColors.empty()) {
        std::vector<size_t> dims = {results.FaceColors.size(), 1};
        ::cppxdic::MatWriter::writeArrayVariable(
            matfp, "FaceColors", results.FaceColors.data(), dims, MAT_T_DOUBLE, MAT_C_DOUBLE);
    }

    Mat_Close(matfp);
    std::cout << "Wrote DIC2DPairResults: " << filename << std::endl;
    return true;
}

bool MatResultsWriter::writeAllPairsResults(
    mat_t* matfp,
    const std::vector<::cppxdic::DIC3DpairResults>& all_pairs) {
    if (!matfp) {
        std::cerr << "Invalid MAT file pointer" << std::endl;
        return false;
    }

    std::cout << "Writing AllPairsResults with " << all_pairs.size() << " pairs..." << std::endl;
    if (all_pairs.empty()) {
        std::cout << "Warning: Empty AllPairsResults, skipping" << std::endl;
        return true;
    }

    matvar_t* cell_array = buildAllPairsResultsCell(all_pairs);
    if (!cell_array) {
        std::cerr << "Failed to create AllPairsResults cell array" << std::endl;
        return false;
    }

    Mat_VarWrite(matfp, cell_array, MAT_COMPRESSION_NONE);
    Mat_VarFree(cell_array);
    std::cout << "✓ AllPairsResults written successfully (" << all_pairs.size() << " pairs)" << std::endl;
    return true;
}

bool MatResultsWriter::writeDIC2Dinfo(
    mat_t* matfp,
    const std::vector<::cppxdic::DIC2DPairResults>& dic2d_info) {
    if (!matfp) {
        std::cerr << "Invalid MAT file pointer" << std::endl;
        return false;
    }

    std::cout << "Writing DIC2Dinfo with " << dic2d_info.size() << " entries..." << std::endl;
    if (dic2d_info.empty()) {
        std::cout << "Warning: Empty DIC2Dinfo, skipping" << std::endl;
        return true;
    }

    matvar_t* cell_array = buildDIC2DinfoCell(dic2d_info);
    if (!cell_array) {
        std::cerr << "Failed to create DIC2Dinfo cell array" << std::endl;
        return false;
    }

    Mat_VarWrite(matfp, cell_array, MAT_COMPRESSION_NONE);
    Mat_VarFree(cell_array);
    std::cout << "✓ DIC2Dinfo written successfully (" << dic2d_info.size() << " entries)" << std::endl;
    return true;
}

matvar_t* MatResultsWriter::buildCombinedStructFields(
    mat_t* matfp,
    const ::cppxdic::DIC3Dcombined& combined,
    const std::string& struct_name,
    const std::vector<std::string>& extra_fields) {
    (void)matfp;

    std::vector<std::string> combined_fields = {
        "pairIndices", "Points3D", "Faces", "FaceColors", "corrComb",
        "FaceCorrComb", "FaceCentroids", "Disp", "FacePairInds",
        "PointPairInds", "calibration", "distortion", "AllPairsResults", "DIC2Dinfo"
    };
    for (const auto& field : extra_fields) {
        combined_fields.push_back(field);
    }

    matvar_t* combined_struct =
        ::cppxdic::MatWriter::createStructVariable(struct_name, combined_fields);
    if (!combined_struct) {
        std::cerr << "Failed to create " << struct_name << " struct" << std::endl;
        return nullptr;
    }

    if (!combined.pairIndices.empty()) {
        const size_t n_cams = combined.pairIndices.size();
        size_t n_pairs = n_cams / 2;
        if (n_pairs == 0) {
            n_pairs = 1;
        }
        std::vector<double> pair_dbl(n_pairs * 2, 0.0);
        for (size_t i = 0; i < n_pairs; ++i) {
            if (2 * i < combined.pairIndices.size()) {
                pair_dbl[i] = static_cast<double>(combined.pairIndices[2 * i]);
            }
            if (2 * i + 1 < combined.pairIndices.size()) {
                pair_dbl[i + n_pairs] = static_cast<double>(combined.pairIndices[2 * i + 1]);
            }
        }
        std::vector<size_t> dims = {n_pairs, 2};
        Mat_VarSetStructFieldByName(combined_struct, "pairIndices", 0,
                                    createDoubleArrayVar("pairIndices", pair_dbl, dims));
    }

    matvar_t* points3d_cell = buildPoints3DCell("Points3D", combined.Points3D);
    if (points3d_cell) {
        Mat_VarSetStructFieldByName(combined_struct, "Points3D", 0, points3d_cell);
    }

    matvar_t* faces_var = createIndexedFacesVar("Faces", combined.Faces);
    if (faces_var) {
        Mat_VarSetStructFieldByName(combined_struct, "Faces", 0, faces_var);
    }

    if (!combined.FaceColors.empty()) {
        std::vector<size_t> dims = {combined.FaceColors.size(), 1};
        Mat_VarSetStructFieldByName(combined_struct, "FaceColors", 0,
                                    createDoubleArrayVar("FaceColors", combined.FaceColors, dims));
    }

    if (!combined.corrComb.empty()) {
        matvar_t* corr_cell = buildScalarCell("corrComb", combined.corrComb);
        if (corr_cell) {
            Mat_VarSetStructFieldByName(combined_struct, "corrComb", 0, corr_cell);
        }
    }

    if (!combined.FaceCorrComb.empty()) {
        matvar_t* face_corr_cell = buildScalarCell("FaceCorrComb", combined.FaceCorrComb);
        if (face_corr_cell) {
            Mat_VarSetStructFieldByName(combined_struct, "FaceCorrComb", 0, face_corr_cell);
        }
    }

    if (!combined.FaceCentroids.empty()) {
        matvar_t* centroid_cell = buildFaceTripletCell("FaceCentroids", combined.FaceCentroids);
        if (centroid_cell) {
            Mat_VarSetStructFieldByName(combined_struct, "FaceCentroids", 0, centroid_cell);
        }
    }

    matvar_t* disp_struct = buildDispStruct(combined.Disp);
    if (disp_struct) {
        Mat_VarSetStructFieldByName(combined_struct, "Disp", 0, disp_struct);
    }

    if (!combined.FacePairInds.empty()) {
        std::vector<double> face_pair_inds(combined.FacePairInds.begin(), combined.FacePairInds.end());
        std::vector<size_t> dims = {combined.FacePairInds.size(), 1};
        Mat_VarSetStructFieldByName(combined_struct, "FacePairInds", 0,
                                    createDoubleArrayVar("FacePairInds", face_pair_inds, dims));
    }

    if (!combined.PointPairInds.empty()) {
        std::vector<double> point_pair_inds(combined.PointPairInds.begin(), combined.PointPairInds.end());
        std::vector<size_t> dims = {combined.PointPairInds.size(), 1};
        Mat_VarSetStructFieldByName(combined_struct, "PointPairInds", 0,
                                    createDoubleArrayVar("PointPairInds", point_pair_inds, dims));
    }

    if (!combined.calibration.DLT_paths.empty()) {
        std::vector<std::string> calib_fields = {"DLTpath", "DLTparameters"};
        matvar_t* calib_struct =
            ::cppxdic::MatWriter::createStructVariable("calibration", calib_fields);
        const size_t rows = combined.calibration.DLT_paths.size();
        const size_t cols = rows > 0 ? combined.calibration.DLT_paths[0].size() : 0;
        Mat_VarSetStructFieldByName(
            calib_struct, "DLTpath", 0,
            ::cppxdic::MatWriter::createCellArray2DFromStrings(
                "DLTpath", combined.calibration.DLT_paths, rows, cols));
        Mat_VarSetStructFieldByName(
            calib_struct, "DLTparameters", 0,
            ::cppxdic::MatWriter::createCellArray2DFromVectors(
                "DLTparameters", combined.calibration.DLT_params, rows, cols));
        Mat_VarSetStructFieldByName(combined_struct, "calibration", 0, calib_struct);
    }

    if (!combined.distortion.distortion_models.empty()) {
        std::vector<std::string> dist_fields = {"distortionModel", "distortionPath"};
        matvar_t* dist_struct =
            ::cppxdic::MatWriter::createStructVariable("distortion", dist_fields);
        const size_t rows = combined.distortion.distortion_models.size();
        const size_t cols = rows > 0 ? combined.distortion.distortion_models[0].size() : 0;
        Mat_VarSetStructFieldByName(
            dist_struct, "distortionModel", 0,
            ::cppxdic::MatWriter::createCellArray2DFromStrings(
                "distortionModel", combined.distortion.distortion_models, rows, cols));
        Mat_VarSetStructFieldByName(
            dist_struct, "distortionPath", 0,
            ::cppxdic::MatWriter::createCellArray2DFromStrings(
                "distortionPath", combined.distortion.distortion_paths, rows, cols));
        Mat_VarSetStructFieldByName(combined_struct, "distortion", 0, dist_struct);
    }

    if (!combined.AllPairsResults.empty()) {
        matvar_t* all_pairs_var = buildAllPairsResultsCell(combined.AllPairsResults);
        if (all_pairs_var) {
            Mat_VarSetStructFieldByName(combined_struct, "AllPairsResults", 0, all_pairs_var);
        }
    }

    if (!combined.DIC2Dinfo.empty()) {
        matvar_t* dic2d_info_var = buildDIC2DinfoCell(combined.DIC2Dinfo);
        if (dic2d_info_var) {
            Mat_VarSetStructFieldByName(combined_struct, "DIC2Dinfo", 0, dic2d_info_var);
        }
    }

    return combined_struct;
}

bool MatResultsWriter::write3DCombinedResults(const std::string& filename,
                                              const ::cppxdic::DIC3Dcombined& combined,
                                              const std::string& struct_name) {
    mat_t* matfp = ::cppxdic::MatWriter::createMatFileHDF5(filename);
    if (!matfp) {
        std::cerr << "Failed to create file: " << filename << std::endl;
        return false;
    }

    matvar_t* combined_struct = buildCombinedStructFields(matfp, combined, struct_name);
    if (!combined_struct) {
        Mat_Close(matfp);
        return false;
    }

    Mat_VarWrite(matfp, combined_struct, MAT_COMPRESSION_NONE);
    Mat_VarFree(combined_struct);
    Mat_Close(matfp);
    std::cout << "Wrote " << struct_name << ": " << filename << std::endl;
    return true;
}

bool MatResultsWriter::write3DPPresults(const std::string& filename,
                                        const ::cppxdic::DIC3DPPresults& ppresults) {
    mat_t* matfp = ::cppxdic::MatWriter::createMatFileHDF5(filename);
    if (!matfp) {
        std::cerr << "Failed to create DIC3DPPresults file: " << filename << std::endl;
        return false;
    }

    std::vector<std::string> extra_fields = {"Deform", "FaceIsoInd", "deftype"};
    matvar_t* pp_struct = buildCombinedStructFields(matfp, ppresults, "DIC3DPPresults", extra_fields);
    if (!pp_struct) {
        Mat_Close(matfp);
        return false;
    }

    if (!ppresults.deform_full.frames.empty()) {
        std::cout << "Writing full Deform group with " << ppresults.deform_full.n_frames
                  << " frames and " << ppresults.deform_full.n_faces << " faces..." << std::endl;
        matvar_t* deform_struct = buildDeformationStruct("Deform", ppresults.deform_full);
        if (deform_struct) {
            Mat_VarSetStructFieldByName(pp_struct, "Deform", 0, deform_struct);
        } else {
            std::cerr << "Warning: Failed to build Deform struct" << std::endl;
        }
    } else {
        std::cout << "Warning: No deformation data available, skipping Deform group" << std::endl;
    }

    if (!ppresults.FaceIsoInd.empty()) {
        matvar_t* iso_cell = buildScalarCell("FaceIsoInd", ppresults.FaceIsoInd);
        if (iso_cell) {
            Mat_VarSetStructFieldByName(pp_struct, "FaceIsoInd", 0, iso_cell);
        }
    }

    if (!ppresults.deftype.empty()) {
        std::vector<size_t> str_dims = {1, ppresults.deftype.size()};
        matvar_t* deftype_var = Mat_VarCreate("deftype", MAT_C_CHAR, MAT_T_UTF8,
                                              2, str_dims.data(),
                                              const_cast<char*>(ppresults.deftype.c_str()), 0);
        if (deftype_var) {
            Mat_VarSetStructFieldByName(pp_struct, "deftype", 0, deftype_var);
        }
    }

    Mat_VarWrite(matfp, pp_struct, MAT_COMPRESSION_NONE);
    Mat_VarFree(pp_struct);
    Mat_Close(matfp);
    std::cout << "Wrote DIC3DPPresults: " << filename << std::endl;
    return true;
}

matvar_t* MatResultsWriter::buildDeformationStruct(
    const std::string& group_name,
    const ::cppxdic::FrameDeformationResult& deform_data) {
    const size_t n_frames = deform_data.n_frames;

    std::vector<std::vector<double>> Area_data(n_frames), Lamda1_data(n_frames), Lamda2_data(n_frames);
    std::vector<std::vector<double>> J_data(n_frames), Emgn_data(n_frames), emgn_data(n_frames);
    std::vector<std::vector<double>> Epc1_data(n_frames), Epc2_data(n_frames);
    std::vector<std::vector<double>> epc1_data(n_frames), epc2_data(n_frames);
    std::vector<std::vector<double>> EShearMax_data(n_frames), eShearMax_data(n_frames);
    std::vector<std::vector<double>> Eeq_data(n_frames), eeq_data(n_frames), Dnorm_data(n_frames);

    std::vector<std::vector<Eigen::Vector3d>> D1_data(n_frames), D2_data(n_frames), D3_data(n_frames);
    std::vector<std::vector<Eigen::Vector3d>> d1_data(n_frames), d2_data(n_frames), d3_data(n_frames);
    std::vector<std::vector<Eigen::Vector3d>> Drec1_data(n_frames), Drec2_data(n_frames);
    std::vector<std::vector<Eigen::Vector3d>> Epc1vec_data(n_frames), Epc2vec_data(n_frames);
    std::vector<std::vector<Eigen::Vector3d>> Epc1vecCur_data(n_frames), Epc2vecCur_data(n_frames);
    std::vector<std::vector<Eigen::Vector3d>> epc1vec_data(n_frames), epc2vec_data(n_frames);
    std::vector<std::vector<Eigen::Vector3d>> EShearMaxVec1_data(n_frames), EShearMaxVec2_data(n_frames);
    std::vector<std::vector<Eigen::Vector3d>> EShearMaxVecCur1_data(n_frames), EShearMaxVecCur2_data(n_frames);
    std::vector<std::vector<Eigen::Vector3d>> eShearMaxVec1_data(n_frames), eShearMaxVec2_data(n_frames);

    std::vector<std::vector<Eigen::Matrix3d>> Fmat_data(n_frames), Cmat_data(n_frames);
    std::vector<std::vector<Eigen::Matrix3d>> Emat_data(n_frames), emat_data(n_frames);

    for (size_t i = 0; i < n_frames && i < deform_data.frames.size(); ++i) {
        const auto& frame = deform_data.frames[i];
        Area_data[i] = frame.Area;
        Lamda1_data[i] = frame.Lamda1;
        Lamda2_data[i] = frame.Lamda2;
        J_data[i] = frame.J;
        Emgn_data[i] = frame.Emgn;
        emgn_data[i] = frame.emgn;
        Epc1_data[i] = frame.Epc1;
        Epc2_data[i] = frame.Epc2;
        epc1_data[i] = frame.epc1;
        epc2_data[i] = frame.epc2;
        EShearMax_data[i] = frame.EShearMax;
        eShearMax_data[i] = frame.eShearMax;
        Eeq_data[i] = frame.Eeq;
        eeq_data[i] = frame.eeq;
        Dnorm_data[i] = frame.Dnorm;
        D1_data[i] = frame.D1;
        D2_data[i] = frame.D2;
        D3_data[i] = frame.D3;
        d1_data[i] = frame.d1;
        d2_data[i] = frame.d2;
        d3_data[i] = frame.d3;
        Drec1_data[i] = frame.Drec1;
        Drec2_data[i] = frame.Drec2;
        Epc1vec_data[i] = frame.Epc1vec;
        Epc2vec_data[i] = frame.Epc2vec;
        Epc1vecCur_data[i] = frame.Epc1vecCur;
        Epc2vecCur_data[i] = frame.Epc2vecCur;
        epc1vec_data[i] = frame.epc1vec;
        epc2vec_data[i] = frame.epc2vec;
        EShearMaxVec1_data[i] = frame.EShearMaxVec1;
        EShearMaxVec2_data[i] = frame.EShearMaxVec2;
        EShearMaxVecCur1_data[i] = frame.EShearMaxVecCur1;
        EShearMaxVecCur2_data[i] = frame.EShearMaxVecCur2;
        eShearMaxVec1_data[i] = frame.eShearMaxVec1;
        eShearMaxVec2_data[i] = frame.eShearMaxVec2;
        Fmat_data[i] = frame.Fmat;
        Cmat_data[i] = frame.Cmat;
        Emat_data[i] = frame.Emat;
        emat_data[i] = frame.emat;
    }

    matvar_t* Area_cell = ::cppxdic::MatWriter::createCellArrayFromScalars("Area", Area_data, n_frames);
    matvar_t* Lamda1_cell = ::cppxdic::MatWriter::createCellArrayFromScalars("Lamda1", Lamda1_data, n_frames);
    matvar_t* Lamda2_cell = ::cppxdic::MatWriter::createCellArrayFromScalars("Lamda2", Lamda2_data, n_frames);
    matvar_t* J_cell = ::cppxdic::MatWriter::createCellArrayFromScalars("J", J_data, n_frames);
    matvar_t* Emgn_cell = ::cppxdic::MatWriter::createCellArrayFromScalars("Emgn", Emgn_data, n_frames);
    matvar_t* emgn_cell = ::cppxdic::MatWriter::createCellArrayFromScalars("emgn", emgn_data, n_frames);
    matvar_t* Epc1_cell = ::cppxdic::MatWriter::createCellArrayFromScalars("Epc1", Epc1_data, n_frames);
    matvar_t* Epc2_cell = ::cppxdic::MatWriter::createCellArrayFromScalars("Epc2", Epc2_data, n_frames);
    matvar_t* epc1_cell = ::cppxdic::MatWriter::createCellArrayFromScalars("epc1", epc1_data, n_frames);
    matvar_t* epc2_cell = ::cppxdic::MatWriter::createCellArrayFromScalars("epc2", epc2_data, n_frames);
    matvar_t* EShearMax_cell = ::cppxdic::MatWriter::createCellArrayFromScalars("EShearMax", EShearMax_data, n_frames);
    matvar_t* eShearMax_cell = ::cppxdic::MatWriter::createCellArrayFromScalars("eShearMax", eShearMax_data, n_frames);
    matvar_t* Eeq_cell = ::cppxdic::MatWriter::createCellArrayFromScalars("Eeq", Eeq_data, n_frames);
    matvar_t* eeq_cell = ::cppxdic::MatWriter::createCellArrayFromScalars("eeq", eeq_data, n_frames);
    matvar_t* Dnorm_cell = ::cppxdic::MatWriter::createCellArrayFromScalars("Dnorm", Dnorm_data, n_frames);

    matvar_t* D1_cell = ::cppxdic::MatWriter::createCellArrayFromVectors("D1", D1_data, n_frames);
    matvar_t* D2_cell = ::cppxdic::MatWriter::createCellArrayFromVectors("D2", D2_data, n_frames);
    matvar_t* D3_cell = ::cppxdic::MatWriter::createCellArrayFromVectors("D3", D3_data, n_frames);
    matvar_t* d1_cell = ::cppxdic::MatWriter::createCellArrayFromVectors("d1", d1_data, n_frames);
    matvar_t* d2_cell = ::cppxdic::MatWriter::createCellArrayFromVectors("d2", d2_data, n_frames);
    matvar_t* d3_cell = ::cppxdic::MatWriter::createCellArrayFromVectors("d3", d3_data, n_frames);
    matvar_t* Drec1_cell = ::cppxdic::MatWriter::createCellArrayFromVectors("Drec1", Drec1_data, n_frames);
    matvar_t* Drec2_cell = ::cppxdic::MatWriter::createCellArrayFromVectors("Drec2", Drec2_data, n_frames);
    matvar_t* Epc1vec_cell = ::cppxdic::MatWriter::createCellArrayFromVectors("Epc1vec", Epc1vec_data, n_frames);
    matvar_t* Epc2vec_cell = ::cppxdic::MatWriter::createCellArrayFromVectors("Epc2vec", Epc2vec_data, n_frames);
    matvar_t* Epc1vecCur_cell = ::cppxdic::MatWriter::createCellArrayFromVectors("Epc1vecCur", Epc1vecCur_data, n_frames);
    matvar_t* Epc2vecCur_cell = ::cppxdic::MatWriter::createCellArrayFromVectors("Epc2vecCur", Epc2vecCur_data, n_frames);
    matvar_t* epc1vec_cell = ::cppxdic::MatWriter::createCellArrayFromVectors("epc1vec", epc1vec_data, n_frames);
    matvar_t* epc2vec_cell = ::cppxdic::MatWriter::createCellArrayFromVectors("epc2vec", epc2vec_data, n_frames);
    matvar_t* EShearMaxVec1_cell = ::cppxdic::MatWriter::createCellArrayFromVectors("EShearMaxVec1", EShearMaxVec1_data, n_frames);
    matvar_t* EShearMaxVec2_cell = ::cppxdic::MatWriter::createCellArrayFromVectors("EShearMaxVec2", EShearMaxVec2_data, n_frames);
    matvar_t* EShearMaxVecCur1_cell = ::cppxdic::MatWriter::createCellArrayFromVectors("EShearMaxVecCur1", EShearMaxVecCur1_data, n_frames);
    matvar_t* EShearMaxVecCur2_cell = ::cppxdic::MatWriter::createCellArrayFromVectors("EShearMaxVecCur2", EShearMaxVecCur2_data, n_frames);
    matvar_t* eShearMaxVec1_cell = ::cppxdic::MatWriter::createCellArrayFromVectors("eShearMaxVec1", eShearMaxVec1_data, n_frames);
    matvar_t* eShearMaxVec2_cell = ::cppxdic::MatWriter::createCellArrayFromVectors("eShearMaxVec2", eShearMaxVec2_data, n_frames);

    matvar_t* Fmat_cell = ::cppxdic::MatWriter::createCellArrayFromMatrices("Fmat", Fmat_data, n_frames);
    matvar_t* Cmat_cell = ::cppxdic::MatWriter::createCellArrayFromMatrices("Cmat", Cmat_data, n_frames);
    matvar_t* Emat_cell = ::cppxdic::MatWriter::createCellArrayFromMatrices("Emat", Emat_data, n_frames);
    matvar_t* emat_cell = ::cppxdic::MatWriter::createCellArrayFromMatrices("emat", emat_data, n_frames);

    std::vector<std::string> deform_fields = {
        "Area", "Lamda1", "Lamda2", "J", "Emgn", "emgn",
        "Epc1", "Epc2", "epc1", "epc2", "EShearMax", "eShearMax",
        "Eeq", "eeq", "Dnorm", "D1", "D2", "D3", "d1", "d2", "d3",
        "Drec1", "Drec2", "Epc1vec", "Epc2vec", "Epc1vecCur", "Epc2vecCur",
        "epc1vec", "epc2vec", "EShearMaxVec1", "EShearMaxVec2",
        "EShearMaxVecCur1", "EShearMaxVecCur2", "eShearMaxVec1", "eShearMaxVec2",
        "Fmat", "Cmat", "Emat", "emat"
    };
    matvar_t* deform_struct =
        ::cppxdic::MatWriter::createStructVariable(group_name, deform_fields);

    Mat_VarSetStructFieldByName(deform_struct, "Area", 0, Area_cell);
    Mat_VarSetStructFieldByName(deform_struct, "Lamda1", 0, Lamda1_cell);
    Mat_VarSetStructFieldByName(deform_struct, "Lamda2", 0, Lamda2_cell);
    Mat_VarSetStructFieldByName(deform_struct, "J", 0, J_cell);
    Mat_VarSetStructFieldByName(deform_struct, "Emgn", 0, Emgn_cell);
    Mat_VarSetStructFieldByName(deform_struct, "emgn", 0, emgn_cell);
    Mat_VarSetStructFieldByName(deform_struct, "Epc1", 0, Epc1_cell);
    Mat_VarSetStructFieldByName(deform_struct, "Epc2", 0, Epc2_cell);
    Mat_VarSetStructFieldByName(deform_struct, "epc1", 0, epc1_cell);
    Mat_VarSetStructFieldByName(deform_struct, "epc2", 0, epc2_cell);
    Mat_VarSetStructFieldByName(deform_struct, "EShearMax", 0, EShearMax_cell);
    Mat_VarSetStructFieldByName(deform_struct, "eShearMax", 0, eShearMax_cell);
    Mat_VarSetStructFieldByName(deform_struct, "Eeq", 0, Eeq_cell);
    Mat_VarSetStructFieldByName(deform_struct, "eeq", 0, eeq_cell);
    Mat_VarSetStructFieldByName(deform_struct, "Dnorm", 0, Dnorm_cell);
    Mat_VarSetStructFieldByName(deform_struct, "D1", 0, D1_cell);
    Mat_VarSetStructFieldByName(deform_struct, "D2", 0, D2_cell);
    Mat_VarSetStructFieldByName(deform_struct, "D3", 0, D3_cell);
    Mat_VarSetStructFieldByName(deform_struct, "d1", 0, d1_cell);
    Mat_VarSetStructFieldByName(deform_struct, "d2", 0, d2_cell);
    Mat_VarSetStructFieldByName(deform_struct, "d3", 0, d3_cell);
    Mat_VarSetStructFieldByName(deform_struct, "Drec1", 0, Drec1_cell);
    Mat_VarSetStructFieldByName(deform_struct, "Drec2", 0, Drec2_cell);
    Mat_VarSetStructFieldByName(deform_struct, "Epc1vec", 0, Epc1vec_cell);
    Mat_VarSetStructFieldByName(deform_struct, "Epc2vec", 0, Epc2vec_cell);
    Mat_VarSetStructFieldByName(deform_struct, "Epc1vecCur", 0, Epc1vecCur_cell);
    Mat_VarSetStructFieldByName(deform_struct, "Epc2vecCur", 0, Epc2vecCur_cell);
    Mat_VarSetStructFieldByName(deform_struct, "epc1vec", 0, epc1vec_cell);
    Mat_VarSetStructFieldByName(deform_struct, "epc2vec", 0, epc2vec_cell);
    Mat_VarSetStructFieldByName(deform_struct, "EShearMaxVec1", 0, EShearMaxVec1_cell);
    Mat_VarSetStructFieldByName(deform_struct, "EShearMaxVec2", 0, EShearMaxVec2_cell);
    Mat_VarSetStructFieldByName(deform_struct, "EShearMaxVecCur1", 0, EShearMaxVecCur1_cell);
    Mat_VarSetStructFieldByName(deform_struct, "EShearMaxVecCur2", 0, EShearMaxVecCur2_cell);
    Mat_VarSetStructFieldByName(deform_struct, "eShearMaxVec1", 0, eShearMaxVec1_cell);
    Mat_VarSetStructFieldByName(deform_struct, "eShearMaxVec2", 0, eShearMaxVec2_cell);
    Mat_VarSetStructFieldByName(deform_struct, "Fmat", 0, Fmat_cell);
    Mat_VarSetStructFieldByName(deform_struct, "Cmat", 0, Cmat_cell);
    Mat_VarSetStructFieldByName(deform_struct, "Emat", 0, Emat_cell);
    Mat_VarSetStructFieldByName(deform_struct, "emat", 0, emat_cell);

    std::cout << "Built deformation struct '" << group_name << "' with "
              << n_frames << " frames and " << deform_data.n_faces << " faces" << std::endl;
    return deform_struct;
}

} // namespace cppxdic::io::mat

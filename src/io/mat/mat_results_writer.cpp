#include "cppxdic/io/mat/mat_results_writer.h"
#include "cppxdic/io/mat/mat_result_codecs.h"
#include "cppxdic/io/mat/matio_helpers.h"

#include "mat_writer.h"

#include <iostream>
namespace cppxdic::io::mat {

bool MatResultsWriter::writeDIC2DPairResults(const std::string& filename,
                                             const ::cppxdic::DIC2DPairResults& results) {
    mat_t* matfp = ::cppxdic::MatWriter::createMatFileHDF5(filename);
    if (!matfp) {
        std::cerr << "Failed to create DIC2DPairResults file: " << filename << std::endl;
        return false;
    }

    writeScalarVariable(matfp, "nCamRef", results.nCamRef);
    writeScalarVariable(matfp, "nCamDef", results.nCamDef);
    writeScalarVariable(matfp, "nImages", results.nImages);
    writeScalarVariable(matfp, "pairForced", results.pairForced ? 1.0 : 0.0);

    if (!results.pairOrder.empty()) {
        std::vector<double> pair_order(results.pairOrder.begin(), results.pairOrder.end());
        std::vector<size_t> dims = {1, pair_order.size()};
        writeArrayVariable(
            matfp, "pairOrder", pair_order.data(), dims, MAT_T_DOUBLE, MAT_C_DOUBLE);
    }

    if (!results.ROImask.empty()) {
        writeMatVariable(matfp, "ROImask", results.ROImask);
    }

    matvar_t* ncorr_struct = codecs::buildNcorrInfoStruct(results.ncorrInfo);
    if (ncorr_struct) {
        Mat_VarWrite(matfp, ncorr_struct, MAT_COMPRESSION_NONE);
        Mat_VarFree(ncorr_struct);
    }

    matvar_t* points_cell = codecs::buildPoints2DCell("Points", results.Points);
    if (points_cell) {
        Mat_VarWrite(matfp, points_cell, MAT_COMPRESSION_NONE);
        Mat_VarFree(points_cell);
    }

    if (!results.CorCoeffVec.empty()) {
        matvar_t* corr_cell = codecs::buildScalarCell("CorCoeffVec", results.CorCoeffVec);
        if (corr_cell) {
            Mat_VarWrite(matfp, corr_cell, MAT_COMPRESSION_NONE);
            Mat_VarFree(corr_cell);
        }
    }

    matvar_t* faces_var = codecs::createIndexedFacesVar("Faces", results.Faces);
    if (faces_var) {
        Mat_VarWrite(matfp, faces_var, MAT_COMPRESSION_NONE);
        Mat_VarFree(faces_var);
    }

    if (!results.FaceColors.empty()) {
        std::vector<size_t> dims = {results.FaceColors.size(), 1};
        writeArrayVariable(
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

    matvar_t* cell_array = codecs::buildAllPairsResultsCell(all_pairs);
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

    matvar_t* cell_array = codecs::buildDIC2DinfoCell(dic2d_info);
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
    return codecs::buildCombinedStructFields(combined, struct_name, extra_fields);
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
        matvar_t* iso_cell = codecs::buildScalarCell("FaceIsoInd", ppresults.FaceIsoInd);
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
    return codecs::buildDeformationStruct(group_name, deform_data);
}

} // namespace cppxdic::io::mat

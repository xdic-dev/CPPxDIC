#ifndef CPPXDIC_IO_MAT_MAT_RESULT_CODECS_H
#define CPPXDIC_IO_MAT_MAT_RESULT_CODECS_H

#include "dic_structures.h"

#include <matio.h>
#include <string>
#include <vector>

namespace cppxdic::io::mat::codecs {

matvar_t* createCvMatVar(const char* name, const cv::Mat& mat);
matvar_t* createIndexedFacesVar(const char* name, const std::vector<int>& faces);
matvar_t* createDoubleArrayVar(const char* name,
                               const std::vector<double>& values,
                               const std::vector<size_t>& dims);

matvar_t* buildPoints2DCell(const char* name, const std::vector<::cppxdic::Points2D>& points);
matvar_t* buildPoints3DCell(const char* name, const std::vector<::cppxdic::Points3D>& points);
matvar_t* buildScalarCell(const char* name,
                          const std::vector<std::vector<double>>& data,
                          bool row_vector_cells = true);
matvar_t* buildFaceTripletCell(const char* name, const std::vector<std::vector<double>>& data);

matvar_t* buildDispStruct(const ::cppxdic::DispData& disp);
matvar_t* buildNcorrInfoStruct(const ::cppxdic::DICInfo& info);
matvar_t* buildDIC2DPairStruct(const ::cppxdic::DIC2DPairResults& dic2d,
                               const char* struct_name);
matvar_t* buildDIC3DPairStruct(const ::cppxdic::DIC3DpairResults& pair);
matvar_t* buildAllPairsResultsCell(const std::vector<::cppxdic::DIC3DpairResults>& all_pairs);
matvar_t* buildDIC2DinfoCell(const std::vector<::cppxdic::DIC2DPairResults>& dic2d_info);

matvar_t* buildCombinedStructFields(const ::cppxdic::DIC3Dcombined& combined,
                                    const std::string& struct_name,
                                    const std::vector<std::string>& extra_fields = {});

matvar_t* buildDeformationStruct(const std::string& group_name,
                                 const ::cppxdic::FrameDeformationResult& deform_data);

} // namespace cppxdic::io::mat::codecs

#endif // CPPXDIC_IO_MAT_MAT_RESULT_CODECS_H

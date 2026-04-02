#ifndef CPPXDIC_IO_MAT_MAT_RESULTS_WRITER_H
#define CPPXDIC_IO_MAT_MAT_RESULTS_WRITER_H

#include "dic_structures.h"

#include <matio.h>
#include <string>
#include <vector>

namespace cppxdic::io::mat {

class MatResultsWriter {
public:
    static bool writeDIC2DPairResults(const std::string& filename,
                                      const ::cppxdic::DIC2DPairResults& results);

    static bool writeAllPairsResults(
        mat_t* matfp,
        const std::vector<::cppxdic::DIC3DpairResults>& all_pairs);

    static bool writeDIC2Dinfo(
        mat_t* matfp,
        const std::vector<::cppxdic::DIC2DPairResults>& dic2d_info);

    static matvar_t* buildCombinedStructFields(
        mat_t* matfp,
        const ::cppxdic::DIC3Dcombined& combined,
        const std::string& struct_name,
        const std::vector<std::string>& extra_fields = {});

    static bool write3DCombinedResults(const std::string& filename,
                                       const ::cppxdic::DIC3Dcombined& combined,
                                       const std::string& struct_name = "DIC3Dcombined");

    static bool write3DPPresults(const std::string& filename,
                                 const ::cppxdic::DIC3DPPresults& ppresults);

    static matvar_t* buildDeformationStruct(const std::string& group_name,
                                            const ::cppxdic::FrameDeformationResult& deform_data);
};

} // namespace cppxdic::io::mat

#endif // CPPXDIC_IO_MAT_MAT_RESULTS_WRITER_H

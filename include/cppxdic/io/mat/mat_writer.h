#ifndef CPPXDIC_IO_MAT_MAT_WRITER_H
#define CPPXDIC_IO_MAT_MAT_WRITER_H

#include "dic_structures.h"
#include <mat_writer.h>
#include <string>

namespace cppxdic::io::mat {

class Writer {
public:
    static bool writeDIC3Dcombined(const std::string& path, const ::cppxdic::DIC3Dcombined& combined) {
        return ::cppxdic::MatWriter::write3DCombinedResults(path, combined);
    }

    static bool writeDIC3DPPresults(const std::string& path, const ::cppxdic::DIC3DPPresults& results) {
        return ::cppxdic::MatWriter::write3DPPresults(path, results);
    }

    static bool writeDIC2DPairResults(const std::string& path, const ::cppxdic::DIC2DPairResults& results) {
        return ::cppxdic::MatWriter::writeDIC2DPairResults(path, results);
    }
};

} // namespace cppxdic::io::mat

#endif // CPPXDIC_IO_MAT_MAT_WRITER_H

#ifndef CPPXDIC_IO_MAT_MAT_READER_H
#define CPPXDIC_IO_MAT_MAT_READER_H

#include "dic_structures.h"
#include <mat_reader.h>
#include <string>

namespace cppxdic::io::mat {

class Reader {
public:
    static bool loadProtocol(const std::string& path, ::cppxdic::ProtocolFileData& protocol) {
        return ::cppxdic::MatReader::loadProtocol(path, protocol);
    }

    static bool readDIC3Dcombined(const std::string& path, ::cppxdic::DIC3Dcombined& combined) {
        return ::cppxdic::MatReader::readDIC3Dcombined(path, combined);
    }

    static bool readDIC2DPairResults(const std::string& path, ::cppxdic::DIC2DPairResults& results) {
        return ::cppxdic::MatReader::readDIC2DPairResults(path, results);
    }
};

} // namespace cppxdic::io::mat

#endif // CPPXDIC_IO_MAT_MAT_READER_H

#include "cppxdic/io/mat/matio_helpers.h"

namespace cppxdic::io::mat {

MatFileHandle openReadOnly(const std::filesystem::path& path) {
    return ::cppxdic::MatReader::openMat(path.string());
}

MatVarHandle readVariable(::mat_t* file, std::string_view variable_name) {
    return ::cppxdic::MatReader::readVar(file, std::string(variable_name));
}

bool hasVariable(::mat_t* file, std::string_view variable_name) {
    return ::cppxdic::MatReader::variableExists(file, std::string(variable_name));
}

} // namespace cppxdic::io::mat

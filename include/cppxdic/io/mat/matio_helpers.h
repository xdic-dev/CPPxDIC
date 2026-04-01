#ifndef CPPXDIC_IO_MAT_MATIO_HELPERS_H
#define CPPXDIC_IO_MAT_MATIO_HELPERS_H

#include "cppxdic/io/mat/mat_reader.h"
#include <mat_reader.h>
#include <matio.h>
#include <filesystem>
#include <string_view>

namespace cppxdic::io::mat {

using MatFileHandle = ::cppxdic::MatReader::MatFilePtr;
using MatVarHandle = ::cppxdic::MatReader::MatVarPtr;

MatFileHandle openReadOnly(const std::filesystem::path& path);
MatVarHandle readVariable(::mat_t* file, std::string_view variable_name);
bool hasVariable(::mat_t* file, std::string_view variable_name);

} // namespace cppxdic::io::mat

#endif // CPPXDIC_IO_MAT_MATIO_HELPERS_H

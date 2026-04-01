#ifndef CPPXDIC_IO_MAT_MAT_SCHEMA_H
#define CPPXDIC_IO_MAT_MAT_SCHEMA_H

#include <string_view>

namespace cppxdic::io::mat::schema {

std::string_view protocolStructName();
std::string_view protocolFallbackStructName();
std::string_view protocolTitlesField();
std::string_view protocolTableField();
std::string_view dic3DCombinedVariableName();
std::string_view dic3DPPresultsVariableName();

} // namespace cppxdic::io::mat::schema

#endif // CPPXDIC_IO_MAT_MAT_SCHEMA_H

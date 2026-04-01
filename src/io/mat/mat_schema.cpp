#include "cppxdic/io/mat/mat_schema.h"

namespace cppxdic::io::mat::schema {

std::string_view protocolStructName() {
    return "cond";
}

std::string_view protocolFallbackStructName() {
    return "protocol";
}

std::string_view protocolTitlesField() {
    return "titles";
}

std::string_view protocolTableField() {
    return "table";
}

std::string_view dic3DCombinedVariableName() {
    return "DIC3Dcombined";
}

std::string_view dic3DPPresultsVariableName() {
    return "DIC3DPPresults";
}

} // namespace cppxdic::io::mat::schema

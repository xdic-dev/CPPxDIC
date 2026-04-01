#ifndef CPPXDIC_APP_CLI_H
#define CPPXDIC_APP_CLI_H

#include <ostream>
#include <string>

namespace cppxdic::app {

struct CliOptions {
    std::string subject_override;
    int reftrial_override = -1;
    std::string dic_params_file = "dic_params.txt";
    std::string ncorr_params_file = "ncorr_params.txt";
    std::string viz_params_file = "visualization_params.txt";
    bool show_help = false;
};

CliOptions parseCli(int argc, char* argv[]);
void printUsage(std::ostream& os, const char* prog_name);

} // namespace cppxdic::app

#endif // CPPXDIC_APP_CLI_H

#include "cppxdic/app/cli.h"

#include <getopt.h>
#include <stdexcept>

namespace cppxdic::app {

void printUsage(std::ostream& os, const char* prog_name) {
    os << "Usage: " << prog_name << " [OPTIONS]\n\n"
       << "FINGERTIP 3D RECONSTRUCTION using DIC\n\n"
       << "OPTIONS:\n"
       << "  -s, --subject <id>         Override subject ID (e.g., S09)\n"
       << "  -r, --reftrial <num>       Override reference trial number\n"
       << "  -d, --dic-params <file>    DIC parameters file (default: dic_params.txt)\n"
       << "  -n, --ncorr-params <file>  NCorr parameters file (default: ncorr_params.txt)\n"
       << "  -v, --viz-params <file>    Visualization parameters file (default: visualization_params.txt)\n"
       << "  -h, --help                 Show this help message\n\n"
       << "EXAMPLES:\n"
       << "  " << prog_name << " --subject S10 --reftrial 3\n"
       << "  " << prog_name << " -s S08 -r 5 -d custom_dic.txt\n"
       << std::endl;
}

CliOptions parseCli(int argc, char* argv[]) {
    CliOptions options;

    static option long_options[] = {
        {"subject", required_argument, nullptr, 's'},
        {"reftrial", required_argument, nullptr, 'r'},
        {"dic-params", required_argument, nullptr, 'd'},
        {"ncorr-params", required_argument, nullptr, 'n'},
        {"viz-params", required_argument, nullptr, 'v'},
        {"help", no_argument, nullptr, 'h'},
        {nullptr, 0, nullptr, 0}
    };

    int option_index = 0;
    int opt = 0;
    while ((opt = getopt_long(argc, argv, "s:r:d:n:v:h", long_options, &option_index)) != -1) {
        switch (opt) {
            case 's':
                options.subject_override = optarg;
                break;
            case 'r':
                options.reftrial_override = std::stoi(optarg);
                break;
            case 'd':
                options.dic_params_file = optarg;
                break;
            case 'n':
                options.ncorr_params_file = optarg;
                break;
            case 'v':
                options.viz_params_file = optarg;
                break;
            case 'h':
                options.show_help = true;
                break;
            default:
                throw std::invalid_argument("Invalid command line arguments");
        }
    }

    return options;
}

} // namespace cppxdic::app

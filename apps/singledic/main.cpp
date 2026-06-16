/**
 * @file main.cpp
 * @brief singledic - STUB target for single-camera DIC (no stereo / no 3D reconstruction).
 *
 * Goal: run 2D Digital Image Correlation on a single camera's image sequence, producing
 * in-plane displacement and strain fields, WITHOUT the stereo pairing and 3D surface
 * reconstruction that the full xDIC camerapairs pipeline performs.
 *
 * Status: STUB. It parses the path to the MATLAB reference folder from the CLI (or a config
 * file), logs that the mode is not yet implemented, and exits cleanly with status 0.
 *
 * Build with: cmake -DBUILD_SINGLEDIC=ON ..   (default OFF; not part of default build)
 *
 * @par TODO - what "fully implemented" means
 *  - Read a single-camera image folder (reuse src/input/image_folder_reader once it decodes).
 *  - Load ROI + seed (reuse the .mat/JSON loaders in src/utils.cpp / Utils::loadROIFromMat).
 *  - Run CppNCorr DIC analysis on the single sequence (see apps/proxyncorr for the pattern).
 *  - Run strain analysis and export displacement/strain fields (vtk/ply/csv via Config).
 *  - Validate against the MATLAB single-camera reference noted in README_singledic.md.
 */

#include <iostream>
#include <string>
#include <getopt.h>

/// Print usage for the singledic stub.
static void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [OPTIONS]\n\n"
              << "singledic - single-camera 2D DIC (STUB, not yet implemented)\n\n"
              << "OPTIONS:\n"
              << "  -m, --matlab-ref <path>  Path to the MATLAB reference folder for behaviour\n"
              << "                           comparison (see apps/singledic/README_singledic.md)\n"
              << "  -h, --help               Show this help message\n"
              << std::endl;
}

/**
 * @brief Entry point for the singledic stub.
 * @return 0 on clean exit (stub always exits cleanly).
 */
int main(int argc, char* argv[]) {
    std::string matlab_ref;

    static struct option long_options[] = {
        {"matlab-ref", required_argument, 0, 'm'},
        {"help",       no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt, idx = 0;
    while ((opt = getopt_long(argc, argv, "m:h", long_options, &idx)) != -1) {
        switch (opt) {
            case 'm': matlab_ref = optarg; break;
            case 'h': print_usage(argv[0]); return 0;
            default:  print_usage(argv[0]); return 1;
        }
    }

    std::cout << "singledic: single-camera DIC mode" << std::endl;
    if (!matlab_ref.empty()) {
        std::cout << "MATLAB reference folder: " << matlab_ref << std::endl;
    } else {
        std::cout << "(no --matlab-ref provided; see apps/singledic/README_singledic.md)"
                  << std::endl;
    }
    std::cout << "singledic not yet implemented" << std::endl;
    return 0;
}

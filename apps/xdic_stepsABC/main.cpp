/**
 * xdic_stepsABC - standalone driver for xDIC StepA/B/C utilities (mask/seed + StepC).
 *
 * Covers the in-scope parts of the MATLAB MultiDIC pipeline:
 *   (A) MASK + Seed setup, in GUI (local) or non-GUI (cluster) mode.
 *   (C) StepC stereovision / DLT calibration.
 * (StepA kinematics and StepB friction plots are intentionally out of scope.)
 *
 * The same binary runs interactively (OpenCV highgui) or fully headless. GUI code
 * is compiled only when XDIC_STEPSABC_GUI is defined; with --gui requested but GUI
 * support absent, the tool errors out with a helpful message instead of crashing.
 *
 * See README_xdic_stepsABC.md for CLI, file formats and example commands.
 */

#include <cstring>
#include <fstream>
#include <getopt.h>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <opencv2/opencv.hpp>

#include "stepsABC/mask_seed_setup.h"
#include "stepsABC/stereo_calibration.h"

using namespace cppxdic;
using namespace cppxdic::stepsABC;

namespace {

void printUsage(const char* prog) {
    std::cout
        << "Usage: " << prog << " <command> [OPTIONS]\n\n"
        << "Commands:\n"
        << "  mask-seed     Set up the MASK (polygon ROI) and Seed point(s).\n"
        << "  stepc         Run StepC stereovision / DLT calibration.\n"
        << "  gen-object    Generate cylindrical calibration object 3D coordinates.\n\n"
        << "Common options:\n"
        << "  --gui                 Interactive setup (requires GUI build + a display).\n"
        << "  --no-gui              Headless setup from files (default on cluster).\n"
        << "  -h, --help            Show this help.\n\n"
        << "mask-seed options:\n"
        << "  --image <path>        Reference image (required for --gui; size source for --no-gui).\n"
        << "  --mask <path>         Mask polygon (.poly) or mask image (.png) for --no-gui input.\n"
        << "  --seed <path>         Seed file (.seed) for --no-gui input.\n"
        << "  --num-seeds <n>       Number of seed points to collect in GUI mode (default 1).\n"
        << "  --out <prefix>        Output prefix; writes <prefix>.poly, <prefix>_mask.png, <prefix>.seed.\n\n"
        << "gen-object options:\n"
        << "  --radius <mm>         Cylinder radius (default 30).\n"
        << "  --columns <n>         Dots around circumference (default 18).\n"
        << "  --rows <n>            Dot rows along axis (default 11).\n"
        << "  --dz <mm>             Vertical spacing between rows (default 5).\n"
        << "  --object-out <path>   Output 3D coordinate file (required).\n\n"
        << "stepc options:\n"
        << "  --object <path>       3D calibration object coordinates (x y z per line).\n"
        << "  --cam1 <id> --cam2 <id>         Camera ids for the stereo pair.\n"
        << "  --img1 <path> --img2 <path>     2D image-point files (u v per line) per camera.\n"
        << "  --result-out <path>             Optional text report output.\n\n"
        << "GUI support compiled in: " << (MaskSeedSetup::guiAvailable() ? "YES" : "NO") << "\n";
}

int runMaskSeed(bool gui, const std::string& image_path, const std::string& mask_path,
                const std::string& seed_path, int num_seeds, const std::string& out_prefix) {
    std::string err;

    cv::Mat ref;
    if (!image_path.empty()) {
        ref = cv::imread(image_path, cv::IMREAD_GRAYSCALE);
        if (ref.empty()) {
            std::cerr << "Error: cannot read reference image: " << image_path << "\n";
            return 1;
        }
    }

    MaskSeedResult result;
    if (gui) {
        if (!MaskSeedSetup::guiAvailable()) {
            std::cerr << "Error: --gui requested but this binary was built without GUI "
                         "support (XDIC_STEPSABC_GUI=OFF). Use --no-gui with --mask/--seed, "
                         "or rebuild with -DXDIC_STEPSABC_GUI=ON on a machine with a display.\n";
            return 2;
        }
        if (ref.empty()) {
            std::cerr << "Error: --gui requires --image <path>.\n";
            return 1;
        }
        result = MaskSeedSetup::runGui(ref, num_seeds, err);
    } else {
        if (mask_path.empty() || seed_path.empty()) {
            std::cerr << "Error: --no-gui requires --mask and --seed.\n";
            return 1;
        }
        cv::Size size = ref.empty() ? cv::Size(0, 0) : ref.size();
        result = MaskSeedSetup::loadFromFiles(mask_path, seed_path, size, err);
    }

    if (!result.valid) {
        std::cerr << "Error: mask/seed setup failed: " << err << "\n";
        return 1;
    }

    std::cout << "Mask polygon vertices: " << result.polygon.size() << "\n";
    std::cout << "Seed points: " << result.seeds.size() << "\n";
    for (size_t i = 0; i < result.seeds.size(); ++i) {
        std::cout << "  seed[" << i << "] = (" << result.seeds[i].pw[0] << ", "
                  << result.seeds[i].pw[1] << ")\n";
    }

    if (!out_prefix.empty()) {
        if (!MaskSeedSetup::save(result, out_prefix, err)) {
            std::cerr << "Error: failed to save: " << err << "\n";
            return 1;
        }
        std::cout << "Saved: " << out_prefix << ".poly, " << out_prefix << "_mask.png, "
                  << out_prefix << ".seed\n";
    }
    return 0;
}

int runGenObject(const CylinderCalibSpec& spec, const std::string& out_path) {
    if (out_path.empty()) {
        std::cerr << "Error: gen-object requires --object-out <path>.\n";
        return 1;
    }
    auto pts = StereoCalibration::generateCylindricalObject(spec);
    std::string err;
    if (!StereoCalibration::writeObjectFile(pts, out_path, err)) {
        std::cerr << "Error: " << err << "\n";
        return 1;
    }
    std::cout << "Generated " << pts.size() << " calibration points -> " << out_path << "\n";
    return 0;
}

int runStepC(const std::string& object_path, int cam1, int cam2, const std::string& img1_path,
             const std::string& img2_path, const std::string& result_out) {
    std::string err;
    auto object_points = StereoCalibration::readObjectFile(object_path, err);
    if (object_points.empty()) {
        std::cerr << "Error: " << (err.empty() ? "no object points loaded" : err) << "\n";
        return 1;
    }
    auto img1 = StereoCalibration::readImagePointsFile(img1_path, err);
    auto img2 = StereoCalibration::readImagePointsFile(img2_path, err);
    if (img1.empty() || img2.empty()) {
        std::cerr << "Error: failed to load image-point files (" << img1_path << ", " << img2_path
                  << "): " << err << "\n";
        return 1;
    }

    auto result = StereoCalibration::calibrateStereoPair(cam1, img1, cam2, img2, object_points, err);
    if (!result.valid) {
        std::cerr << "Error: StepC calibration failed: " << err << "\n";
        return 1;
    }

    std::ostringstream report;
    report << "StepC stereovision / DLT calibration\n";
    report << "  cam " << result.cam_first.camera_id
           << " RMS reprojection (px): " << result.cam_first.rms_reprojection << "\n";
    report << "  cam " << result.cam_second.camera_id
           << " RMS reprojection (px): " << result.cam_second.rms_reprojection << "\n";
    report << "  3D reconstruction error (mm): mean=" << result.recon_error.mean
           << " rms=" << result.recon_error.rms << " max=" << result.recon_error.max
           << " std=" << result.recon_error.std << "\n";

    std::cout << report.str();

    if (!result_out.empty()) {
        std::ofstream ofs(result_out);
        if (!ofs) {
            std::cerr << "Error: cannot write result file: " << result_out << "\n";
            return 1;
        }
        ofs << report.str();
        ofs << "# DLT parameters cam " << result.cam_first.camera_id << "\n";
        for (double v : result.cam_first.L)
            ofs << v << "\n";
        ofs << "# DLT parameters cam " << result.cam_second.camera_id << "\n";
        for (double v : result.cam_second.L)
            ofs << v << "\n";
        std::cout << "Wrote report -> " << result_out << "\n";
    }
    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    std::string command = argv[1];
    if (command == "-h" || command == "--help") {
        printUsage(argv[0]);
        return 0;
    }

    // Defaults.
    bool gui = false;
    bool gui_set = false;
    std::string image_path, mask_path, seed_path, out_prefix;
    std::string object_path, object_out, img1_path, img2_path, result_out;
    int num_seeds = 1;
    int cam1 = 1, cam2 = 2;
    CylinderCalibSpec spec;

    enum {
        OPT_GUI = 1000, OPT_NOGUI, OPT_IMAGE, OPT_MASK, OPT_SEED, OPT_NUMSEEDS, OPT_OUT,
        OPT_RADIUS, OPT_COLUMNS, OPT_ROWS, OPT_DZ, OPT_OBJECT_OUT,
        OPT_OBJECT, OPT_CAM1, OPT_CAM2, OPT_IMG1, OPT_IMG2, OPT_RESULT_OUT
    };

    static struct option long_options[] = {
        {"gui", no_argument, 0, OPT_GUI},
        {"no-gui", no_argument, 0, OPT_NOGUI},
        {"image", required_argument, 0, OPT_IMAGE},
        {"mask", required_argument, 0, OPT_MASK},
        {"seed", required_argument, 0, OPT_SEED},
        {"num-seeds", required_argument, 0, OPT_NUMSEEDS},
        {"out", required_argument, 0, OPT_OUT},
        {"radius", required_argument, 0, OPT_RADIUS},
        {"columns", required_argument, 0, OPT_COLUMNS},
        {"rows", required_argument, 0, OPT_ROWS},
        {"dz", required_argument, 0, OPT_DZ},
        {"object-out", required_argument, 0, OPT_OBJECT_OUT},
        {"object", required_argument, 0, OPT_OBJECT},
        {"cam1", required_argument, 0, OPT_CAM1},
        {"cam2", required_argument, 0, OPT_CAM2},
        {"img1", required_argument, 0, OPT_IMG1},
        {"img2", required_argument, 0, OPT_IMG2},
        {"result-out", required_argument, 0, OPT_RESULT_OUT},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}};

    // Parse options starting after the command argument.
    optind = 2;
    int opt;
    while ((opt = getopt_long(argc, argv, "h", long_options, nullptr)) != -1) {
        switch (opt) {
            case OPT_GUI: gui = true; gui_set = true; break;
            case OPT_NOGUI: gui = false; gui_set = true; break;
            case OPT_IMAGE: image_path = optarg; break;
            case OPT_MASK: mask_path = optarg; break;
            case OPT_SEED: seed_path = optarg; break;
            case OPT_NUMSEEDS: num_seeds = std::stoi(optarg); break;
            case OPT_OUT: out_prefix = optarg; break;
            case OPT_RADIUS: spec.radius = std::stod(optarg); break;
            case OPT_COLUMNS: spec.num_columns = std::stoi(optarg); break;
            case OPT_ROWS: spec.num_rows = std::stoi(optarg); break;
            case OPT_DZ: spec.dz = std::stod(optarg); break;
            case OPT_OBJECT_OUT: object_out = optarg; break;
            case OPT_OBJECT: object_path = optarg; break;
            case OPT_CAM1: cam1 = std::stoi(optarg); break;
            case OPT_CAM2: cam2 = std::stoi(optarg); break;
            case OPT_IMG1: img1_path = optarg; break;
            case OPT_IMG2: img2_path = optarg; break;
            case OPT_RESULT_OUT: result_out = optarg; break;
            case 'h': printUsage(argv[0]); return 0;
            default: printUsage(argv[0]); return 1;
        }
    }

    // Default to headless when GUI support is not compiled in.
    if (!gui_set)
        gui = MaskSeedSetup::guiAvailable();

    if (command == "mask-seed") {
        return runMaskSeed(gui, image_path, mask_path, seed_path, num_seeds, out_prefix);
    } else if (command == "gen-object") {
        return runGenObject(spec, object_out);
    } else if (command == "stepc") {
        return runStepC(object_path, cam1, cam2, img1_path, img2_path, result_out);
    }

    std::cerr << "Unknown command: " << command << "\n\n";
    printUsage(argv[0]);
    return 1;
}

/**
 * @file main.cpp
 * @brief singledic — single-camera 2D DIC driver (no stereo, no 3D).
 *
 * Runs Viktoriia's single-camera "sliding analysis" step-D workflow in C++:
 * read one camera's image sequence, saturate, (optionally) bandpass-filter,
 * load/derive a single ROI and seed, run ONE ncorr tracking pass against the
 * reference frame, and export the displacement / correlation fields.
 *
 * MATLAB reference (read-only):
 *   Tools/MultiDIC/lib_script/sliding_analysis/viktoriia_script_analysis/
 *     process_single_trial_ncorr.m, draw_ref_roi_single_trial.m,
 *     draw_ref_seed_single.m, ncorr_matching2ref_single.m
 *   (contrast stereo: Tools/MultiDIC/main_script/stepD_2DDIC.m)
 *
 * Engine reuse: cppxdic::ImageProcessor / cppxdic::ROIManager for
 * saturate/filter/ROI/seed, and ncorr::NcorrSession for the DIC core. Path and
 * parameter handling reuse the production `Config` via a thin SinglediConfig
 * adapter (no shared file is modified).
 */

#include "singledic/singledic_config.h"
#include "singledic/single_dic_workflow.h"

#include "config.h"
#include "logging.h"

#include <getopt.h>
#include <iostream>
#include <string>

static void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [OPTIONS]\n\n"
              << "singledic - single-camera 2D DIC (no stereo / no 3D reconstruction)\n\n"
              << "OPTIONS:\n"
              << "  -c, --config <file>      Unified config file (reuses Config; key=value)\n"
              << "      --dic-params <file>  dic_params.txt to load step-D/ncorr tunables\n"
              << "      --data-path <dir>    Base data path (videos live under <dir>/vid)\n"
              << "      --dic-path <dir>     Output/analysis base path\n"
              << "  -s, --subject <id>       Subject folder name (e.g. S01)\n"
              << "  -b, --bloc <id>          Bloc/block folder name\n"
              << "  -t, --trial <id>         Trial name (video stem)\n"
              << "  -r, --reftrial <id>      Reference trial name (default: trial)\n"
              << "      --start <n>          First frame (1-based, default 1)\n"
              << "      --end <n>            Last frame (default: end of sequence)\n"
              << "      --jump <n>           Frame stride (default 1)\n"
              << "      --dir <forward|backward>  Tracking direction (default forward)\n"
              << "      --ncorr-number <n>   Output tag (1/2) for forward/backward (default 1)\n"
              << "      --gs-low <n>         Saturation low clamp (default 40)\n"
              << "      --gs-high <n>        Saturation high clamp (default 140)\n"
              << "      --filter             Enable bandpass filtering before DIC\n"
              << "      --filt-low <n>       Bandpass low cutoff (default 50)\n"
              << "      --filt-high <n>      Bandpass high cutoff (default 200)\n"
              << "      --no-mat             Do not write .mat sidecar\n"
              << "      --no-csv             Do not write .csv output\n"
              << "      --matlab-ref <path>  (informational) MATLAB reference folder\n"
              << "  -h, --help               Show this help\n"
              << std::endl;
}

int main(int argc, char* argv[]) {
    Config base_config; // production defaults

    std::string config_file;
    std::string dic_params_file;
    singledic::SinglediConfig cfg; // wraps base_config below

    // Local holders for path overrides (applied to base_config before wrap).
    std::string data_path_override, dic_path_override;
    std::string subject = cfg.subject, bloc = cfg.bloc, trial = cfg.trial, reftrial;
    int start = cfg.idx_frame_start, end = cfg.idx_frame_end, jump = cfg.frame_jump;
    std::string tracking_dir = cfg.tracking_dir;
    int ncorr_number = cfg.ncorr_number;
    int gs_low = cfg.limit_grayscale_low, gs_high = cfg.limit_grayscale_high;
    bool do_filter = cfg.filter_im_mode;
    int filt_low = cfg.param_filt_low, filt_high = cfg.param_filt_high;
    bool write_mat = cfg.write_mat, write_csv = cfg.write_csv;
    std::string matlab_ref;

    enum {
        OPT_DIC_PARAMS = 1000,
        OPT_DATA_PATH,
        OPT_DIC_PATH,
        OPT_START,
        OPT_END,
        OPT_JUMP,
        OPT_DIR,
        OPT_NCORR_NUMBER,
        OPT_GS_LOW,
        OPT_GS_HIGH,
        OPT_FILTER,
        OPT_FILT_LOW,
        OPT_FILT_HIGH,
        OPT_NO_MAT,
        OPT_NO_CSV,
        OPT_MATLAB_REF
    };
    static struct option long_options[] = {{"config", required_argument, 0, 'c'},
                                           {"dic-params", required_argument, 0, OPT_DIC_PARAMS},
                                           {"data-path", required_argument, 0, OPT_DATA_PATH},
                                           {"dic-path", required_argument, 0, OPT_DIC_PATH},
                                           {"subject", required_argument, 0, 's'},
                                           {"bloc", required_argument, 0, 'b'},
                                           {"trial", required_argument, 0, 't'},
                                           {"reftrial", required_argument, 0, 'r'},
                                           {"start", required_argument, 0, OPT_START},
                                           {"end", required_argument, 0, OPT_END},
                                           {"jump", required_argument, 0, OPT_JUMP},
                                           {"dir", required_argument, 0, OPT_DIR},
                                           {"ncorr-number", required_argument, 0, OPT_NCORR_NUMBER},
                                           {"gs-low", required_argument, 0, OPT_GS_LOW},
                                           {"gs-high", required_argument, 0, OPT_GS_HIGH},
                                           {"filter", no_argument, 0, OPT_FILTER},
                                           {"filt-low", required_argument, 0, OPT_FILT_LOW},
                                           {"filt-high", required_argument, 0, OPT_FILT_HIGH},
                                           {"no-mat", no_argument, 0, OPT_NO_MAT},
                                           {"no-csv", no_argument, 0, OPT_NO_CSV},
                                           {"matlab-ref", required_argument, 0, OPT_MATLAB_REF},
                                           {"help", no_argument, 0, 'h'},
                                           {0, 0, 0, 0}};

    int opt, idx = 0;
    while ((opt = getopt_long(argc, argv, "c:s:b:t:r:h", long_options, &idx)) != -1) {
        switch (opt) {
            case 'c':
                config_file = optarg;
                break;
            case OPT_DIC_PARAMS:
                dic_params_file = optarg;
                break;
            case OPT_DATA_PATH:
                data_path_override = optarg;
                break;
            case OPT_DIC_PATH:
                dic_path_override = optarg;
                break;
            case 's':
                subject = optarg;
                break;
            case 'b':
                bloc = optarg;
                break;
            case 't':
                trial = optarg;
                break;
            case 'r':
                reftrial = optarg;
                break;
            case OPT_START:
                start = std::stoi(optarg);
                break;
            case OPT_END:
                end = std::stoi(optarg);
                break;
            case OPT_JUMP:
                jump = std::stoi(optarg);
                break;
            case OPT_DIR:
                tracking_dir = optarg;
                break;
            case OPT_NCORR_NUMBER:
                ncorr_number = std::stoi(optarg);
                break;
            case OPT_GS_LOW:
                gs_low = std::stoi(optarg);
                break;
            case OPT_GS_HIGH:
                gs_high = std::stoi(optarg);
                break;
            case OPT_FILTER:
                do_filter = true;
                break;
            case OPT_FILT_LOW:
                filt_low = std::stoi(optarg);
                break;
            case OPT_FILT_HIGH:
                filt_high = std::stoi(optarg);
                break;
            case OPT_NO_MAT:
                write_mat = false;
                break;
            case OPT_NO_CSV:
                write_csv = false;
                break;
            case OPT_MATLAB_REF:
                matlab_ref = optarg;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    // Tier the config the same way as the main driver: config file, then
    // dic_params.txt, then compiled defaults.
    if (!config_file.empty()) base_config.loadFromConfigFile(config_file);
    if (!dic_params_file.empty()) base_config.loadFromDicParamsFile(dic_params_file);
    if (!data_path_override.empty()) base_config.data_path = data_path_override;
    if (!dic_path_override.empty()) base_config.dic_path = dic_path_override;
    base_config.updateVariables();

    // Build the single-camera adapter on top of the resolved Config.
    cfg = singledic::SinglediConfig(base_config);
    cfg.subject = subject;
    cfg.bloc = bloc;
    cfg.trial = trial;
    cfg.reftrial = reftrial.empty() ? trial : reftrial;
    cfg.idx_frame_start = start;
    cfg.idx_frame_end = end;
    cfg.frame_jump = jump;
    cfg.tracking_dir = tracking_dir;
    cfg.ncorr_number = ncorr_number;
    cfg.limit_grayscale_low = gs_low;
    cfg.limit_grayscale_high = gs_high;
    cfg.filter_im_mode = do_filter;
    cfg.param_filt_low = filt_low;
    cfg.param_filt_high = filt_high;
    cfg.write_mat = write_mat;
    cfg.write_csv = write_csv;

    if (!matlab_ref.empty()) LOG_INFO << "MATLAB reference folder: " << matlab_ref;

    singledic::SingleDicWorkflow workflow(cfg);
    singledic::SingleDicResult res = workflow.run();

    if (!res.ok) {
        LOG_ERROR << "singledic: FAILED — " << res.message;
        return 1;
    }
    LOG_INFO << "singledic: OK — tracked " << res.frames.size() << " frame(s).";
    return 0;
}

/**
 * CPPXDIC - C++ equivalent of Matlab xDIC
 * Main entry point for FINGERTIP 3D RECONSTRUCTION using DIC
 *
 * This is the C++ port of the Matlab xDIC library using the ncorr C++ library
 */

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <chrono>
#include <getopt.h>
#include "config.h"
#include "dic_analysis.h"
#include "logging.h"
#include "stage_plan.h"
#include "trial_selection.h"
#include "utils.h"
#include "xdic/xdic_mode.h"

// Mode-specific dispatch headers (stubs for non-default modes).
#if defined(XDIC_MODE_MIRRORED)
#include "xdic/mirrored/mirrored_mode.h"
#endif

// Long-option ids for trial-level selection flags (no short equivalents).
enum {
    OPT_TRIAL = 1000,
    OPT_TRIALS,
    OPT_TRIALS_FILE,
    OPT_SUBJECT_TRIAL_CSV,
    OPT_LOG_LEVEL,
    OPT_LOG_FILE,
    OPT_DEBUG,
    OPT_STAGES,
    OPT_PAIR,
    OPT_CAM
};

// Print usage information
void print_usage(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " [OPTIONS]\n\n"
              << "FINGERTIP 3D RECONSTRUCTION using DIC\n\n"
              << "OPTIONS:\n"
              << "  -s, --subject <id>         Override subject ID (e.g., S09)\n"
              << "  -r, --reftrial <num>       Override reference trial number\n"
              << "  -C, --config <file>        Unified config file (default: config/default.cfg)\n"
              << "  -d, --dic-params <file>    DIC parameters file (default: dic_params.txt)\n"
              << "  -n, --ncorr-params <file>  NCorr parameters file (default: ncorr_params.txt)\n"
              << "  -v, --viz-params <file>    Visualization parameters file (default: "
                 "visualization_params.txt)\n"
              << "  -h, --help                 Show this help message\n\n"
              << "LOGGING:\n"
              << "  -V, --verbose              Increase console verbosity (repeatable: -VV)\n"
              << "  -q, --quiet                Quiet: only warnings and errors on the console\n"
              << "      --log-level <lvl>      Console level: trace|debug|info|warn|error|off\n"
              << "      --log-file <path>      Also write a full-detail (debug) log to <path>\n"
              << "      --debug                Debug mode: debug-level console + source locations\n"
              << "  Env: CPPXDIC_LOG_LEVEL, CPPXDIC_LOG_FILE, CPPXDIC_LOG_CONSOLE also apply.\n\n"
              << "TRIAL-LEVEL RUN MODES (mutually exclusive; pick at most one):\n"
              << "  (a) --trial <id>             Run a single trial.\n"
              << "  (b) --trials <list>          Trial list (e.g. 7,12,25); with SLURM_ARRAY_TASK_ID\n"
              << "      --trials-file <path>     set, the Nth (1-based) trial is run for this task.\n"
              << "  (c) --subject-trial-csv <p>  subject_trial.csv (header subject,trial). With\n"
              << "                               SLURM_ARRAY_TASK_ID set, the Nth data line selects\n"
              << "                               both subject and trial for this task.\n"
              << "  Without SLURM_ARRAY_TASK_ID, --trials/--trials-file run the whole list and\n"
              << "  --subject-trial-csv runs every row (grouped per subject).\n\n"
              << "STAGE DECOMPOSITION (run sub-steps in separate processes; needs explicit trials):\n"
              << "      --stages <list>          Comma list of stages to run. Tokens:\n"
              << "                                 all | d(=match,track,format) | match | track |\n"
              << "                                 format | e(=recon) | f(=deform).  Default: all.\n"
              << "      --pair <p>               Restrict Step-D work to stereopair p (1..num_pair).\n"
              << "      --cam <id>               Restrict tracking to camera id (use with --stages track).\n"
              << "  Each sub-step checkpoints to / loads from disk, so e.g. a 'match' job, then\n"
              << "  per-camera 'track' jobs, then a 'format,e,f' job can run as separate SLURM tasks.\n\n"
              << "EXAMPLES:\n"
              << "  " << prog_name << " --subject S10 --reftrial 3\n"
              << "  " << prog_name << " -s S08 -r 5 -d custom_dic.txt\n"
              << "  " << prog_name << " --subject S09 --trial 7\n"
              << "  SLURM_ARRAY_TASK_ID=$SLURM_ARRAY_TASK_ID " << prog_name
              << " --subject S09 --trials 7,12,25\n"
              << "  SLURM_ARRAY_TASK_ID=$SLURM_ARRAY_TASK_ID " << prog_name
              << " --subject-trial-csv subject_trial.csv\n"
              << "  " << prog_name << " --subject S09 --trial 7 --pair 1 --stages match\n"
              << "  " << prog_name << " --subject S09 --trial 7 --pair 1 --cam 1 --stages track\n"
              << "  " << prog_name << " --subject S09 --trial 7 --stages format,e,f\n"
              << std::endl;
}

int main(int argc, char* argv[]) {
    // Command-line argument parsing
    std::string subject_override = "";
    int reftrial_override = -1;
    std::string config_file = "config/default.cfg";
    std::string dic_params_file = "dic_params.txt";
    std::string ncorr_params_file = "ncorr_params.txt";
    std::string viz_params_file = "visualization_params.txt";

    // Trial-level run-mode selectors (empty/unset => default run()).
    int single_trial = -1;
    std::string trials_spec;       // --trials <list>
    std::string trials_file;       // --trials-file <path>
    std::string subject_trial_csv; // --subject-trial-csv <path>

    // Logging selectors (CLI overrides config-file values, which override env).
    std::string cli_log_level;
    std::string cli_log_file;
    int verbose_count = 0;
    bool quiet = false;
    bool cli_debug = false;

    // Stage decomposition selectors (empty => full pipeline, as before).
    std::string stages_spec;   // --stages e.g. "match" | "track" | "format,e,f" | "d"
    int only_pair = 0;         // --pair  (0 = all stereopairs)
    int only_cam = 0;          // --cam   (0 = both cameras within a pair)

    static struct option long_options[] = {{"subject", required_argument, 0, 's'},
                                           {"reftrial", required_argument, 0, 'r'},
                                           {"config", required_argument, 0, 'C'},
                                           {"dic-params", required_argument, 0, 'd'},
                                           {"ncorr-params", required_argument, 0, 'n'},
                                           {"viz-params", required_argument, 0, 'v'},
                                           {"trial", required_argument, 0, OPT_TRIAL},
                                           {"trials", required_argument, 0, OPT_TRIALS},
                                           {"trials-file", required_argument, 0, OPT_TRIALS_FILE},
                                           {"subject-trial-csv", required_argument, 0, OPT_SUBJECT_TRIAL_CSV},
                                           {"verbose", no_argument, 0, 'V'},
                                           {"quiet", no_argument, 0, 'q'},
                                           {"log-level", required_argument, 0, OPT_LOG_LEVEL},
                                           {"log-file", required_argument, 0, OPT_LOG_FILE},
                                           {"debug", no_argument, 0, OPT_DEBUG},
                                           {"stages", required_argument, 0, OPT_STAGES},
                                           {"pair", required_argument, 0, OPT_PAIR},
                                           {"cam", required_argument, 0, OPT_CAM},
                                           {"help", no_argument, 0, 'h'},
                                           {0, 0, 0, 0}};

    int opt;
    int option_index = 0;
    while ((opt = getopt_long(argc, argv, "s:r:C:d:n:v:hVq", long_options, &option_index)) != -1) {
        switch (opt) {
            case 's':
                subject_override = optarg;
                break;
            case 'C':
                config_file = optarg;
                break;
            case 'r':
                reftrial_override = std::stoi(optarg);
                break;
            case 'd':
                dic_params_file = optarg;
                break;
            case 'n':
                ncorr_params_file = optarg;
                break;
            case 'v':
                viz_params_file = optarg;
                break;
            case OPT_STAGES:
                stages_spec = optarg;
                break;
            case OPT_PAIR:
                only_pair = std::stoi(optarg);
                break;
            case OPT_CAM:
                only_cam = std::stoi(optarg);
                break;
            case OPT_TRIAL:
                single_trial = std::stoi(optarg);
                break;
            case OPT_TRIALS:
                trials_spec = optarg;
                break;
            case OPT_TRIALS_FILE:
                trials_file = optarg;
                break;
            case OPT_SUBJECT_TRIAL_CSV:
                subject_trial_csv = optarg;
                break;
            case 'V':
                ++verbose_count;
                break;
            case 'q':
                quiet = true;
                break;
            case OPT_LOG_LEVEL:
                cli_log_level = optarg;
                break;
            case OPT_LOG_FILE:
                cli_log_file = optarg;
                break;
            case OPT_DEBUG:
                cli_debug = true;
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    // Configure logging as early as possible from CLI + environment so that all
    // subsequent output (including config loading) is leveled and, if requested,
    // captured to the log file. Config-file values are merged in after load.
    {
        cppxdic::log::Options logopts;
        if (!cli_log_level.empty()) {
            logopts.console_level = cppxdic::log::levelFromString(cli_log_level);
            logopts.console_level_set = true;
        }
        logopts.log_file = cli_log_file;
        logopts.debug = cli_debug;
        logopts.verbose = verbose_count;
        logopts.quiet = quiet;
        cppxdic::log::configureFromOptions(logopts);
    }

    LOG_INFO << "FINGERTIP 3D RECONSTRUCTION using DIC";
    LOG_INFO << "--------------------------------------";
    LOG_INFO << "DIC analysis for the fingertip";

    // ---- Resolve trial-level run mode (Part 2) --------------------------------
    // Exactly one of the four selectors may be used. They produce either an
    // explicit list of trials to run (selected_trials), and possibly a
    // CSV-driven subject override. If none is used, selected_trials stays empty
    // and the default DicAnalysis::run() path is taken (unchanged behaviour).
    std::vector<int> selected_trials;
    {
        int mode_count = (single_trial >= 0 ? 1 : 0) + (!trials_spec.empty() ? 1 : 0) +
                         (!trials_file.empty() ? 1 : 0) + (!subject_trial_csv.empty() ? 1 : 0);
        if (mode_count > 1) {
            LOG_ERROR << "--trial, --trials, --trials-file and --subject-trial-csv "
                         "are mutually exclusive.";
            return 1;
        }

        const int task_id = cppxdic::slurmArrayTaskId();
        try {
            if (single_trial >= 0) {
                // (a) single trial
                selected_trials = {single_trial};
            } else if (!trials_spec.empty() || !trials_file.empty()) {
                // (b) SLURM array over a list of trials
                std::vector<int> trials = trials_file.empty()
                                              ? cppxdic::parseTrialList(trials_spec)
                                              : cppxdic::readTrialsFile(trials_file);
                if (trials.empty()) {
                    LOG_ERROR << "trial list is empty.";
                    return 1;
                }
                if (task_id >= 0) {
                    if (task_id < 1 || static_cast<size_t>(task_id) > trials.size()) {
                        LOG_ERROR << "SLURM_ARRAY_TASK_ID=" << task_id
                                  << " out of range 1.." << trials.size();
                        return 1;
                    }
                    selected_trials = {trials[task_id - 1]};
                    LOG_INFO << "[SLURM array] task " << task_id << " -> trial "
                             << selected_trials.front();
                } else {
                    selected_trials = trials;  // no array: run the whole list
                }
            } else if (!subject_trial_csv.empty()) {
                // (c) SLURM array over (subject, trial) pairs from CSV
                std::vector<cppxdic::SubjectTrial> rows =
                    cppxdic::readSubjectTrialCsv(subject_trial_csv);
                if (rows.empty()) {
                    LOG_ERROR << "subject_trial CSV has no data rows: " << subject_trial_csv;
                    return 1;
                }
                if (task_id >= 0) {
                    if (task_id < 1 || static_cast<size_t>(task_id) > rows.size()) {
                        LOG_ERROR << "SLURM_ARRAY_TASK_ID=" << task_id
                                  << " out of range 1.." << rows.size();
                        return 1;
                    }
                    const auto& row = rows[task_id - 1];
                    subject_override = row.subject;  // CSV drives the subject
                    selected_trials = {row.trial};
                    LOG_INFO << "[SLURM array] task " << task_id << " -> subject "
                             << row.subject << ", trial " << row.trial;
                } else {
                    // No array: process every row. Require all rows share one
                    // subject so a single Config run is well defined; otherwise
                    // ask the user to drive it via a SLURM array (or per-subject).
                    const std::string& subj0 = rows.front().subject;
                    for (const auto& row : rows) {
                        if (row.subject != subj0) {
                            LOG_ERROR << "subject_trial CSV spans multiple subjects ("
                                      << subj0 << ", " << row.subject
                                      << "). Use SLURM_ARRAY_TASK_ID to select a row, "
                                         "or split the CSV per subject.";
                            return 1;
                        }
                        selected_trials.push_back(row.trial);
                    }
                    subject_override = subj0;
                }
            }
        } catch (const std::exception& e) {
            LOG_ERROR << "Error resolving trial selection: " << e.what();
            return 1;
        }
    }

    try {
        // Three-tier override chain (lowest -> highest priority):
        //   1. Compiled defaults  (Config member initializers)
        //   2. Config file(s)     (unified config/default.cfg, then specific param files)
        //   3. CLI arguments      (override everything)
        //
        // Tier 1: compiled defaults (from class initialization)
        Config config;

        LOG_INFO << "Loading configuration files...";
        LOG_INFO << "------------------------------";

        // Tier 2a: unified config file (broadest config-file source)
        config.loadFromConfigFile(config_file);

        // Tier 2b: specific param files override the unified file where present
        config.loadFromDicParamsFile(dic_params_file);
        config.loadFromNcorrParamsFile(ncorr_params_file);
        config.loadFromVisualizationParamsFile(viz_params_file);

        // Tier 3: command-line overrides (highest priority)
        if (!subject_override.empty()) {
            config.overrideSubject(subject_override);
        }
        if (reftrial_override >= 0) {
            config.overrideRefTrial(reftrial_override);
        }

        // Update derived variables
        config.updateVariables();

        // Re-apply logging configuration now that config files are loaded, merging
        // their values (log_level/log_file/debug_mode) under any CLI flags, which
        // keep priority. Env was already applied in the early configure above.
        {
            cppxdic::log::Options logopts;
            if (!config.log_level.empty()) {
                logopts.console_level = cppxdic::log::levelFromString(config.log_level);
                logopts.console_level_set = true;
            }
            logopts.log_file = config.log_file;
            logopts.debug = config.debug_mode;
            // CLI flags override config-file values.
            if (!cli_log_level.empty()) {
                logopts.console_level = cppxdic::log::levelFromString(cli_log_level);
                logopts.console_level_set = true;
            }
            if (!cli_log_file.empty()) logopts.log_file = cli_log_file;
            if (cli_debug) logopts.debug = true;
            logopts.verbose = verbose_count;
            logopts.quiet = quiet;
            cppxdic::log::configureFromOptions(logopts);
        }

        // Announce the compile-time reconstruction mode (Section 2c).
        LOG_INFO << "xDIC reconstruction mode: " << xdic::active_mode_name();

        // Print final parameters
        LOG_INFO << "Final Configuration:";
        LOG_INFO << "--------------------";
        LOG_INFO << "Subject: " << config.subject_id << ", Phase: " << config.phase_id
                 << ", Material: " << config.material << ", Stereo Pairs: " << config.num_pair;
        LOG_INFO << "Reference trial number: " << config.ref_trial_id;
        LOG_INFO << "Frame: " << config.idx_frame_start << " to " << config.idx_frame_end
                 << ", jump= " << config.frame_jump;
        LOG_INFO << "Show visualization: " << config.showvisu
                 << ", Debug mode: " << config.debug_mode
                 << ", Automatic process: " << config.automatic_process;

#if defined(XDIC_MODE_CAMERAPAIRS)
        // ---- Camera-pairs mode: the only fully-implemented reconstruction path. ----
        // Checking
        LOG_INFO << "Checking the data and protocol...";
        if (!Utils::dicCheck(config)) {
            LOG_ERROR << "Data and protocol check failed!";
            return 1;
        }
        // Build the stage plan (default = full pipeline; unchanged behaviour).
        StagePlan plan;
        if (!stages_spec.empty()) {
            std::string perr;
            if (!StagePlan::parse(stages_spec, plan, &perr)) {
                LOG_ERROR << "Invalid --stages '" << stages_spec << "': " << perr
                          << " (valid tokens: all, d, match, track, format, e, f)";
                return 1;
            }
        }
        plan.only_pair = only_pair;
        plan.only_cam = only_cam;
        const bool custom_plan = !stages_spec.empty() || only_pair != 0 || only_cam != 0;

        // Stage decomposition is meant for explicit (array-driven) trial selection.
        if (custom_plan && selected_trials.empty()) {
            LOG_ERROR << "--stages/--pair/--cam require an explicit trial selection "
                         "(--trial / --trials / --trials-file / --subject-trial-csv).";
            return 1;
        }

        // Call analysis function. If a trial-level run mode was selected, run
        // the resolved trial list; otherwise use the default run() behaviour.
        DicAnalysis dicAnalysis(config);
        bool success;
        if (!selected_trials.empty()) {
            std::ostringstream trial_list;
            for (size_t i = 0; i < selected_trials.size(); ++i) {
                trial_list << selected_trials[i];
                if (i + 1 < selected_trials.size()) trial_list << ", ";
            }
            LOG_INFO << "Running selected trials: [" << trial_list.str() << "]";
            if (custom_plan) {
                LOG_INFO << "Stage selection: stages='" << (stages_spec.empty() ? "all" : stages_spec)
                         << "' pair=" << only_pair << " cam=" << only_cam;
            }
            success = dicAnalysis.run(selected_trials, plan);
        } else {
            success = dicAnalysis.run();
        }

        if (success) {
            LOG_INFO << "Analysis completed successfully!";
        } else {
            LOG_ERROR << "Analysis failed!";
            return 1;
        }
#elif defined(XDIC_MODE_MIRRORED)
        // ---- Mirrored-camera mode: STUB (Section 2d). ----
        if (!xdic::mirrored::run(config)) {
            LOG_ERROR << "Mirrored mode failed.";
            return 1;
        }
#elif defined(XDIC_MODE_MULTI)
// ---- Multi-camera mode: STUB (Section 2e). ----
// The multi-mode placeholder intentionally fails to compile (see
// src/xdic/multi/multi_mode.cpp), so this branch is only reachable when that
// translation unit is added to the build under XDIC_MODE=multi.
#error "xdic multi-camera mode is not implemented"
#endif

    } catch (const std::exception& e) {
        LOG_ERROR << e.what();
        return 1;
    }

    LOG_INFO << "End of script";
    return 0;
}

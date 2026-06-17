/**
 * CPPXDIC - C++ equivalent of Matlab xDIC
 * Main entry point for FINGERTIP 3D RECONSTRUCTION using DIC
 *
 * This is the C++ port of the Matlab xDIC library using the ncorr C++ library
 */

#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <getopt.h>
#include "config.h"
#include "dic_analysis.h"
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
    OPT_SUBJECT_TRIAL_CSV
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
              << "TRIAL-LEVEL RUN MODES (mutually exclusive; pick at most one):\n"
              << "  (a) --trial <id>             Run a single trial.\n"
              << "  (b) --trials <list>          Trial list (e.g. 7,12,25); with SLURM_ARRAY_TASK_ID\n"
              << "      --trials-file <path>     set, the Nth (1-based) trial is run for this task.\n"
              << "  (c) --subject-trial-csv <p>  subject_trial.csv (header subject,trial). With\n"
              << "                               SLURM_ARRAY_TASK_ID set, the Nth data line selects\n"
              << "                               both subject and trial for this task.\n"
              << "  Without SLURM_ARRAY_TASK_ID, --trials/--trials-file run the whole list and\n"
              << "  --subject-trial-csv runs every row (grouped per subject).\n\n"
              << "EXAMPLES:\n"
              << "  " << prog_name << " --subject S10 --reftrial 3\n"
              << "  " << prog_name << " -s S08 -r 5 -d custom_dic.txt\n"
              << "  " << prog_name << " --subject S09 --trial 7\n"
              << "  SLURM_ARRAY_TASK_ID=$SLURM_ARRAY_TASK_ID " << prog_name
              << " --subject S09 --trials 7,12,25\n"
              << "  SLURM_ARRAY_TASK_ID=$SLURM_ARRAY_TASK_ID " << prog_name
              << " --subject-trial-csv subject_trial.csv\n"
              << std::endl;
}

int main(int argc, char* argv[]) {
    std::cout << "FINGERTIP 3D RECONSTRUCTION using DIC" << std::endl;
    std::cout << "--------------------------------------" << std::endl;
    std::cout << "DIC analysis for the fingertip" << std::endl;
    std::cout << std::endl;

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
                                           {"help", no_argument, 0, 'h'},
                                           {0, 0, 0, 0}};

    int opt;
    int option_index = 0;
    while ((opt = getopt_long(argc, argv, "s:r:C:d:n:v:h", long_options, &option_index)) != -1) {
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
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

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
            std::cerr << "Error: --trial, --trials, --trials-file and --subject-trial-csv "
                         "are mutually exclusive."
                      << std::endl;
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
                    std::cerr << "Error: trial list is empty." << std::endl;
                    return 1;
                }
                if (task_id >= 0) {
                    if (task_id < 1 || static_cast<size_t>(task_id) > trials.size()) {
                        std::cerr << "Error: SLURM_ARRAY_TASK_ID=" << task_id
                                  << " out of range 1.." << trials.size() << std::endl;
                        return 1;
                    }
                    selected_trials = {trials[task_id - 1]};
                    std::cout << "[SLURM array] task " << task_id << " -> trial "
                              << selected_trials.front() << std::endl;
                } else {
                    selected_trials = trials;  // no array: run the whole list
                }
            } else if (!subject_trial_csv.empty()) {
                // (c) SLURM array over (subject, trial) pairs from CSV
                std::vector<cppxdic::SubjectTrial> rows =
                    cppxdic::readSubjectTrialCsv(subject_trial_csv);
                if (rows.empty()) {
                    std::cerr << "Error: subject_trial CSV has no data rows: "
                              << subject_trial_csv << std::endl;
                    return 1;
                }
                if (task_id >= 0) {
                    if (task_id < 1 || static_cast<size_t>(task_id) > rows.size()) {
                        std::cerr << "Error: SLURM_ARRAY_TASK_ID=" << task_id
                                  << " out of range 1.." << rows.size() << std::endl;
                        return 1;
                    }
                    const auto& row = rows[task_id - 1];
                    subject_override = row.subject;  // CSV drives the subject
                    selected_trials = {row.trial};
                    std::cout << "[SLURM array] task " << task_id << " -> subject "
                              << row.subject << ", trial " << row.trial << std::endl;
                } else {
                    // No array: process every row. Require all rows share one
                    // subject so a single Config run is well defined; otherwise
                    // ask the user to drive it via a SLURM array (or per-subject).
                    const std::string& subj0 = rows.front().subject;
                    for (const auto& row : rows) {
                        if (row.subject != subj0) {
                            std::cerr << "Error: subject_trial CSV spans multiple subjects ("
                                      << subj0 << ", " << row.subject
                                      << "). Use SLURM_ARRAY_TASK_ID to select a row, "
                                         "or split the CSV per subject."
                                      << std::endl;
                            return 1;
                        }
                        selected_trials.push_back(row.trial);
                    }
                    subject_override = subj0;
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "Error resolving trial selection: " << e.what() << std::endl;
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

        std::cout << "Loading configuration files..." << std::endl;
        std::cout << "------------------------------" << std::endl;

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
        std::cout << std::endl;

        // Announce the compile-time reconstruction mode (Section 2c).
        std::cout << "xDIC reconstruction mode: " << xdic::active_mode_name() << std::endl;

        // Print final parameters
        std::cout << "Final Configuration:" << std::endl;
        std::cout << "--------------------" << std::endl;
        std::cout << "Subject: " << config.subject_id << ", Phase: " << config.phase_id
                  << ", Material: " << config.material << ", Stereo Pairs: " << config.num_pair
                  << std::endl;
        std::cout << "Reference trial number: " << config.ref_trial_id << std::endl;
        std::cout << "Frame: " << config.idx_frame_start << " to " << config.idx_frame_end
                  << ", jump= " << config.frame_jump << std::endl;
        std::cout << "Show visualization: " << config.showvisu
                  << ", Debug mode: " << config.debug_mode
                  << ", Automatic process: " << config.automatic_process << std::endl;
        std::cout << std::endl;

#if defined(XDIC_MODE_CAMERAPAIRS)
        // ---- Camera-pairs mode: the only fully-implemented reconstruction path. ----
        // Checking
        std::cout << "Checking the data and protocol..." << std::endl;
        if (!Utils::dicCheck(config)) {
            std::cerr << "Data and protocol check failed!" << std::endl;
            return 1;
        }
        // Call analysis function. If a trial-level run mode was selected, run
        // the resolved trial list; otherwise use the default run() behaviour.
        DicAnalysis dicAnalysis(config);
        bool success;
        if (!selected_trials.empty()) {
            std::cout << "Running selected trials: [";
            for (size_t i = 0; i < selected_trials.size(); ++i) {
                std::cout << selected_trials[i];
                if (i + 1 < selected_trials.size()) std::cout << ", ";
            }
            std::cout << "]" << std::endl;
            success = dicAnalysis.run(selected_trials);
        } else {
            success = dicAnalysis.run();
        }

        if (success) {
            std::cout << "Analysis completed successfully!" << std::endl;
        } else {
            std::cerr << "Analysis failed!" << std::endl;
            return 1;
        }
#elif defined(XDIC_MODE_MIRRORED)
        // ---- Mirrored-camera mode: STUB (Section 2d). ----
        if (!xdic::mirrored::run(config)) {
            std::cerr << "Mirrored mode failed." << std::endl;
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
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "End of script" << std::endl;
    return 0;
}

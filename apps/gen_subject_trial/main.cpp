/**
 * gen_subject_trial - subject_trial.csv generator.
 *
 * For each requested subject, looks up the subject's list of trials by reusing
 * DicAnalysis::searchTrialTarget(subject) (which parses the protocol .mat), then
 * writes one "subject,trial" line per (subject, trial) pair to a CSV.
 *
 * The CSV is consumed by the trial-level experiment runner (cppxdic
 * --subject-trial-csv) for SLURM array jobs over (subject, trial) pairs.
 *
 * This target is additive and OFF by default (BUILD_GEN_SUBJECT_TRIAL).
 */

#include <fstream>
#include <getopt.h>
#include <iostream>
#include <string>
#include <vector>

#include "config.h"
#include "dic_analysis.h"
#include "logging.h"
#include "trial_selection.h"

using cppxdic::trim;

static void print_usage(const char* prog) {
    std::cout
        << "Usage: " << prog << " [OPTIONS] [SUBJECT ...]\n\n"
        << "Generate subject_trial.csv by looking up each subject's trials\n"
        << "from its protocol .mat (reuses DicAnalysis::searchTrialTarget).\n\n"
        << "OPTIONS:\n"
        << "  -S, --subjects <list>      Comma/space-separated subjects (e.g. S08,S09,S10)\n"
        << "  -f, --subjects-file <path> File with one subject id per line (# = comment)\n"
        << "  -o, --output <path>        Output CSV path (default: subject_trial.csv)\n"
        << "  -d, --dic-params <file>    DIC parameters file (default: dic_params.txt)\n"
        << "  -n, --ncorr-params <file>  NCorr parameters file (default: ncorr_params.txt)\n"
        << "  -v, --viz-params <file>    Visualization params (default: visualization_params.txt)\n"
        << "  -h, --help                 Show this help\n\n"
        << "Subjects may also be passed as positional arguments.\n\n"
        << "EXAMPLES:\n"
        << "  " << prog << " --subjects S08,S09,S10 -o subject_trial.csv\n"
        << "  " << prog << " --subjects-file subjects.txt\n"
        << "  " << prog << " S08 S09 S10\n"
        << std::endl;
}

static std::vector<std::string> read_subjects_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("Cannot open subjects file: " + path);
    }
    std::vector<std::string> subjects;
    std::string line;
    while (std::getline(in, line)) {
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        subjects.push_back(t);
    }
    return subjects;
}

static std::vector<std::string> parse_subject_list(const std::string& spec) {
    std::vector<std::string> out;
    std::string token;
    for (char c : spec) {
        if (c == ',' || c == ' ' || c == '\t' || c == ';') {
            if (!token.empty()) {
                out.push_back(token);
                token.clear();
            }
        } else {
            token.push_back(c);
        }
    }
    if (!token.empty()) out.push_back(token);
    return out;
}

int main(int argc, char* argv[]) {
    std::string subjects_spec;
    std::string subjects_file;
    std::string output_path = "subject_trial.csv";
    std::string dic_params_file = "dic_params.txt";
    std::string ncorr_params_file = "ncorr_params.txt";
    std::string viz_params_file = "visualization_params.txt";

    static struct option long_options[] = {{"subjects", required_argument, 0, 'S'},
                                           {"subjects-file", required_argument, 0, 'f'},
                                           {"output", required_argument, 0, 'o'},
                                           {"dic-params", required_argument, 0, 'd'},
                                           {"ncorr-params", required_argument, 0, 'n'},
                                           {"viz-params", required_argument, 0, 'v'},
                                           {"help", no_argument, 0, 'h'},
                                           {0, 0, 0, 0}};

    int opt;
    int option_index = 0;
    while ((opt = getopt_long(argc, argv, "S:f:o:d:n:v:h", long_options, &option_index)) != -1) {
        switch (opt) {
            case 'S':
                subjects_spec = optarg;
                break;
            case 'f':
                subjects_file = optarg;
                break;
            case 'o':
                output_path = optarg;
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
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    try {
        // Gather subjects: --subjects, --subjects-file, then positional args.
        std::vector<std::string> subjects;
        if (!subjects_spec.empty()) {
            auto s = parse_subject_list(subjects_spec);
            subjects.insert(subjects.end(), s.begin(), s.end());
        }
        if (!subjects_file.empty()) {
            auto s = read_subjects_file(subjects_file);
            subjects.insert(subjects.end(), s.begin(), s.end());
        }
        for (int i = optind; i < argc; ++i) {
            subjects.push_back(argv[i]);
        }

        if (subjects.empty()) {
            LOG_ERROR << "no subjects provided.";
            print_usage(argv[0]);
            return 1;
        }

        // Load base configuration (paths, phase, nf/spd condition sets, etc.).
        Config config;
        config.loadFromDicParamsFile(dic_params_file);
        config.loadFromNcorrParamsFile(ncorr_params_file);
        config.loadFromVisualizationParamsFile(viz_params_file);
        config.updateVariables();

        DicAnalysis dic(config);

        std::ofstream out(output_path);
        if (!out) {
            LOG_ERROR << "cannot open output file: " << output_path;
            return 1;
        }
        out << "subject,trial\n";

        size_t total_rows = 0;
        for (const auto& subject : subjects) {
            std::vector<int> trials = dic.searchTrialTarget(subject);
            LOG_INFO << subject << ": " << trials.size() << " trial(s)";
            for (int trial : trials) {
                out << subject << "," << trial << "\n";
                ++total_rows;
            }
        }
        out.close();

        LOG_INFO << "Wrote " << total_rows << " (subject,trial) row(s) to " << output_path;
        LOG_INFO << "SLURM array size for subject_trial mode: --array=1-" << total_rows;
        return 0;
    } catch (const std::exception& e) {
        LOG_ERROR << e.what();
        return 1;
    }
}

/**
 * Trial / (subject, trial) selection helpers for CPPXDIC drivers.
 *
 * Small, header-only utilities shared by the gen_subject_trial generator and
 * the trial-level experiment runner (single trial, SLURM array over a trial
 * list, SLURM array over a subject_trial.csv). Kept self-contained so new
 * targets can include just this header without pulling in the full pipeline.
 */

#ifndef TRIAL_SELECTION_H
#define TRIAL_SELECTION_H

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace cppxdic {

/** A single (subject, trial) row, e.g. parsed from subject_trial.csv. */
struct SubjectTrial {
    std::string subject;
    int trial = 0;
};

/** Trim leading/trailing whitespace (spaces, tabs, CR, LF). */
inline std::string trim(const std::string& s) {
    const char* ws = " \t\r\n";
    size_t b = s.find_first_not_of(ws);
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

/**
 * Parse a comma/space-separated list of integer trial IDs.
 * Accepts e.g. "7,12,25" or "7 12 25" or "7, 12 , 25".
 */
inline std::vector<int> parseTrialList(const std::string& spec) {
    std::vector<int> trials;
    std::string token;
    for (char c : spec) {
        if (c == ',' || c == ' ' || c == '\t' || c == ';') {
            if (!token.empty()) {
                trials.push_back(std::stoi(token));
                token.clear();
            }
        } else {
            token.push_back(c);
        }
    }
    if (!token.empty()) trials.push_back(std::stoi(token));
    return trials;
}

/**
 * Read a trials file: one trial ID per line. Blank lines and lines whose first
 * non-whitespace character is '#' are ignored.
 */
inline std::vector<int> readTrialsFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("Cannot open trials file: " + path);
    }
    std::vector<int> trials;
    std::string line;
    while (std::getline(in, line)) {
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        trials.push_back(std::stoi(t));
    }
    return trials;
}

/**
 * Read subject_trial.csv (header "subject,trial") into rows, skipping the
 * header line and any blank/comment lines. Each data line must contain at
 * least two comma-separated fields: subject, trial.
 */
inline std::vector<SubjectTrial> readSubjectTrialCsv(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("Cannot open subject_trial CSV: " + path);
    }
    std::vector<SubjectTrial> rows;
    std::string line;
    bool header_skipped = false;
    while (std::getline(in, line)) {
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        // Skip the header line ("subject,trial") once.
        if (!header_skipped) {
            header_skipped = true;
            std::string lower;
            for (char c : t)
                lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            if (lower.rfind("subject", 0) == 0) continue;
        }
        std::stringstream ss(t);
        std::string subj, trial_str;
        if (!std::getline(ss, subj, ',')) continue;
        if (!std::getline(ss, trial_str, ',')) {
            throw std::runtime_error("Malformed line in " + path + ": " + line);
        }
        SubjectTrial row;
        row.subject = trim(subj);
        row.trial = std::stoi(trim(trial_str));
        rows.push_back(row);
    }
    return rows;
}

/**
 * Resolve SLURM_ARRAY_TASK_ID from the environment.
 * @return the task id, or -1 if the variable is unset/empty.
 */
inline int slurmArrayTaskId() {
    const char* env = std::getenv("SLURM_ARRAY_TASK_ID");
    if (!env || *env == '\0') return -1;
    return std::stoi(std::string(env));
}

} // namespace cppxdic

#endif // TRIAL_SELECTION_H

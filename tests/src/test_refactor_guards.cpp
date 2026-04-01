#include "config.h"
#include "cppxdic/io/project_paths.h"
#include "cppxdic/pipeline/trial_selector.h"
#include "temporal_filter.h"
#include "utils.h"

#include <matio.h>
#include <filesystem>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace cppxdic;

static int g_total = 0;
static int g_passed = 0;

#define TEST(name) \
    do { \
        ++g_total; \
        std::cout << "  " << name << "... "; \
    } while (0)

#define PASS() \
    do { \
        ++g_passed; \
        std::cout << "PASSED" << std::endl; \
    } while (0)

#define FAIL(msg) \
    do { \
        std::cout << "FAILED: " << msg << std::endl; \
    } while (0)

#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            FAIL(msg); \
            return; \
        } \
    } while (0)

namespace {

matvar_t* makeStringVar(const std::string& value) {
    const size_t dims[2] = {1, value.empty() ? 1 : value.size()};
    return Mat_VarCreate(nullptr, MAT_C_CHAR, MAT_T_UTF8, 2, dims,
                         const_cast<char*>(value.c_str()), 0);
}

matvar_t* makeScalarVar(double value) {
    const size_t dims[2] = {1, 1};
    return Mat_VarCreate(nullptr, MAT_C_DOUBLE, MAT_T_DOUBLE, 2, dims, &value, 0);
}

bool writeProtocolFixture(const std::filesystem::path& path) {
    if (auto parent = path.parent_path(); !parent.empty()) {
        std::filesystem::create_directories(parent);
    }

    mat_t* matfp = Mat_CreateVer(path.string().c_str(), nullptr, MAT_FT_MAT5);
    if (!matfp) {
        return false;
    }

    const std::vector<std::string> titles = {"dir", "nf", "spd", "rep"};
    const size_t title_dims[2] = {1, titles.size()};
    matvar_t* titles_var = Mat_VarCreate("titles", MAT_C_CELL, MAT_T_CELL, 2, title_dims, nullptr, 0);
    for (size_t index = 0; index < titles.size(); ++index) {
        Mat_VarSetCell(titles_var, index, makeStringVar(titles[index]));
    }

    struct TrialRow {
        std::string dir;
        double nf;
        double spd;
        double rep;
    };
    const std::vector<TrialRow> rows = {
        {"Ubnf", 5.0, 0.04, 1.0},
        {"Dbnf", 5.0, 0.04, 1.0},
        {"Rbnf", 5.0, 0.04, 2.0},
        {"Ubnf", 10.0, 0.08, 1.0},
    };

    const size_t table_dims[2] = {rows.size(), titles.size()};
    matvar_t* table_var = Mat_VarCreate("table", MAT_C_CELL, MAT_T_CELL, 2, table_dims, nullptr, 0);
    for (size_t row = 0; row < rows.size(); ++row) {
        Mat_VarSetCell(table_var, row + 0 * rows.size(), makeStringVar(rows[row].dir));
        Mat_VarSetCell(table_var, row + 1 * rows.size(), makeScalarVar(rows[row].nf));
        Mat_VarSetCell(table_var, row + 2 * rows.size(), makeScalarVar(rows[row].spd));
        Mat_VarSetCell(table_var, row + 3 * rows.size(), makeScalarVar(rows[row].rep));
    }

    const std::vector<const char*> fields = {"titles", "table"};
    const size_t struct_dims[2] = {1, 1};
    matvar_t* cond_var = Mat_VarCreateStruct("cond", 2, struct_dims, fields.data(), fields.size());
    Mat_VarSetStructFieldByName(cond_var, "titles", 0, titles_var);
    Mat_VarSetStructFieldByName(cond_var, "table", 0, table_var);

    const bool write_ok = (Mat_VarWrite(matfp, cond_var, MAT_COMPRESSION_NONE) == 0);
    Mat_VarFree(cond_var);
    Mat_Close(matfp);
    return write_ok;
}

Config makeConfig(const std::filesystem::path& root) {
    Config config;
    config.frictional_conditions = {"glass"};
    config.material_id = 1;
    config.subject_id = "S09";
    config.phase_id = "loading";
    config.data_path = (root / "data").string();
    config.dic_path = (root / "analysis").string();
    config.nfcond_set = {5};
    config.spddxlcond_set = {0.04};
    config.updateVariables();
    return config;
}

void test_trial_selector_reads_protocol() {
    TEST("trial selector reads protocol MAT");

    const std::filesystem::path root = std::filesystem::path("tests") / "tmp_refactor_guards_case";
    std::filesystem::remove_all(root);

    Config config = makeConfig(root);
    const auto protocol_dir = std::filesystem::path(config.data_path)
        / "rawdata" / config.subject_id / "speckles" / config.material / "protocol";
    CHECK(writeProtocolFixture(protocol_dir / "protocol_fixture.mat"), "Could not write protocol fixture");

    cppxdic::io::ProjectPaths paths(config);
    cppxdic::pipeline::TrialSelector selector(config, paths);
    const auto trials = selector.selectTargets();

    CHECK(trials.size() == 2, "Expected exactly 2 matching trials");
    CHECK(trials[0] == 1 && trials[1] == 3, "Expected loading trials [1, 3]");
    PASS();
}

void test_project_paths_match_legacy_helpers() {
    TEST("project paths preserve legacy layout");

    const Config config = makeConfig(std::filesystem::path("tests") / "tmp_refactor_guards_case");
    cppxdic::io::ProjectPaths paths(config);

    CHECK(paths.protocolDir().string() == Utils::buildProtocolDir(config, true, true, true, true),
          "Protocol dir diverged from legacy helper");
    CHECK(paths.phaseDir(7).string() == Utils::buildOutputUntilPhaseDir(config, 7),
          "Phase dir diverged from legacy helper");
    CHECK(paths.dic3DCombinedFile(7, ".mat").string() ==
              Utils::buildDic3DCombinedFilePath(Utils::buildOutputUntilPhaseDir(config, 7), config.num_pair, ".mat"),
          "DIC3Dcombined path diverged from legacy helper");
    PASS();
}

void test_invalid_pair_is_rejected() {
    TEST("invalid stereopair is rejected");

    int cam_first = 0;
    int cam_second = 0;
    bool threw = false;
    try {
        Utils::getCamerasForPair(3, cam_first, cam_second);
    } catch (const std::runtime_error&) {
        threw = true;
    }

    CHECK(threw, "Expected invalid pair 3 to throw");
    PASS();
}

void test_shared_temporal_filter_handles_gaps() {
    TEST("shared temporal filter handles gap interpolation");

    const double nan = std::numeric_limits<double>::quiet_NaN();
    const std::vector<std::vector<double>> point_major = {
        {1.0, nan, 2.0, 3.0, 4.0, 5.0}
    };
    const auto strict_filtered = cppxdic::filterTime(point_major, 5.0, 50.0);
    CHECK(strict_filtered.size() == 1, "Strict filter size mismatch");
    for (const double value : strict_filtered.front()) {
        CHECK(std::isnan(value), "Strict filter should leave gap-containing track as NaN");
    }

    const std::vector<std::vector<double>> frame_major = {
        {1.0}, {nan}, {2.0}, {3.0}, {4.0}, {5.0}
    };
    const auto interpolated = cppxdic::filterTimeFrameMajor(frame_major, 5.0, 50.0, true);
    CHECK(interpolated.size() == frame_major.size(), "Interpolated output frame count mismatch");
    for (const auto& frame : interpolated) {
        CHECK(frame.size() == 1, "Interpolated output point count mismatch");
        CHECK(!std::isnan(frame.front()), "Interpolated filter should fill and smooth missing values");
    }

    PASS();
}

} // namespace

int main() {
    std::cout << "CPPxDIC refactor guard tests\n";
    std::cout << "===========================\n";

    test_trial_selector_reads_protocol();
    test_project_paths_match_legacy_helpers();
    test_invalid_pair_is_rejected();
    test_shared_temporal_filter_handles_gaps();

    std::cout << "\nSummary: " << g_passed << "/" << g_total << " tests passed\n";
    return (g_passed == g_total) ? 0 : 1;
}

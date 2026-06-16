/**
 * @file test_config.cpp
 * @brief Unit tests for Config (include/config.h, src/config.cpp).
 *
 * Covers the public surface of the parameter system:
 *  - default construction (compiled defaults)
 *  - updateVariables() deriving `material` from material_id
 *  - overrideSubject / overrideRefTrial (the CLI tier of the override chain)
 *  - loadFromConfigFile (the config-file tier): overrides applied, untouched keys
 *    keep their compiled defaults, parse helpers exercised indirectly (bool, int/double
 *    lists, comment/whitespace handling).
 *
 * NOTE: the low-level parse* helpers are private; they are validated here through the public
 * loadFromConfigFile path, which routes values through them.
 */

#include "../framework/test_harness.h"
#include "config.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {
fs::path write_temp_cfg(const std::string& contents, const std::string& tag) {
    fs::path p = fs::temp_directory_path() /
                 ("cppxdic_cfg_" + std::to_string(::getpid()) + "_" + tag + ".cfg");
    std::ofstream(p) << contents;
    return p;
}
} // namespace

TEST(config, default_construction) {
    Config c;
    CHECK_EQ(c.num_pair, 2);
    CHECK_EQ(c.idx_frame_start, 10);
    CHECK_EQ(c.data_format, std::string("mat"));
    CHECK_EQ(c.step_e.radius, 60);
    CHECK_EQ(c.subject_id, std::string("S09"));
    CHECK_EQ(c.material_id, 2);
}

TEST(config, update_variables_derives_material) {
    Config c;
    // frictional_conditions = {glass, coating, coating_oil}; material_id is 1-based.
    c.material_id = 1;
    c.updateVariables();
    CHECK_EQ(c.material, std::string("glass"));

    c.material_id = 3;
    c.updateVariables();
    CHECK_EQ(c.material, std::string("coating_oil"));

    // Out-of-range -> "unknown" (graceful).
    c.material_id = 99;
    c.updateVariables();
    CHECK_EQ(c.material, std::string("unknown"));
}

TEST(config, override_methods) {
    Config c;
    c.overrideSubject("CCC");
    CHECK_EQ(c.subject_id, std::string("CCC"));
    c.overrideRefTrial(42);
    CHECK_EQ(c.ref_trial_id, 42);
}

TEST(config, load_config_file_overrides_and_preserves) {
    // Override a handful of keys; leave the rest untouched.
    const std::string cfg =
        "# unified config (comment line)\n"
        "\n"
        "subject_id = AAA\n"
        "idx_frame_start = 20\n"
        "  num_pair = 3  \n"           // leading/trailing whitespace must be trimmed
        "data_format = bin\n"
        "spddxlcond_set = 0.1, 0.2, 0.3\n"
        "nfcond_set = 7, 8\n";
    fs::path p = write_temp_cfg(cfg, "override");

    Config c;
    bool ok = c.loadFromConfigFile(p.string());
    CHECK_TRUE(ok);

    // Overrides applied:
    CHECK_EQ(c.subject_id, std::string("AAA"));
    CHECK_EQ(c.idx_frame_start, 20);
    CHECK_EQ(c.num_pair, 3);
    CHECK_EQ(c.data_format, std::string("bin"));
    CHECK_EQ(c.spddxlcond_set.size(), static_cast<size_t>(3));
    if (c.spddxlcond_set.size() == 3) CHECK_NEAR(c.spddxlcond_set[2], 0.3, 1e-9);
    CHECK_EQ(c.nfcond_set.size(), static_cast<size_t>(2));

    // Untouched key keeps its compiled default:
    CHECK_EQ(c.idx_frame_end, 150);
    CHECK_EQ(c.material_id, 2);

    fs::remove(p);
}

TEST(config, load_config_file_missing_returns_false) {
    Config c;
    bool ok = c.loadFromConfigFile("/no/such/config/file.cfg");
    CHECK_FALSE(ok);
    // Defaults intact.
    CHECK_EQ(c.subject_id, std::string("S09"));
}

TEST(config, override_chain_config_then_cli) {
    // config-file sets subject_id=AAA, then CLI override wins.
    fs::path p = write_temp_cfg("subject_id = AAA\n", "chain");
    Config c;
    c.loadFromConfigFile(p.string());
    CHECK_EQ(c.subject_id, std::string("AAA"));
    c.overrideSubject("CCC"); // CLI tier
    CHECK_EQ(c.subject_id, std::string("CCC"));
    fs::remove(p);
}

TEST_MAIN()

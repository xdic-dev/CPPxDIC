/**
 * CPPXDIC - C++ equivalent of Matlab xDIC
 * Main entry point for FINGERTIP 3D RECONSTRUCTION using DIC
 * 
 * This is the C++ port of the Matlab xDIC library using the ncorr C++ library
 */

#include <iostream>
#include "cppxdic/app/app_config.h"
#include "cppxdic/app/cli.h"
#include "cppxdic/pipeline/pipeline_runner.h"
#include "utils.h"

int main(int argc, char* argv[]) {
    cppxdic::pipeline::PipelineRunner::printBanner(std::cout);

    try {
        const cppxdic::app::CliOptions cli_options = cppxdic::app::parseCli(argc, argv);
        if (cli_options.show_help) {
            cppxdic::app::printUsage(std::cout, argv[0]);
            return 0;
        }

        const cppxdic::app::AppConfig app_config = cppxdic::app::AppConfig::load(cli_options);
        app_config.printSummary(std::cout);

        std::cout << "Checking the data and protocol..." << std::endl;
        if (!Utils::dicCheck(app_config.legacy())) {
            std::cerr << "Data and protocol check failed!" << std::endl;
            return 1;
        }

        cppxdic::pipeline::PipelineRunner runner(app_config.legacy());
        const bool success = runner.run();
        if (success) {
            std::cout << "Analysis completed successfully!" << std::endl;
        } else {
            std::cerr << "Analysis failed!" << std::endl;
            return 1;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "End of script" << std::endl;
    return 0;
}

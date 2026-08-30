/**
 * @file main.cpp
 * @brief Asset pack bundler command-line entry point.
 *
 * @details Invokes the engine-owned AssetBundler for the content pipeline in
 * ARCHITECTURE.md section 6.6. The interactive content authoring UI belongs to work item 15.
 */

#include <iostream>
#include <string>

#include "voxels/assets/asset_bundler.hpp"

int main(int argc, char** argv) {
    std::string inputDirectory;
    std::string outputPath;
    for (int index = 1; index < argc; ++index) {
        const std::string argument(argv[index]);
        if (argument.rfind("--input=", 0) == 0) inputDirectory = argument.substr(8);
        if (argument.rfind("--output=", 0) == 0) outputPath = argument.substr(9);
    }
    if (inputDirectory.empty() || outputPath.empty()) {
        std::cerr << "Usage: voxels_editor --bundle --input=<content_dir> --output=<pack.vpk>\n";
        return 2;
    }

    voxels::AssetBundleReport report;
    std::string error;
    if (!voxels::AssetBundler::Bundle(inputDirectory, outputPath, report, error)) {
        std::cerr << "Bundle failed: " << error << '\n';
        return 1;
    }
    std::cout << "Bundle wrote " << report.archive.entryCount << " entries (" << report.archive.sourceBytes
              << " source bytes, " << report.archive.packedBytes << " packed bytes, content hash "
              << report.archive.contentHash << ").\n";
    for (const std::string& warning : report.warnings) std::cout << "Warning: " << warning << '\n';
    return 0;
}

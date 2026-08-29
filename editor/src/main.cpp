/*
 * Scope: Editor application bootstrap and entry point.
 *
 * This file is the starting point for the asset and model editing workflow, including preview,
 * texture application, and packaging operations. It intentionally avoids deep editor logic at
 * this stage and focuses on establishing the runtime boundary for the tool.
 *
 * Relation to the rest of the codebase: the editor consumes the engine as a library and is
 * designed to operate independently from the game application runtime.
 */

#include <iostream>

int main() {
    std::cout << "Voxels editor scaffold initialized." << std::endl;
    return 0;
}

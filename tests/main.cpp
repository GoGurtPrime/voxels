/**
 * @file main.cpp
 * @brief Automated unit test runner entry point for VoxelsEngine test suite.
 * 
 * @details Initializes Catch2 test framework session and executes all registered
 *          unit and integration test suites.
 */

#include <catch2/catch_session.hpp>

int main(int argc, char* argv[]) {
    return Catch::Session().run(argc, argv);
}

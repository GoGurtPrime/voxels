cmake_minimum_required(VERSION 3.16)

add_executable(voxels_tests
    main.cpp
    test_sanity.cpp
    test_core.cpp
    test_platform.cpp
    test_input.cpp
    test_graphics.cpp
    test_world.cpp
    test_world_gen.cpp
    test_audio_assets.cpp
    test_app_lifecycle.cpp
    test_networking.cpp
    test_gameplay.cpp
    test_runtime_integration.cpp
)

target_link_libraries(voxels_tests
    PRIVATE
        voxels_engine
        Catch2::Catch2
)

target_compile_features(voxels_tests PRIVATE cxx_std_20)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(voxels_tests PRIVATE -Wall -Wextra -Wpedantic)
elseif(MSVC)
    target_compile_options(voxels_tests PRIVATE /W4)
endif()

include(CTest)
include(Catch OPTIONAL)
if(COMMAND catch_discover_tests)
    catch_discover_tests(voxels_tests)
else()
    add_test(NAME voxels_tests COMMAND voxels_tests)
endif()

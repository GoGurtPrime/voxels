cmake_minimum_required(VERSION 3.16)

project(VoxelsEngine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(VOXELS_BUILD_APP "Build the game application target" ON)
option(VOXELS_BUILD_EDITOR "Build the editor target" ON)
option(VOXELS_BUILD_TESTS "Build automated unit test suites" ON)
option(VOXELS_ENABLE_VULKAN "Enable the Vulkan renderer backend" ON)
option(VOXELS_ENABLE_DX12 "Enable the DirectX 12 renderer backend" OFF)
option(VOXELS_ENABLE_METAL "Enable the Metal renderer backend" OFF)
option(VOXELS_ENABLE_DREAMCAST "Enable the Dreamcast renderer backend" OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
elseif(MSVC)
    add_compile_options(/W4)
endif()

# Dependency discovery is intentionally lightweight during the architectural phase.
# When the project is ready for production builds, these can be resolved through either
# a package manager or FetchContent for SDL2, GLM, ImGui, and Asio.
find_package(glm QUIET)
find_package(SDL2 QUIET)
find_package(asio QUIET)

if(NOT TARGET glm::glm)
    include(FetchContent)
    FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG        1.0.1
    )
    FetchContent_MakeAvailable(glm)
endif()
if(NOT TARGET SDL2::SDL2)
    message(STATUS "SDL2 not found; the platform layer remains abstract and can be configured per target.")
endif()
if(NOT TARGET asio::asio)
    include(FetchContent)
    FetchContent_Declare(
        asio
        GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
        GIT_TAG        asio-1-30-2
    )
    FetchContent_MakeAvailable(asio)
    if(NOT TARGET asio::asio)
        add_library(asio INTERFACE)
        add_library(asio::asio ALIAS asio)
        target_include_directories(asio INTERFACE "${asio_SOURCE_DIR}/asio/include")
        target_compile_definitions(asio INTERFACE ASIO_STANDALONE)
        if(WIN32)
            target_compile_definitions(asio INTERFACE _WIN32_WINNT=0x0A00)
        endif()
    endif()
endif()

add_subdirectory(engine)

if(VOXELS_BUILD_APP)
    add_subdirectory(app)
endif()

if(VOXELS_BUILD_EDITOR)
    add_subdirectory(editor)
endif()

if(VOXELS_BUILD_TESTS)
    include(CTest)
    enable_testing()

    find_package(Catch2 3 QUIET)
    if(NOT Catch2_FOUND)
        include(FetchContent)
        FetchContent_Declare(
            Catch2
            GIT_REPOSITORY https://github.com/catchorg/Catch2.git
            GIT_TAG        v3.5.2
        )
        FetchContent_MakeAvailable(Catch2)
    endif()

    add_subdirectory(tests)
endif()

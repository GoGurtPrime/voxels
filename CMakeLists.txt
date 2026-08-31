cmake_minimum_required(VERSION 3.21)

project(VoxelsEngine VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(VOXELS_BUILD_APP "Build the game application target" ON)
option(VOXELS_BUILD_EDITOR "Build the editor target" ON)
option(VOXELS_BUILD_TESTS "Build automated unit test suites" ON)
option(VOXELS_ENABLE_VULKAN "Enable the Vulkan renderer backend" OFF)
option(VOXELS_ENABLE_DX12 "Enable the DirectX 12 renderer backend" OFF)
option(VOXELS_ENABLE_METAL "Enable the Metal renderer backend" OFF)
option(VOXELS_ENABLE_DREAMCAST "Enable the Dreamcast renderer backend" OFF)
option(VOXELS_ENABLE_STEAM "Enable optional Steamworks platform services (achievements, overlay, cloud saves)" OFF)
option(VOXELS_WARNINGS_AS_ERRORS "Treat warnings as errors for first-party targets" ON)

add_compile_definitions(VOXELS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_compile_definitions(VOXELS_BINARY_DIR="${CMAKE_BINARY_DIR}")

set(VOXELS_WARNING_FLAGS "")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(VOXELS_WARNING_FLAGS -Wall -Wextra -Wpedantic)
elseif(MSVC)
    set(VOXELS_WARNING_FLAGS /W4)
endif()
if(VOXELS_WARNINGS_AS_ERRORS)
    if(MSVC)
        list(APPEND VOXELS_WARNING_FLAGS /WX)
    else()
        list(APPEND VOXELS_WARNING_FLAGS -Werror)
    endif()
endif()

include(FetchContent)
find_package(Python REQUIRED COMPONENTS Interpreter)

find_package(glm QUIET)
if(NOT TARGET glm::glm)
    FetchContent_Declare(
        glm
        GIT_REPOSITORY https://github.com/g-truc/glm.git
        GIT_TAG        1.0.1
    )
    FetchContent_MakeAvailable(glm)
endif()

find_package(SDL2 CONFIG QUIET)
if(NOT TARGET SDL2::SDL2)
    FetchContent_Declare(
        SDL2
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG        release-2.30.7
    )
    set(SDL_SHARED ON CACHE BOOL "Build SDL2 shared library" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "Disable SDL2 static library" FORCE)
    set(SDL_TEST OFF CACHE BOOL "Disable SDL2 test targets" FORCE)
    FetchContent_MakeAvailable(SDL2)
endif()
if(NOT TARGET SDL2::SDL2)
    message(FATAL_ERROR "SDL2 is a required desktop dependency for VoxelsEngine. The build cannot continue without a real windowing/backend stack.")
endif()

FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        docking
)
FetchContent_MakeAvailable(imgui)

if(NOT TARGET voxels_imgui)
    add_library(voxels_imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
        ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    )
    target_include_directories(voxels_imgui SYSTEM PUBLIC
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
    )
    target_link_libraries(voxels_imgui PUBLIC SDL2::SDL2)
endif()

find_package(glad CONFIG QUIET)
if(NOT TARGET glad_gl_core_33)
    execute_process(
        COMMAND "${Python_EXECUTABLE}" -m pip install --user glad
        RESULT_VARIABLE GLAD_PIP_RESULT
        OUTPUT_VARIABLE GLAD_PIP_OUTPUT
        ERROR_VARIABLE GLAD_PIP_ERROR
    )
    if(NOT GLAD_PIP_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to install the Python 'glad' package required to generate the OpenGL loader. Output: ${GLAD_PIP_OUTPUT}\nError: ${GLAD_PIP_ERROR}")
    endif()

    set(GLAD_GENERATED_DIR "${CMAKE_BINARY_DIR}/glad_gl_core_33")
    file(MAKE_DIRECTORY "${GLAD_GENERATED_DIR}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "PYTHONUTF8=1" "${Python_EXECUTABLE}" -m glad --generator c --profile core --out-path "${GLAD_GENERATED_DIR}" --api "gl=3.3" --reproducible --no-loader
        RESULT_VARIABLE GLAD_GENERATE_RESULT
        OUTPUT_VARIABLE GLAD_GENERATE_OUTPUT
        ERROR_VARIABLE GLAD_GENERATE_ERROR
    )
    if(NOT GLAD_GENERATE_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to generate the OpenGL 3.3 glad loader. Output: ${GLAD_GENERATE_OUTPUT}\nError: ${GLAD_GENERATE_ERROR}")
    endif()

    add_library(glad_gl_core_33 STATIC
        "${GLAD_GENERATED_DIR}/src/glad.c"
    )
    target_include_directories(glad_gl_core_33 PUBLIC
        "${GLAD_GENERATED_DIR}/include"
        "${GLAD_GENERATED_DIR}/include/KHR"
    )
endif()
if(NOT TARGET glad_gl_core_33)
    message(FATAL_ERROR "glad is a required OpenGL loader dependency for the desktop build.")
endif()
add_library(voxels_glad INTERFACE)
target_link_libraries(voxels_glad INTERFACE glad_gl_core_33)

find_package(asio QUIET)
if(NOT TARGET asio::asio)
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

# Packaging content (work_items/18): generated once per configure so the version stamp and the
# third-party dependency list can never silently drift from what CMake actually vendors/links.
set(VOXELS_PACKAGE_DIR "${CMAKE_BINARY_DIR}/package")
file(MAKE_DIRECTORY "${VOXELS_PACKAGE_DIR}")
set(VOXELS_VERSION "${PROJECT_VERSION}")
string(TIMESTAMP VOXELS_LICENSE_TIMESTAMP "%Y-%m-%dT%H:%M:%SZ" UTC)
if(VOXELS_STEAM_ACTIVE)
    set(VOXELS_STEAM_LICENSE_NOTE "
================================================================================
Steamworks SDK — proprietary, Valve Corporation (optional; VOXELS_ENABLE_STEAM=ON in this build)
https://partner.steamgames.com/
--------------------------------------------------------------------------------
The Steamworks SDK is licensed to registered Steamworks partners under the Steamworks SDK
Access Agreement and is not open source. It is not redistributed by this source repository;
the operator supplies it locally (see ASSET_REQUESTS.md). Its runtime library is included in
this specific package build because VOXELS_ENABLE_STEAM was enabled.")
else()
    set(VOXELS_STEAM_LICENSE_NOTE "")
endif()
configure_file("${CMAKE_SOURCE_DIR}/packaging/README.txt.in" "${VOXELS_PACKAGE_DIR}/README.txt" @ONLY)
configure_file("${CMAKE_SOURCE_DIR}/packaging/THIRD_PARTY_LICENSES.txt.in"
               "${VOXELS_PACKAGE_DIR}/THIRD_PARTY_LICENSES.txt" @ONLY)

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
        FetchContent_Declare(
            Catch2
            GIT_REPOSITORY https://github.com/catchorg/Catch2.git
            GIT_TAG        v3.5.2
        )
        FetchContent_MakeAvailable(Catch2)
    endif()

    add_subdirectory(tests)
endif()

# --- Packaging (work_items/18_packaging_distribution_and_platform_services.md) ---------------
# Produces a self-contained, redistributable archive: `cmake --install build --prefix <dir>` or
# `cpack --config build/CPackConfig.cmake` after building. The editor is never installed here —
# it ships separately as an internal tool, per the work item's explicit scope.
if(VOXELS_BUILD_APP)
    set(CPACK_PACKAGE_NAME "VoxelsGame")
    set(CPACK_PACKAGE_VENDOR "VoxelsEngine Project")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
    set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
    set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "VoxelsGame")
    set(CPACK_PACKAGE_FILE_NAME "VoxelsGame-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-x64")
    set(CPACK_PACKAGE_CHECKSUM "SHA256")
    if(WIN32)
        set(CPACK_GENERATOR "ZIP")
    elseif(APPLE)
        set(CPACK_GENERATOR "TGZ") # A proper .app bundle + DMG is future work (see completion report).
    else()
        set(CPACK_GENERATOR "TGZ")
    endif()
    include(CPack)
endif()

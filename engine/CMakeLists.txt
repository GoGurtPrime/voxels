cmake_minimum_required(VERSION 3.21)

# Generated via an OUTPUT-based custom command so incremental builds only rerun this step when
# inputs change (template/script/git ref), avoiding full-engine rebuild churn.
set(VOXELS_GENERATED_INCLUDE_DIR "${CMAKE_BINARY_DIR}/generated_include")
set(VOXELS_VERSION_HEADER "${VOXELS_GENERATED_INCLUDE_DIR}/voxels/core/version.hpp")

set(VOXELS_VERSION_HEADER_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/voxels/core/version.hpp.in"
    "${CMAKE_SOURCE_DIR}/cmake/GenerateVersionHeader.cmake"
)
if(EXISTS "${CMAKE_SOURCE_DIR}/.git/HEAD")
    list(APPEND VOXELS_VERSION_HEADER_DEPENDS "${CMAKE_SOURCE_DIR}/.git/HEAD")
    file(READ "${CMAKE_SOURCE_DIR}/.git/HEAD" VOXELS_GIT_HEAD_CONTENT)
    string(STRIP "${VOXELS_GIT_HEAD_CONTENT}" VOXELS_GIT_HEAD_CONTENT)
    if(VOXELS_GIT_HEAD_CONTENT MATCHES "^ref: (.+)$")
        set(VOXELS_GIT_REF_FILE "${CMAKE_SOURCE_DIR}/.git/${CMAKE_MATCH_1}")
        if(EXISTS "${VOXELS_GIT_REF_FILE}")
            list(APPEND VOXELS_VERSION_HEADER_DEPENDS "${VOXELS_GIT_REF_FILE}")
        endif()
    endif()
endif()

add_custom_command(
    OUTPUT "${VOXELS_VERSION_HEADER}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${VOXELS_GENERATED_INCLUDE_DIR}/voxels/core"
    COMMAND "${CMAKE_COMMAND}"
        "-DVOXELS_VERSION=${PROJECT_VERSION}"
        "-DVOXELS_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}"
        "-DVOXELS_VERSION_MINOR=${PROJECT_VERSION_MINOR}"
        "-DVOXELS_VERSION_PATCH=${PROJECT_VERSION_PATCH}"
        "-DSOURCE_DIR=${CMAKE_SOURCE_DIR}"
        "-DTEMPLATE_FILE=${CMAKE_CURRENT_SOURCE_DIR}/include/voxels/core/version.hpp.in"
        "-DOUTPUT_FILE=${VOXELS_VERSION_HEADER}"
        -P "${CMAKE_SOURCE_DIR}/cmake/GenerateVersionHeader.cmake"
    DEPENDS ${VOXELS_VERSION_HEADER_DEPENDS}
    COMMENT "Generating voxels/core/version.hpp"
    VERBATIM
)

add_custom_target(voxels_generate_version_header DEPENDS "${VOXELS_VERSION_HEADER}")

add_library(voxels_engine
    src/engine.cpp
    src/core/job_system.cpp
    src/platform/headless_platform.cpp
    src/platform/platform_factory.cpp
    src/platform/platform_services.cpp
    src/rendering.cpp
    src/render/camera.cpp
    src/render/celestial_lighting.cpp
    src/render/sky_renderer.cpp
    src/render/underwater_overlay.cpp
    src/render/texture_forge.cpp
    src/render/texture_atlas.cpp
    src/render/chunk_mesher.cpp
    src/render/chunk_renderer.cpp
    src/render/model_registry.cpp
    src/render/item_drop_renderer.cpp
    src/render/gameplay_hud.cpp
    src/render/remote_player_renderer.cpp
    src/graphics/gl_renderer.cpp
    src/graphics/mock_renderer.cpp
    src/app/state_machine_render.cpp
    src/app/game_session.cpp
    src/app/network_sync.cpp
    src/input.cpp
    src/world/block.cpp
    src/world/chunk.cpp
    src/world/geometry.cpp
    src/world/generation_pipeline.cpp
    src/world/noise.cpp
    src/world/spawn_calculator.cpp
    src/world/world.cpp
    src/world/world_serialization.cpp
    src/core/logger.cpp
    src/core/preferences.cpp
    src/core/math.cpp
    src/core/paths.cpp
    src/audio/audio_engine.cpp
    src/assets/asset_manager.cpp
    src/assets/asset_bundler.cpp
    src/assets/texture_loader.cpp
    src/assets/vmdl_codec.cpp
    src/networking/packet.cpp
    src/networking/reliable_channel.cpp
    src/networking/server.cpp
    src/networking/client.cpp
    src/gameplay/physics.cpp
    src/gameplay/camera_controller.cpp
    src/gameplay/block_interaction.cpp
    src/gameplay/item_drop.cpp
    src/app/cli_parser.cpp
    src/app/display_settings.cpp
    src/app/state_machine.cpp
    src/app/player_ui_dispatcher.cpp
    src/app/save_manager.cpp
    src/app/menus.cpp
    src/app/state_machine_screens.cpp
    src/ui/ui_manager.cpp
    src/ui/player_ui.cpp
    src/ui/imgui_ui_manager.cpp
    src/ui/web_ui_manifest.cpp
    src/ui/web_ui_compositor.cpp
)

if(VOXELS_ENABLE_WEB_UI)
    target_sources(voxels_engine PRIVATE src/ui/web_ui_manager.cpp)
    target_link_libraries(voxels_engine PUBLIC voxels_cef)
endif()

if(WIN32)
    target_sources(voxels_engine PRIVATE src/graphics/dx11_renderer.cpp)
    target_link_libraries(voxels_engine PUBLIC d3d11 dxgi d3dcompiler)
    target_compile_definitions(voxels_engine PUBLIC VOXELS_HAS_DX11=1)
endif()

target_include_directories(voxels_engine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${VOXELS_GENERATED_INCLUDE_DIR}
        ${CMAKE_BINARY_DIR}/glad_gl_core_33/include
    SYSTEM PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

add_dependencies(voxels_engine voxels_generate_version_header)

target_compile_features(voxels_engine PUBLIC cxx_std_20)

target_compile_options(voxels_engine PRIVATE ${VOXELS_WARNING_FLAGS})

if(TARGET SDL2::SDL2)
    target_link_libraries(voxels_engine PUBLIC SDL2::SDL2)
    target_sources(voxels_engine PRIVATE src/platform/sdl_platform.cpp)
    target_compile_definitions(voxels_engine PUBLIC VOXELS_HAS_SDL2=1)
endif()

if(TARGET glm::glm)
    target_link_libraries(voxels_engine PUBLIC glm::glm)
endif()

if(TARGET voxels_imgui)
    target_link_libraries(voxels_engine PUBLIC voxels_imgui)
endif()

if(TARGET voxels_glad)
    target_link_libraries(voxels_engine PUBLIC voxels_glad)
endif()

if(TARGET asio::asio)
    target_link_libraries(voxels_engine PUBLIC asio::asio)
endif()

find_package(Threads QUIET)
if(TARGET Threads::Threads)
    target_link_libraries(voxels_engine PUBLIC Threads::Threads)
endif()

if(VOXELS_ENABLE_VULKAN)
    target_compile_definitions(voxels_engine PUBLIC VOXELS_ENABLE_VULKAN=1)
endif()

if(VOXELS_ENABLE_DX12)
    target_compile_definitions(voxels_engine PUBLIC VOXELS_ENABLE_DX12=1)
endif()

if(VOXELS_ENABLE_METAL)
    target_compile_definitions(voxels_engine PUBLIC VOXELS_ENABLE_METAL=1)
endif()

if(VOXELS_ENABLE_DREAMCAST)
    target_compile_definitions(voxels_engine PUBLIC VOXELS_ENABLE_DREAMCAST=1)
    target_sources(voxels_engine PRIVATE src/platform/dreamcast_platform.cpp)
endif()

if(VOXELS_ENABLE_STEAM)
    if(NOT DEFINED STEAM_SDK_ROOT OR STEAM_SDK_ROOT STREQUAL "")
        set(STEAM_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/steam")
    endif()
    set(VOXELS_STEAM_HEADER "${STEAM_SDK_ROOT}/include/steam/steam_api.h")
    if(WIN32)
        set(VOXELS_STEAM_LIB "${STEAM_SDK_ROOT}/lib/win64/steam_api64.lib")
        set(VOXELS_STEAM_RUNTIME "${STEAM_SDK_ROOT}/lib/win64/steam_api64.dll")
    elseif(APPLE)
        set(VOXELS_STEAM_LIB "${STEAM_SDK_ROOT}/lib/osx/libsteam_api.dylib")
        set(VOXELS_STEAM_RUNTIME "${VOXELS_STEAM_LIB}")
    else()
        set(VOXELS_STEAM_LIB "${STEAM_SDK_ROOT}/lib/linux64/libsteam_api.so")
        set(VOXELS_STEAM_RUNTIME "${VOXELS_STEAM_LIB}")
    endif()

    if(EXISTS "${VOXELS_STEAM_HEADER}" AND EXISTS "${VOXELS_STEAM_LIB}")
        target_sources(voxels_engine PRIVATE src/platform/steam_platform_services.cpp)
        target_include_directories(voxels_engine SYSTEM PUBLIC "${STEAM_SDK_ROOT}/include")
        target_link_libraries(voxels_engine PUBLIC "${VOXELS_STEAM_LIB}")
        target_compile_definitions(voxels_engine PUBLIC VOXELS_ENABLE_STEAM=1)
        set(VOXELS_STEAM_ACTIVE TRUE PARENT_SCOPE)
        set(VOXELS_STEAM_RUNTIME "${VOXELS_STEAM_RUNTIME}" PARENT_SCOPE)
        message(STATUS "Steam platform services enabled (SDK at ${STEAM_SDK_ROOT}).")
    else()
        # Optional feature: an absent SDK cleanly disables it rather than failing the build
        # (work_items/18_packaging_distribution_and_platform_services.md §4.7).
        message(WARNING "VOXELS_ENABLE_STEAM was ON but the Steamworks SDK was not found at "
                        "'${STEAM_SDK_ROOT}' (expected 'include/steam/steam_api.h' and the "
                        "platform import library). Disabling Steam integration for this build; "
                        "set -DSTEAM_SDK_ROOT=<path> to point at a valid SDK.")
    endif()
endif()


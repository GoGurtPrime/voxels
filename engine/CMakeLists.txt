cmake_minimum_required(VERSION 3.16)

add_library(voxels_engine
    src/engine.cpp
    src/platform/headless_platform.cpp
    src/platform/platform_factory.cpp
    src/rendering.cpp
    src/graphics/mock_renderer.cpp
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
    src/audio/audio_engine.cpp
    src/assets/asset_manager.cpp
    src/networking/packet.cpp
    src/networking/server.cpp
    src/networking/client.cpp
    src/gameplay/physics.cpp
    src/gameplay/camera_controller.cpp
    src/gameplay/block_interaction.cpp
    src/app/cli_parser.cpp
    src/app/state_machine.cpp
    src/app/save_manager.cpp
    src/app/menus.cpp
    src/ui/ui_manager.cpp
)

target_include_directories(voxels_engine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(voxels_engine PUBLIC cxx_std_20)

if(TARGET SDL2::SDL2)
    target_link_libraries(voxels_engine PUBLIC SDL2::SDL2)
    target_sources(voxels_engine PRIVATE src/platform/sdl_platform.cpp)
    target_compile_definitions(voxels_engine PUBLIC VOXELS_HAS_SDL2=1)
endif()

if(TARGET glm::glm)
    target_link_libraries(voxels_engine PUBLIC glm::glm)
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


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
    test_asset_pipeline.cpp
    test_audio_runtime.cpp
    test_app_lifecycle.cpp
    test_networking.cpp
    test_gameplay.cpp
    test_block_interaction.cpp
    test_item_drop.cpp
    test_runtime_integration.cpp
    test_player_and_streaming.cpp
    test_mvp_playthrough.cpp
    test_build_environment.cpp
    test_render_backend.cpp
    test_chunk_meshing.cpp
    test_voxel_models.cpp
    test_editor.cpp
    test_block_catalogue.cpp
    test_ui_framework.cpp
    test_persistence.cpp
    test_packaging.cpp
    test_web_ui_manifest.cpp
    test_web_ui_runtime_proof.cpp
    test_web_ui_compositor.cpp
    test_web_ui_app_composition.cpp
)

target_link_libraries(voxels_tests
    PRIVATE
        voxels_engine
    voxels_editor_core
        Catch2::Catch2
)

target_compile_features(voxels_tests PRIVATE cxx_std_20)

add_custom_command(TARGET voxels_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/app/assets"
        "$<TARGET_FILE_DIR:voxels_tests>/assets"
    COMMENT "Staging app assets next to the test executable"
)

if(WIN32 AND TARGET SDL2::SDL2)
    add_custom_command(TARGET voxels_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:SDL2::SDL2>"
            "$<TARGET_FILE_DIR:voxels_tests>"
        COMMENT "Staging SDL2 runtime next to the test executable"
    )
endif()

if(VOXELS_ENABLE_WEB_UI)
    # voxels_tests links the CEF wrapper (test_web_ui_app_composition.cpp constructs a
    # WebUIManager), so libcef.dll must be resolvable at process load time, not just at link
    # time, or even test *discovery* fails with STATUS_DLL_NOT_FOUND before any test runs.
    file(GLOB VOXELS_CEF_RELEASE_ITEMS "${VOXELS_CEF_ROOT}/Release/*")
    list(FILTER VOXELS_CEF_RELEASE_ITEMS EXCLUDE REGEX "\\.lib$")
    list(FILTER VOXELS_CEF_RELEASE_ITEMS EXCLUDE REGEX "[/\\\\]bootstrap[^/\\\\]*\\.exe$")
    set(VOXELS_CEF_TEST_STAGE_COMMANDS)
    foreach(voxels_cef_item IN LISTS VOXELS_CEF_RELEASE_ITEMS)
        list(APPEND VOXELS_CEF_TEST_STAGE_COMMANDS COMMAND ${CMAKE_COMMAND} -E copy_if_different "${voxels_cef_item}" "$<TARGET_FILE_DIR:voxels_tests>")
    endforeach()
    add_custom_command(TARGET voxels_tests POST_BUILD
        ${VOXELS_CEF_TEST_STAGE_COMMANDS}
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${VOXELS_CEF_ROOT}/Resources" "$<TARGET_FILE_DIR:voxels_tests>"
        COMMENT "Staging verified CEF runtime next to the test executable"
    )
endif()

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

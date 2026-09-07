cmake_minimum_required(VERSION 3.21)

add_executable(voxels_app
    src/main.cpp
)

target_link_libraries(voxels_app PRIVATE voxels_engine)

target_compile_features(voxels_app PRIVATE cxx_std_20)
target_compile_options(voxels_app PRIVATE ${VOXELS_WARNING_FLAGS})

if(VOXELS_ENABLE_WEB_UI)
    add_dependencies(voxels_app voxels_web_ui_assets)
endif()

add_custom_command(TARGET voxels_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/app/assets"
        "$<TARGET_FILE_DIR:voxels_app>/assets"
    COMMENT "Staging app assets next to the executable"
)

if(TARGET SDL2::SDL2)
    add_custom_command(TARGET voxels_app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:SDL2::SDL2>"
            "$<TARGET_FILE_DIR:voxels_app>"
        COMMENT "Staging SDL2 runtime next to the executable"
    )
endif()

if(VOXELS_STEAM_ACTIVE)
    add_custom_command(TARGET voxels_app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VOXELS_STEAM_RUNTIME}"
            "$<TARGET_FILE_DIR:voxels_app>"
        COMMENT "Staging Steam runtime next to the executable"
    )
endif()

if(VOXELS_ENABLE_WEB_UI)
    # Stage every Release/ file except import libraries and sandbox-only bootstrap launchers.
    file(GLOB VOXELS_CEF_RELEASE_ITEMS "${VOXELS_CEF_ROOT}/Release/*")
    list(FILTER VOXELS_CEF_RELEASE_ITEMS EXCLUDE REGEX "\\.lib$")
    list(FILTER VOXELS_CEF_RELEASE_ITEMS EXCLUDE REGEX "[/\\\\]bootstrap[^/\\\\]*\\.exe$")
    set(VOXELS_CEF_STAGE_COMMANDS)
    foreach(voxels_cef_item IN LISTS VOXELS_CEF_RELEASE_ITEMS)
        list(APPEND VOXELS_CEF_STAGE_COMMANDS COMMAND ${CMAKE_COMMAND} -E copy_if_different "${voxels_cef_item}" "$<TARGET_FILE_DIR:voxels_app>")
    endforeach()
    add_custom_command(TARGET voxels_app POST_BUILD
        ${VOXELS_CEF_STAGE_COMMANDS}
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${VOXELS_CEF_ROOT}/Resources" "$<TARGET_FILE_DIR:voxels_app>"
        COMMENT "Staging verified CEF runtime next to the executable (import libs/sandbox loaders excluded)"
    )
endif()

# --- Install / package layout (work_items/18) -------------------------------------------------
# Distributable layout:
#   VoxelsGame/
#     voxels_app(.exe), SDL2/Steam runtime libraries
#     assets/            (textures, audio, fonts, models, shaders, data, packs/core.vpk)
#     README.txt, LICENSE.txt, THIRD_PARTY_LICENSES.txt
# The editor is deliberately never installed here; it ships separately as an internal tool.
install(TARGETS voxels_app RUNTIME DESTINATION .)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/app/assets/" DESTINATION assets)
install(FILES "${VOXELS_PACKAGE_DIR}/README.txt"
              "${CMAKE_SOURCE_DIR}/LICENSE.txt"
              "${VOXELS_PACKAGE_DIR}/THIRD_PARTY_LICENSES.txt"
        DESTINATION .)

if(TARGET SDL2::SDL2)
    install(FILES "$<TARGET_FILE:SDL2::SDL2>" DESTINATION .)
endif()

if(VOXELS_STEAM_ACTIVE)
    install(FILES "${VOXELS_STEAM_RUNTIME}" DESTINATION .)
endif()

if(VOXELS_ENABLE_WEB_UI)
    install(DIRECTORY "${VOXELS_CEF_ROOT}/Release/" DESTINATION .
        PATTERN "*.lib" EXCLUDE
        PATTERN "bootstrap*.exe" EXCLUDE)
    install(DIRECTORY "${VOXELS_CEF_ROOT}/Resources/" DESTINATION .)
    install(FILES "${VOXELS_CEF_ROOT}/LICENSE.txt" DESTINATION . OPTIONAL)
endif()

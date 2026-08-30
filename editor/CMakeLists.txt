cmake_minimum_required(VERSION 3.16)

add_executable(voxels_editor
    src/main.cpp
)

target_link_libraries(voxels_editor PRIVATE voxels_engine)

target_compile_features(voxels_editor PRIVATE cxx_std_20)
target_compile_options(voxels_editor PRIVATE ${VOXELS_WARNING_FLAGS})

add_custom_target(bundle_assets
    COMMAND $<TARGET_FILE:voxels_editor> --bundle
        --input=${CMAKE_SOURCE_DIR}/app/assets
        --output=${CMAKE_SOURCE_DIR}/app/assets/packs/core.vpk
    DEPENDS voxels_editor
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Bundling validated app assets into core.vpk"
)

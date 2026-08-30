cmake_minimum_required(VERSION 3.21)

add_executable(voxels_app
    src/main.cpp
)

set_target_properties(voxels_app PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

target_link_libraries(voxels_app PRIVATE voxels_engine)

target_compile_features(voxels_app PRIVATE cxx_std_20)
target_compile_options(voxels_app PRIVATE ${VOXELS_WARNING_FLAGS})

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

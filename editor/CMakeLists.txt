cmake_minimum_required(VERSION 3.16)

add_executable(voxels_editor
    src/main.cpp
)

target_link_libraries(voxels_editor PRIVATE voxels_engine)

target_compile_features(voxels_editor PRIVATE cxx_std_20)

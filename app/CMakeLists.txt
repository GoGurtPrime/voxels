cmake_minimum_required(VERSION 3.16)

add_executable(voxels_app
    src/main.cpp
)

target_link_libraries(voxels_app PRIVATE voxels_engine)

target_compile_features(voxels_app PRIVATE cxx_std_20)

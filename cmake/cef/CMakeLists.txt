# Builds Chromium Embedded Framework's official libcef_dll_wrapper using CEF's
# bundled CMake (curated source list + platform definitions). Kept in its own
# directory so CEF's compiler-flag and language-standard changes stay local and
# never leak into the engine's /W4 C++20 build.

# VOXELS_CEF_ROOT is provided by the parent scope (the extracted distribution).
set(CEF_ROOT "${VOXELS_CEF_ROOT}")

# Sandbox off: cef_sandbox.lib is a static /MT-only library and cannot link into
# the engine's dynamic /MD runtime. Enabling it would require moving the entire
# app (and SDL2) to a static runtime. Tracked as a WI-03 follow-up; see ADR-015.
set(USE_SANDBOX OFF CACHE BOOL "" FORCE)
set(CEF_RUNTIME_LIBRARY_FLAG "/MD" CACHE STRING "" FORCE)

list(APPEND CMAKE_MODULE_PATH "${VOXELS_CEF_ROOT}/cmake")
find_package(CEF REQUIRED)
add_subdirectory("${CEF_LIBCEF_DLL_WRAPPER_PATH}" libcef_dll_wrapper)

# Consumers link this single interface target.
add_library(voxels_cef INTERFACE)
target_include_directories(voxels_cef SYSTEM INTERFACE "${VOXELS_CEF_ROOT}")
target_link_libraries(voxels_cef INTERFACE
    libcef_dll_wrapper
    "$<$<CONFIG:Debug>:${CEF_LIB_DEBUG}>"
    "$<$<NOT:$<CONFIG:Debug>>:${CEF_LIB_RELEASE}>")
target_compile_definitions(voxels_cef INTERFACE VOXELS_HAS_CEF=1 NOMINMAX)

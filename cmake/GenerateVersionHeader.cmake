# Regenerates the engine version header on every build (not just reconfigure) so the embedded
# git commit and build timestamp are always fresh. Invoked via `cmake -P` from a custom target
# that has no tracked outputs, which CMake/Ninja/MSBuild always re-run.
#
# Expected -D arguments: VOXELS_VERSION, VOXELS_VERSION_MAJOR, VOXELS_VERSION_MINOR,
# VOXELS_VERSION_PATCH, SOURCE_DIR, TEMPLATE_FILE, OUTPUT_FILE.

find_package(Git QUIET)

set(GIT_SHA "unknown")
if(GIT_EXECUTABLE)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
        WORKING_DIRECTORY "${SOURCE_DIR}"
        OUTPUT_VARIABLE GIT_SHA_OUT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE GIT_RESULT
        ERROR_QUIET
    )
    if(GIT_RESULT EQUAL 0 AND GIT_SHA_OUT)
        set(GIT_SHA "${GIT_SHA_OUT}")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" status --porcelain
            WORKING_DIRECTORY "${SOURCE_DIR}"
            OUTPUT_VARIABLE GIT_DIRTY_OUT
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(GIT_DIRTY_OUT)
            set(GIT_SHA "${GIT_SHA}-dirty")
        endif()
    endif()
endif()

string(TIMESTAMP BUILD_TIMESTAMP "%Y-%m-%dT%H:%M:%SZ" UTC)

configure_file("${TEMPLATE_FILE}" "${OUTPUT_FILE}" @ONLY)

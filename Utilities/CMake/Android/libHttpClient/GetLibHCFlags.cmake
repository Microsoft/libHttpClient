cmake_minimum_required(VERSION 3.6)

# This function will set common, debug, and release config compiler flags
# into the three OUT_XXX variables, respectively. These are intended to then
# be passed to `target_set_flags`, from `TargetSetFlags.cmake`.
function(GET_LIBHC_FLAGS OUT_FLAGS OUT_FLAGS_DEBUG OUT_FLAGS_RELEASE)
    set(FLAGS
        "-Wall"
        "-fexceptions"
        "-frtti"
        "-std=c++14"
        "-Wno-unknown-pragmas"
        "-Wno-pragma-once-outside-header"
        "-DHC_PLATFORM_MSBUILD_GUESS=HC_PLATFORM_ANDROID"
        )

    if(DEFINED HC_NOWEBSOCKETS)
        list(APPEND FLAGS "-DHC_NOWEBSOCKETS")
    endif()

    set("${OUT_FLAGS}" "${FLAGS}" PARENT_SCOPE)

    set("${OUT_FLAGS_DEBUG}"
        "-O0"
        "-DHC_TRACE_BUILD_LEVEL=5"
        PARENT_SCOPE
        )

    set("${OUT_FLAGS_RELEASE}"
        "-Os"
        "-DHC_TRACE_BUILD_LEVEL=3"
        PARENT_SCOPE
        )
endfunction()

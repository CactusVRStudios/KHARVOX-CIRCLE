# Helpers that keep per-module CMakeLists files short and uniform.

# circle_vr_add_module(<target> SOURCES <files...> [DEPENDS <targets...>])
#
# Declares a static library for one portable module. Headers are included relative to src/, so a
# consumer writes `#include "xr_math/projection.hpp"` regardless of where it lives.
function(circle_vr_add_module target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;DEPENDS")

    add_library(${target} STATIC ${arg_SOURCES})
    target_include_directories(${target} PUBLIC "${PROJECT_SOURCE_DIR}/src")
    target_compile_features(${target} PUBLIC cxx_std_20)
    target_link_libraries(${target} PRIVATE circle_vr_warnings)
    if(arg_DEPENDS)
        target_link_libraries(${target} PUBLIC ${arg_DEPENDS})
    endif()
endfunction()

# circle_vr_add_test(<target> SOURCES <files...> DEPENDS <targets...>)
#
# Declares one doctest executable and registers it with CTest.
function(circle_vr_add_test target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;DEPENDS")

    add_executable(${target} ${arg_SOURCES})
    # circle_vr_warnings comes first so that the test-only suppressions carried by circle_vr_test_main appear
    # after it on the command line and take effect.
    target_link_libraries(${target} PRIVATE circle_vr_warnings circle_vr_test_main ${arg_DEPENDS})
    add_test(NAME ${target} COMMAND ${target})
endfunction()

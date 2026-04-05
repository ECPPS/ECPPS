cmake_minimum_required(VERSION 3.20)
project(ECPPS LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ENABLE_PGO "Enable Profile Guided Optimisation" OFF)
option(PGO_REBUILD "Enable Profile Guided Optimisation Rebuild" OFF)
option(BUILD_TESTING "Build the testing suite" OFF)
option(USE_MIMALLOC "Use mimalloc allocator" ON)
option(ENABLE_CCACHE "Use ccache compiler cache" ON)
option(ENABLE_LTO "Enable Link-Time Optimisation" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(meta-targets/ccache)
include(meta-targets/warnings)
include(meta-targets/project_options)
include(meta-targets/optimisations)
include(meta-targets/lto)
include(meta-targets/pgo)
include(meta-targets/platform)
include(meta-targets/mimalloc)
if (SANITISE)
    include(meta-targets/Sanitisers)
endif()

add_subdirectory(compiler)
add_subdirectory(stdlib)

if(BUILD_TESTING)
    include(meta-targets/catch2)
    enable_testing()
    add_subdirectory(testing)
endif()

add_subdirectory(examples)

execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink
        ${CMAKE_BINARY_DIR}/compile_commands.json
        ${CMAKE_SOURCE_DIR}/compile_commands.json
    RESULT_VARIABLE _symlink_result
    ERROR_QUIET
)
if(NOT _symlink_result EQUAL 0)
    message(WARNING "Failed to create symlink for compile_commands.json. Some tools may not work correctly.")
endif()

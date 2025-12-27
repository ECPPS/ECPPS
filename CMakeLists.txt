cmake_minimum_required(VERSION 3.20)
project(ECPPS)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)

if(MSVC AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")                                     
    add_compile_options(
        /std:c++latest
        /permissive-
        /Zc:__cplusplus
        /Zc:inline
        /Zc:throwingNew
        /Zc:preprocessor
        /Zc:lambda
        /Zc:externConstexpr
        /Zc:referenceBinding
        /Zc:sizedDealloc
        /Zc:strictStrings
        /Zc:templateScope
        /EHsc
        "$<$<CONFIG:Release>:/O2>"
        "$<$<CONFIG:Release>:/GL>"
        "$<$<CONFIG:Release>:/Gw>"
        "$<$<CONFIG:Release>:/GF>"
    )
    add_link_options("$<$<CONFIG:Release>:/LTCG>")
endif()   

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND MSVC)
    add_compile_options(
        -Weverything
        -Wno-padded
        -Wno-missing-prototypes
        -Wno-unused-parameter
        -Wno-unused-variable
        -fms-compatibility-version=19.36
        -Wno-potentially-evaluated-expression
        -Wno-unused-private-field
        -Wno-unused-function
    )
endif()

if(NOT MSVC AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(
        -Wall
        -Wextra
        -Werror
        -Wno-unused-parameter
        -Wno-unused-but-set-variable
        -Wno-potentially-evaluated-expression
        -Wno-unused-private-field
        -Wno-unused-variable
        -Wno-unused-function
        -std=c++23
        $<$<CONFIG:Release>:-O3>
    )
endif()

function(enable_pch target)
    if(MSVC AND EXISTS "${PCH_HEADER}")
        target_precompile_headers(${target} PRIVATE "${PCH_HEADER}")
    endif()
endfunction()

execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink
        ${CMAKE_BINARY_DIR}/compile_commands.json
        ${CMAKE_SOURCE_DIR}/compile_commands.json
    RESULT_VARIABLE _symlink_result
    ERROR_QUIET
)


add_subdirectory(compiler)
add_subdirectory(stdlib)
add_subdirectory(testing)
add_subdirectory(examples)

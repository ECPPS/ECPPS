cmake_minimum_required(VERSION 3.20)
project(ECPPS LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_EXTENSIONS OFF)
option(ENABLE_PGO "Enable Profile Guided Optimisation" OFF)
option(PGO_REBUILD "Enable Profile Guided Optimisation Rebuild" OFF)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
option(BUILD_TESTING "Build the testing suite" OFF)

include(FetchContent)

FetchContent_Declare(
    mimalloc
    GIT_REPOSITORY https://github.com/microsoft/mimalloc.git
    GIT_TAG        v2.2.7
)

set(MI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MI_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(MI_BUILD_SHARED OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(mimalloc)


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
        $<$<COMPILE_LANGUAGE:CXX>:-Weverything>
	   $<$<COMPILE_LANGUAGE:CXX>:-Werror>
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-padded>
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-missing-prototypes>
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-unused-parameter>
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-unused-variable>
        -fms-compatibility-version=19.36
        -Wno-potentially-evaluated-expression
        -Wno-unused-private-field
        -Wno-unused-function
	   -Wno-c++98-compat
	   -Wno-c++98-compat-pedantic
	   -Wno-pre-c++17-compat
	   -Wno-pre-c++23-compat
	   -Wno-shadow-field-in-constructor
	   -Wno-ctad-maybe-unsupported
	   -Wno-missing-noreturn
	   -Wno-reserved-macro-identifier
	   -Wno-reserved-identifier
	   -Wno-implicit-int-conversion
	   -Wno-undefined-reinterpret-cast # pretty naive of it to assume every reinterpret_cast is undefined...
	   -Wno-sign-conversion
	   -Wno-unique-object-duplication
	   -Wno-exit-time-destructors
	   -Wno-switch-enum
	   -Wno-covered-switch-default
	   -Wno-unsafe-buffer-usage-in-libc-call
	   -Wno-unsafe-buffer-usage
	   -Wno-switch-default
	   -Wno-global-constructors
	   -Wno-unused-macros
	   -Wno-deprecated-copy-with-dtor
	   -Wno-shadow-uncaptured-local
	   -Wno-comma
	   -Wno-nrvo
	   -Wno-unknown-warning-option
	   -Wno-undefined-func-template
	   -Wno-nonportable-system-include-path
	   -Wno-c++20-compat
	   -Wno-unknown-argument
	   -Wno-source-uses-openmp
        "$<$<CONFIG:Release>:/O2>"	  
        $<$<CONFIG:RelWithDebInfo>:/O2>
    )
endif()

if(NOT MSVC AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(
        $<$<COMPILE_LANGUAGE:CXX>:-Wall>
        $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
        $<$<COMPILE_LANGUAGE:CXX>:-Werror>
        -Wno-unused-parameter
        -Wno-unused-but-set-variable
        -Wno-potentially-evaluated-expression
        -Wno-unused-private-field
        -Wno-unused-variable
        -Wno-unused-function
        $<$<CONFIG:RelWithDebInfo>:-O3>
	   $<$<CONFIG:Release>:-O3>
        -std=c++23
    )
endif()

set(CLANG_LTO_FLAGS
    -flto=thin
    -fwhole-program-vtables
    -fvisibility-inlines-hidden
)
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(
        "$<$<CONFIG:Release>:${CLANG_LTO_FLAGS}>"
        "$<$<CONFIG:RelWithDebInfo>:${CLANG_LTO_FLAGS}>"
        $<$<CONFIG:Release>:-fstrict-aliasing>   
        $<$<CONFIG:Release>:-fvectorize>
        $<$<CONFIG:Release>:-fslp-vectorize>
        $<$<CONFIG:Release>:-fomit-frame-pointer>
        $<$<CONFIG:Release>:-march=x86-64-v3>
        $<$<CONFIG:RelWithDebInfo>:-fstrict-aliasing>
        $<$<CONFIG:RelWithDebInfo>:-fvectorize>
        $<$<CONFIG:RelWithDebInfo>:-fslp-vectorize>
        $<$<CONFIG:RelWithDebInfo>:-fomit-frame-pointer>
        $<$<CONFIG:RelWithDebInfo>:-march=x86-64-v3>
    )
    add_link_options(
        "$<$<CONFIG:Release>:-flto=thin>"
        "$<$<CONFIG:RelWithDebInfo>:-flto=thin>"
    )
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND ENABLE_PGO AND !PGO_REBUILD)
    add_compile_options(
        "$<$<CONFIG:Release>:-fprofile-use>"
        "$<$<CONFIG:RelWithDebInfo>:-fprofile-use>"
    )
    add_link_options(
        "$<$<CONFIG:Release>:-fprofile-use>"
        "$<$<CONFIG:Release>:-fprofile-correction>"
        "$<$<CONFIG:RelWithDebInfo>:-fprofile-use>"
        "$<$<CONFIG:RelWithDebInfo>:-fprofile-correction>"
    )
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND ENABLE_PGO AND PGO_REBUILD)
    add_compile_options(
        "$<$<CONFIG:Release>:-fprofile-create>"
        "$<$<CONFIG:RelWithDebInfo>:-fprofile-create>"
    )
    add_link_options(
        "$<$<CONFIG:Release>:-fprofile-create>"
        "$<$<CONFIG:RelWithDebInfo>:-fprofile-create>"
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

if(TARGET mimalloc-static)
    target_compile_options(mimalloc-static PRIVATE
        -w
    )
endif()

add_subdirectory(compiler)
add_subdirectory(stdlib)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(testing)
endif()
add_subdirectory(examples)

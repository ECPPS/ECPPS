add_library(ecpps_mimalloc INTERFACE)

if(NOT USE_MIMALLOC)
    target_compile_definitions(ecpps_mimalloc
        INTERFACE
            MI_NO_MIMALLOC=1
    )
    return()
endif()

include(FetchContent)

FetchContent_Declare(
    mimalloc
    GIT_REPOSITORY https://github.com/microsoft/mimalloc.git
    GIT_TAG acf2fdd329f9dc2a7ffe3f12a133fe7175e39378
)

set(MI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MI_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(MI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(MI_BUILD_OBJECT OFF CACHE BOOL "" FORCE)

set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

FetchContent_MakeAvailable(mimalloc)

if(NOT TARGET mimalloc-static)
    message(FATAL_ERROR
        "USE_MIMALLOC=ON but mimalloc-static target was not created"
    )
endif()

if(ENABLE_LTO AND WIN32 AND CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(mimalloc-static
        PRIVATE
            $<$<CONFIG:Release>:-Wa,-mbig-obj>
    )
endif()

target_link_libraries(ecpps_mimalloc
    INTERFACE
        mimalloc-static
)

target_compile_definitions(ecpps_mimalloc
    INTERFACE
        MI_OVERRIDE=1
)

target_compile_options(mimalloc-static
    PRIVATE
        $<$<C_COMPILER_ID:MSVC>:/W0>
        $<$<NOT:$<C_COMPILER_ID:MSVC>>:-w>
)

target_include_directories(ecpps_mimalloc
    SYSTEM
    INTERFACE
        "${mimalloc_SOURCE_DIR}/include"
)

target_precompile_headers(ecpps_mimalloc
    INTERFACE
        <mimalloc.h>
)

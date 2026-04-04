add_library(ecpps_mimalloc INTERFACE)

if(USE_MIMALLOC)
     include(FetchContent)

     FetchContent_Declare(
          mimalloc
          GIT_REPOSITORY https://github.com/microsoft/mimalloc.git
          GIT_TAG v2.2.7
     )

     set(MI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
     set(MI_BUILD_BENCH OFF CACHE BOOL "" FORCE)
     set(MI_BUILD_SHARED OFF CACHE BOOL "" FORCE)

     FetchContent_MakeAvailable(mimalloc)

     if(TARGET mimalloc-static)
          target_link_libraries(ecpps_mimalloc INTERFACE mimalloc-static)
          target_compile_definitions(ecpps_mimalloc INTERFACE MI_OVERRIDE=1)
          target_compile_options(mimalloc-static PRIVATE -w)
          
          target_include_directories(ecpps_mimalloc SYSTEM INTERFACE "${mimalloc_SOURCE_DIR}/include")
          target_precompile_headers(ecpps_mimalloc INTERFACE <mimalloc.h>)
     endif()
endif()

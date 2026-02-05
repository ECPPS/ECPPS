## Split libecpps into smaller subsystem static libraries to improve parallelism

file(GLOB_RECURSE PARSING_SOURCES
    "src/Parsing/*.cpp"
    "src/Parsing/*.cxx"
)

file(GLOB_RECURSE TYPESYSTEM_SOURCES
    "src/TypeSystem/*.cpp"
    "src/TypeSystem/*.cxx"
)

file(GLOB_RECURSE BACKEND_SOURCES
    "src/CodeGeneration/*.cpp"
    "src/CodeGeneration/*.cxx"
    "src/Machine/*.cpp"
    "src/Machine/*.cxx"
    "src/Linker/*.cpp"
    "src/Linker/*.cxx"
    "src/Execution/*.cpp"
    "src/Execution/*.cxx"
)

file(GLOB_RECURSE SHARED_SOURCES
    "src/Shared/*.cpp"
    "src/Shared/*.cxx"
    "src/Debugger/*.cpp"
    "src/Debugger/*.cxx"
)

list(FILTER SHARED_SOURCES EXCLUDE REGEX ".*/cli/.*")

add_library(parserlib STATIC ${PARSING_SOURCES})
target_include_directories(parserlib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src" "../utilitieslib/include/utilitieslib" "../platformlib/include")
target_compile_features(parserlib PUBLIC cxx_std_23)
target_precompile_headers(parserlib PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/Shared/pch.h")

add_library(typesystemlib STATIC ${TYPESYSTEM_SOURCES})
target_include_directories(typesystemlib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src" "../utilitieslib/include/utilitieslib" "../platformlib/include")
target_compile_features(typesystemlib PUBLIC cxx_std_23)
target_precompile_headers(typesystemlib PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/Shared/pch.h" "${CMAKE_CURRENT_SOURCE_DIR}/../utilitieslib/src/pch.h")

add_library(backendlib STATIC ${BACKEND_SOURCES})
target_include_directories(backendlib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src" "../utilitieslib/include/utilitieslib" "../platformlib/include")
target_compile_features(backendlib PUBLIC cxx_std_23)
target_precompile_headers(backendlib PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/Shared/pch.h")

## Core/shared library that aggregates common code and links subsystems
add_library(libecpps STATIC ${SHARED_SOURCES})
target_link_libraries(libecpps PUBLIC mimalloc-static)
target_compile_definitions(libecpps PRIVATE MI_OVERRIDE=1)

find_package(OpenMP QUIET)
if(OpenMP_CXX_FOUND)
    if (MSVC)
        target_compile_options(libecpps PRIVATE -openmp:experimental)
        if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            target_compile_options(libecpps PRIVATE -Wno-source-uses-openmp)
        endif()
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(libecpps PRIVATE -fopenmp=libomp -Wno-source-uses-openmp)
    endif()
    target_link_libraries(libecpps PRIVATE OpenMP::OpenMP_CXX)
    target_compile_definitions(libecpps PRIVATE HAVE_OMP)
else()
    target_compile_definitions(libecpps PRIVATE NO_OMP)
endif()

target_compile_features(libecpps PUBLIC cxx_std_23)
if(DEFINED mimalloc_SOURCE_DIR)
    # Ensure the mimalloc headers are visible when building the PCH
    target_include_directories(libecpps PRIVATE "${mimalloc_SOURCE_DIR}/include")
    target_precompile_headers(libecpps PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/Shared/pch.h" <mimalloc.h>)
else()
    target_precompile_headers(libecpps PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/Shared/pch.h")
endif()

target_link_libraries(libecpps PRIVATE platformlib utilitieslib parserlib typesystemlib backendlib)
target_include_directories(libecpps
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "../platformlib/include"
        "../utilitieslib/include/utilitieslib"
)

file(GLOB_RECURSE CLI_SOURCES "src/cli/*.cpp" "src/cli/*.cxx")

add_executable(ecpps ${CLI_SOURCES})
target_link_libraries(ecpps PRIVATE libecpps)
target_compile_features(ecpps PUBLIC cxx_std_23)
target_precompile_headers(ecpps PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/Shared/pch.h")

file(GLOB_RECURSE ECPPS_SOURCES
    "src/*.cpp"
    "src/*.cxx"
)

list(FILTER ECPPS_SOURCES EXCLUDE REGEX ".*/cli/.*")

add_library(libecpps STATIC ${ECPPS_SOURCES})

find_package(OpenMP QUIET)

if(OpenMP_CXX_FOUND)
    if (MSVC)
        target_compile_options(libecpps PRIVATE -openmp:experimental)
    endif()
    target_link_libraries(libecpps PRIVATE OpenMP::OpenMP_CXX)
    target_compile_definitions(libecpps PRIVATE HAVE_OMP)
else()
    target_compile_definitions(libecpps PRIVATE NO_OMP)
endif()

target_compile_features(libecpps PUBLIC cxx_std_23)
target_precompile_headers(libecpps PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/Shared/pch.h")

target_link_libraries(libecpps PRIVATE platformlib utilitieslib)
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

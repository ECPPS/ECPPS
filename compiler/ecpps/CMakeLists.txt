file(GLOB_RECURSE ECPPS_SOURCES
    "src/*.cpp"
    "src/*.cxx"
)

add_executable(ecpps ${ECPPS_SOURCES})

target_compile_features(ecpps PUBLIC cxx_std_23)
target_precompile_headers(ecpps PRIVATE
                          "${CMAKE_CURRENT_SOURCE_DIR}/src/Shared/pch.h")

target_link_libraries(ecpps PRIVATE platformlib utilitieslib)
target_include_directories(ecpps
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "../platformlib/include"
        "../utilitieslib/include/utilitieslib"
)

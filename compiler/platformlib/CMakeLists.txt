file(GLOB_RECURSE PLATFORMLIB_SOURCES
    "src/*.cpp"
    "src/*.cxx"
)

add_library(platformlib STATIC ${PLATFORMLIB_SOURCES})

target_include_directories(platformlib PUBLIC "include/platformlib")

target_precompile_headers(platformlib PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/pch.h")
target_link_libraries(platformlib PRIVATE utilitieslib)

file(GLOB_RECURSE PLATFORMLIB_SOURCES
    "src/*.cpp"
    "src/*.cxx"
)

add_library(platformlib STATIC ${PLATFORMLIB_SOURCES})

target_include_directories(platformlib PUBLIC "include/platformlib")

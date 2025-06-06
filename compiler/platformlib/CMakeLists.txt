add_library(platformlib STATIC
    src/identify.cpp "include/platformlib/platformlib.h")

target_include_directories(platformlib PUBLIC "include/platformlib")
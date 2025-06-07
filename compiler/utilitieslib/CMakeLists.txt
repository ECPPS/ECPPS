add_library(utilitieslib STATIC
    src/SBOVector.cpp include/utilitieslib/SBOVector.h)

target_include_directories(utilitieslib PUBLIC "include/utilitieslib")
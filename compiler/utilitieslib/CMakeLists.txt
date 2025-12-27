add_library(utilitieslib STATIC
    src/SBOVector.cpp include/utilitieslib/SBOVector.h include/utilitieslib/Numbers.h "include/utilitieslib/Queue.h" "include/utilitieslib/Assert.h" src/Assert.cpp)

target_include_directories(utilitieslib PUBLIC "include/utilitieslib")

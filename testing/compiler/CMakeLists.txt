add_executable(test_compiler
    test_compiler.cpp
)

target_link_libraries(test_compiler PRIVATE libecpps platformlib utilitieslib)

target_include_directories(test_compiler
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../../compiler/ecpps/src"
        "../../compiler/platformlib/include"
        "../../compiler/utilitieslib/include/utilitieslib"
)

target_compile_features(test_compiler PUBLIC cxx_std_23)

include(CTest)
enable_testing()
add_test(NAME UnitTests COMMAND test_compiler)

add_executable(test_compiler
    test_compiler.cpp
)

include(CTest)
enable_testing()
add_test(NAME UnitTests COMMAND test_compiler)

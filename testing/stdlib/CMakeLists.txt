add_executable(test_stdlib
    test_stdlib.cpp
)
target_link_libraries(test_stdlib PRIVATE stdlib-rt)
target_link_libraries(test_stdlib PRIVATE stdlib-crtStartup)
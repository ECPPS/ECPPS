add_executable(test_stdlib
    test_stdlib.cpp
)

target_link_libraries(test_stdlib PRIVATE stdlib-crtStartup)
target_link_libraries(test_stdlib PUBLIC stdlib-rt)
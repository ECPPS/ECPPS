add_executable(test_compiler
    test_compiler.cpp
)

if (MSVC)
    # Disable ASLR                            
    target_link_options(test_compiler PRIVATE /DYNAMICBASE:NO)
    target_link_options(test_compiler PRIVATE /DEBUG:NONE)
endif()
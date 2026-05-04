function(add_compiler_test target_name SOURCES_FILES label)
    add_executable(${target_name} ${SOURCES_FILES})

    target_link_libraries(${target_name} PRIVATE ecpps_options libecpps platformlib utilitieslib Catch2::Catch2WithMain)

    target_include_directories(${target_name}
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/../../compiler/ecpps/src"
            "../../compiler/platformlib/include"
            "../../compiler/utilitieslib/include/utilitieslib"
            "${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    )

    set_target_properties(${target_name} PROPERTIES CXX_STANDARD 23)
    target_precompile_headers(${target_name} PRIVATE "${CMAKE_SOURCE_DIR}/compiler/ecpps/src/Shared/pch.h")

    add_test(NAME ${target_name} COMMAND $<TARGET_FILE:${target_name}>)
    set_tests_properties(${target_name} PROPERTIES LABELS ${label})
endfunction()

add_compiler_test(test_bump_allocator 
    tests/unit/utilities/test_bump_allocator.cpp 
    unit)

add_compiler_test(test_type_comparison 
    tests/unit/typesystem/test_type_comparison.cpp 
    unit)

add_compiler_test(test_arithmetic_types 
    tests/unit/typesystem/test_arithmetic_types.cpp 
    unit)

add_compiler_test(test_x86_64_opcodes 
    tests/unit/test_x86_64_opcodes.cpp 
    unit)

add_compiler_test(test_tokeniser_to_ast 
    tests/integration/test_tokeniser_to_ast.cpp 
    integration)

add_compiler_test(test_e2e_comprehensive 
    tests/e2e/test_comprehensive.cpp 
    e2e)

add_compiler_test(test_pointer_arithmetic_regression 
    tests/regression/test_pointer_arithmetic.cpp 
    regression)

# new tests


add_compiler_test(test_unit_preprocessing_tokens
    tests/unit/parsing/test_pp_tokens.cpp 
    unit)


add_compiler_test(test_unit_tokens
    tests/unit/parsing/test_tokens.cpp 
    unit)

add_compiler_test(test_integration_tokens
    tests/integration/test_tokeniser.cpp 
    integration)

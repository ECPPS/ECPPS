function(add_compiler_test target_name SOURCES_FILES label)
    add_executable(${target_name} ${SOURCES_FILES})

    target_link_libraries(${target_name} PRIVATE ecpps_options libecpps platformlib utilitieslib Catch2::Catch2WithMain)

    target_include_directories(${target_name}
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/../../compiler/ecpps/src"
            "${CMAKE_CURRENT_SOURCE_DIR}/../../compiler/platformlib/include"
            "${CMAKE_CURRENT_SOURCE_DIR}/../../compiler/utilitieslib/include/utilitieslib"
            "${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    )

    target_compile_features(${target_name} PRIVATE cxx_std_23)
    target_precompile_headers(${target_name} PRIVATE "${CMAKE_SOURCE_DIR}/compiler/ecpps/src/Shared/pch.h")

    add_test(NAME ${target_name} COMMAND $<TARGET_FILE:${target_name}>)
    set_tests_properties(${target_name} PROPERTIES LABELS ${label})
    if(ENABLE_COVERAGE)
        set_tests_properties(${target_name} PROPERTIES
            ENVIRONMENT "LLVM_PROFILE_FILE=${CMAKE_BINARY_DIR}/profiles/%p.profraw"
        )
    endif()
endfunction()

add_compiler_test(test_bump_allocator                tests/unit/utilities/test_bump_allocator.cpp    unit)
add_compiler_test(test_include_directory             tests/unit/test_include_directory.cpp            unit)
add_compiler_test(test_config                        tests/unit/test_config.cpp                       unit)
add_compiler_test(test_diagnostics                   tests/unit/test_diagnostics.cpp                  unit)
add_compiler_test(test_type_comparison               tests/unit/typesystem/test_type_comparison.cpp  unit)
add_compiler_test(test_arithmetic_types              tests/unit/typesystem/test_arithmetic_types.cpp unit)
add_compiler_test(test_x86_64_opcodes                tests/unit/test_x86_64_opcodes.cpp              unit)
add_compiler_test(test_range_compat                  tests/unit/test_range_compat.cpp                unit)
add_compiler_test(test_unit_preprocessing_tokens     tests/unit/parsing/test_pp_tokens.cpp           unit)
add_compiler_test(test_unit_tokens                   tests/unit/parsing/test_tokens.cpp              unit)
add_compiler_test(test_unit_ast                      tests/unit/parsing/test_ast.cpp                 unit)
add_compiler_test(test_unit_sbo_vector               tests/unit/utilities/test_sbo_vector.cpp       unit)
add_compiler_test(test_unit_ir_overload_resolution   tests/unit/execution/test_ir_overload_resolution.cpp unit)
add_compiler_test(test_unit_ir_classes               tests/unit/execution/test_ir_classes.cpp       unit)
add_compiler_test(test_unit_ir_codegen               tests/unit/execution/test_ir_codegen.cpp       unit)
add_compiler_test(test_unit_ir_control_flow          tests/unit/execution/test_ir_control_flow.cpp  unit)

add_compiler_test(test_tokeniser_to_ast              tests/integration/test_tokeniser_to_ast.cpp     integration)
add_compiler_test(test_integration_tokens            tests/integration/test_tokeniser.cpp            integration)

add_compiler_test(test_e2e_comprehensive             tests/e2e/test_comprehensive.cpp                e2e)

add_compiler_test(test_pointer_arithmetic_regression tests/regression/test_pointer_arithmetic.cpp    regression)

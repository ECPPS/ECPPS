function(add_compiler_test target_name SOURCES_FILES label)
    add_executable(${target_name} ${SOURCES_FILES})

    target_link_libraries(${target_name} PRIVATE libecpps platformlib utilitieslib)

    target_include_directories(${target_name}
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/../../compiler/ecpps/src"
            "../../compiler/platformlib/include"
            "../../compiler/utilitieslib/include/utilitieslib"
    )

    target_compile_features(${target_name} PUBLIC cxx_std_23)
    target_precompile_headers(${target_name} PRIVATE "${CMAKE_SOURCE_DIR}/compiler/ecpps/src/Shared/pch.h")

    add_test(NAME ${target_name} COMMAND $<TARGET_FILE:${target_name}>)
    set_tests_properties(${target_name} PROPERTIES LABELS ${label})
endfunction()

set(UNIT_TEST_SOURCES
    tests/unit/test_tokenise.cpp
)
add_compiler_test(test_unit ${UNIT_TEST_SOURCES} unit)

set(UNIT_SBO_TEST_SOURCES
    tests/unit/test_sbo_vector.cpp
)
add_compiler_test(test_sbo_vector ${UNIT_SBO_TEST_SOURCES} unit)

set(UNIT_PLATFORM_TEST_SOURCES
    tests/unit/test_platform_identify.cpp
)
add_compiler_test(test_platform_identify ${UNIT_PLATFORM_TEST_SOURCES} unit)

set(INTEGRATION_TEST_SOURCES
    tests/integration/test_ast.cpp
)
add_compiler_test(test_integration ${INTEGRATION_TEST_SOURCES} integration)

set(E2E_TEST_SOURCES
    tests/e2e/test_int_main.cpp
)
add_compiler_test(test_e2e ${E2E_TEST_SOURCES} e2e)

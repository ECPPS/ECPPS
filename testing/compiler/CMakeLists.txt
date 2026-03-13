# Download Catch2 amalgamated files to build directory
set(CATCH2_DOWNLOAD_DIR "${CMAKE_CURRENT_BINARY_DIR}/_deps/catch2")
set(CATCH2_HPP "${CATCH2_DOWNLOAD_DIR}/catch_amalgamated.hpp")
set(CATCH2_CPP "${CATCH2_DOWNLOAD_DIR}/catch_amalgamated.cpp")

if(NOT EXISTS "${CATCH2_HPP}" OR NOT EXISTS "${CATCH2_CPP}")
    message(STATUS "Downloading Catch2 v3.5.2 amalgamated files to ${CATCH2_DOWNLOAD_DIR}...")
    file(MAKE_DIRECTORY "${CATCH2_DOWNLOAD_DIR}")
    
    file(DOWNLOAD
        "https://github.com/catchorg/Catch2/releases/download/v3.5.2/catch_amalgamated.hpp"
        "${CATCH2_HPP}"
        SHOW_PROGRESS
        STATUS download_status_hpp
    )
    
    file(DOWNLOAD
        "https://github.com/catchorg/Catch2/releases/download/v3.5.2/catch_amalgamated.cpp"
        "${CATCH2_CPP}"
        SHOW_PROGRESS
        STATUS download_status_cpp
    )
    
    list(GET download_status_hpp 0 status_code_hpp)
    list(GET download_status_cpp 0 status_code_cpp)
    
    if(NOT status_code_hpp EQUAL 0 OR NOT status_code_cpp EQUAL 0)
        message(FATAL_ERROR "Failed to download Catch2 amalgamated files")
    endif()
    
    message(STATUS "Catch2 downloaded successfully")
endif()

# Build Catch2 library from downloaded files
add_library(Catch2 STATIC "${CATCH2_CPP}")
target_include_directories(Catch2 PUBLIC "${CATCH2_DOWNLOAD_DIR}")
target_compile_features(Catch2 PUBLIC cxx_std_17)

# Disable all warnings for third-party Catch2 library
target_compile_options(Catch2 PRIVATE
    -w
    $<$<CXX_COMPILER_ID:MSVC>:/W0>
)

add_library(Catch2::Catch2WithMain ALIAS Catch2)

function(add_compiler_test target_name SOURCES_FILES label)
    add_executable(${target_name} ${SOURCES_FILES})

    target_link_libraries(${target_name} PRIVATE libecpps platformlib utilitieslib Catch2::Catch2WithMain)

    target_include_directories(${target_name}
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/../../compiler/ecpps/src"
            "../../compiler/platformlib/include"
            "../../compiler/utilitieslib/include/utilitieslib"
            "${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    )

    target_compile_features(${target_name} PUBLIC cxx_std_23)
    target_precompile_headers(${target_name} PRIVATE "${CMAKE_SOURCE_DIR}/compiler/ecpps/src/Shared/pch.h")

    add_test(NAME ${target_name} COMMAND $<TARGET_FILE:${target_name}>)
    set_tests_properties(${target_name} PROPERTIES LABELS ${label})
endfunction()

# ======================
# UNIT TESTS - UTILITIES
# ======================

add_compiler_test(test_bump_allocator 
    tests/unit/utilities/test_bump_allocator.cpp 
    unit)

add_compiler_test(test_sbo_vector_comprehensive 
    tests/unit/utilities/test_sbo_vector.cpp 
    unit)

add_compiler_test(test_platform_identify 
    tests/unit/test_platform_identify.cpp 
    unit)

# ======================
# UNIT TESTS - PARSING
# ======================

# TEMPORARILY DISABLED - Tokeniser API mismatches
add_compiler_test(test_tokenise_comprehensive 
    tests/unit/parsing/test_tokenise.cpp 
    unit)

add_compiler_test(test_unit_legacy 
    tests/unit/test_tokenise.cpp 
    unit)

# ======================
# UNIT TESTS - TYPE SYSTEM
# ======================

add_compiler_test(test_type_comparison 
    tests/unit/typesystem/test_type_comparison.cpp 
    unit)

add_compiler_test(test_arithmetic_types 
    tests/unit/typesystem/test_arithmetic_types.cpp 
    unit)

# ======================
# UNIT TESTS - CODE GENERATION
# ======================

add_compiler_test(test_x86_64_opcodes 
    tests/unit/test_x86_64_opcodes.cpp 
    unit)

# ======================
# INTEGRATION TESTS
# ======================

add_compiler_test(test_tokeniser_to_ast 
    tests/integration/test_tokeniser_to_ast.cpp 
    integration)

add_compiler_test(test_integration_legacy 
    tests/integration/test_ast.cpp 
    integration)

# ======================
# E2E TESTS
# ======================

add_compiler_test(test_e2e_comprehensive 
    tests/e2e/test_comprehensive.cpp 
    e2e)

add_compiler_test(test_e2e_legacy 
    tests/e2e/test_int_main.cpp 
    e2e)

# ======================
# REGRESSION TESTS
# ======================

add_compiler_test(test_pointer_arithmetic_regression 
    tests/regression/test_pointer_arithmetic.cpp 
    regression)

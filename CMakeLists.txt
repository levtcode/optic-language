# Build the CLI independently from the compiler; neither target embeds the other.
# The compiler frontend is shared with its tests. Keep backend/runtime additions in their components.
cmake_minimum_required(VERSION 3.20)
project(Optic VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

function(optic_configure_target target)
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}")
    target_compile_definitions(${target} PRIVATE
        OPTIC_VERSION="${PROJECT_VERSION}"
    )
    if(MSVC)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_executable(optic optic.cpp cli/cli_tool.cpp cli/process.cpp)
optic_configure_target(optic)

# One frontend implementation is shared by the compiler and its C++ contract tests.
add_library(optic_frontend STATIC
    compiler/driver/diagnostics_engine.cpp
    compiler/driver/module.cpp
    compiler/lexer/lexer.cpp
    compiler/preprocessor/preprocessor.cpp
    compiler/preprocessor/macro.cpp
)
optic_configure_target(optic_frontend)
add_executable(opticc compiler/opticc.cpp compiler/driver/args.cpp
    compiler/driver/compiler_instance.cpp)
optic_configure_target(opticc)
target_link_libraries(opticc PRIVATE optic_frontend)

install(TARGETS optic opticc RUNTIME DESTINATION bin)

# Optional Python integration checks exercise real process/argument boundaries.
include(CTest)
if(BUILD_TESTING)
    add_executable(optic_frontend_tests tests/frontend.cpp)
    target_link_libraries(optic_frontend_tests PRIVATE optic_frontend)
    add_executable(optic_macro_tests tests/macros.cpp)
    optic_configure_target(optic_macro_tests)
    target_link_libraries(optic_macro_tests PRIVATE optic_frontend)
    add_test(NAME macros COMMAND optic_macro_tests)
    optic_configure_target(optic_frontend_tests)
    add_test(NAME frontend COMMAND optic_frontend_tests)
    set_tests_properties(frontend macros PROPERTIES TIMEOUT 30)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_executable(optic_test_component tests/cli_component.cpp)
        add_test(NAME cli_integration COMMAND "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tests/cli_integration.py"
            "$<TARGET_FILE:optic>" "$<TARGET_FILE:opticc>"
            "$<TARGET_FILE:optic_test_component>")
        add_test(NAME preprocessor_integration COMMAND "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tests/preprocessor_integration.py" "$<TARGET_FILE:opticc>")
        add_test(NAME macro_integration COMMAND "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tests/macro_integration.py" "$<TARGET_FILE:opticc>")
        set_tests_properties(cli_integration preprocessor_integration macro_integration PROPERTIES TIMEOUT 90)
    endif()
endif()

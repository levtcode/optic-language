# Build the CLI independently from the compiler; neither target embeds the other.
# opticc owns the frontend sources. Keep backend/runtime additions in their components.
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

add_executable(opticc
    compiler/opticc.cpp
    compiler/driver/args.cpp
    compiler/driver/compiler_instance.cpp
    compiler/driver/diagnostics_engine.cpp
    compiler/driver/module.cpp
    compiler/lexer/lexer.cpp
    compiler/preprocessor/preprocessor.cpp
)
optic_configure_target(opticc)

install(TARGETS optic opticc RUNTIME DESTINATION bin)

# Optional Python integration checks exercise real process/argument boundaries.
include(CTest)
if(BUILD_TESTING)
    add_executable(optic_frontend_tests tests/frontend.cpp
        compiler/driver/module.cpp compiler/driver/diagnostics_engine.cpp
        compiler/lexer/lexer.cpp)
    optic_configure_target(optic_frontend_tests)
    add_test(NAME frontend COMMAND optic_frontend_tests)
    set_tests_properties(frontend PROPERTIES TIMEOUT 30)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_executable(optic_test_component tests/cli_component.cpp)
        add_test(NAME cli_integration COMMAND "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tests/cli_integration.py"
            "$<TARGET_FILE:optic>" "$<TARGET_FILE:opticc>"
            "$<TARGET_FILE:optic_test_component>")
        add_test(NAME preprocessor_integration COMMAND "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tests/preprocessor_integration.py" "$<TARGET_FILE:opticc>")
        set_tests_properties(cli_integration preprocessor_integration PROPERTIES TIMEOUT 60)
    endif()
endif()

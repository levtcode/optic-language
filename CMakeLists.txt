cmake_minimum_required(VERSION 3.20)

project(
    Optic
    VERSION 0.1.0
    LANGUAGES C CXX
)

# ==========================================
# Global configuration
# ==========================================

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ==========================================
# Source files
# ==========================================

# Future extensions:
# Use CmakeLists.txt in subdirectories like: compiler/CmakeLists.txt, core/CmakeLists.txt, etc.
# Then, will use `add_subdirectory()` to append CmakeLists.txt files to the Cmake config.

file(GLOB_RECURSE OPTIC_COMPILER_SOURCES
    CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/compiler/*.cpp"
    "${PROJECT_SOURCE_DIR}/core/*.cpp"
)

list(APPEND OPTIC_COMPILER_SOURCES
    "${PROJECT_SOURCE_DIR}/optic.cpp"
)

file(GLOB_RECURSE OPTIC_COMPILER_HEADERS
    CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/compiler/*.hpp"
    "${PROJECT_SOURCE_DIR}/core/*.hpp"
)

# ==========================================
# Executable
# ==========================================

add_executable(optic
    ${OPTIC_COMPILER_SOURCES}
    ${OPTIC_COMPILER_HEADERS}
)

# ==========================================
# Include directories
# ==========================================

target_include_directories(optic
    PRIVATE
        "${PROJECT_SOURCE_DIR}"
        "${PROJECT_SOURCE_DIR}/c_compat/headers"
)

# ==========================================
# Compile definitions
# ==========================================

target_compile_definitions(optic
    PRIVATE
        OPTIC_VERSION="${PROJECT_VERSION}"
        $<$<CONFIG:Debug>:OPTIC_DEBUG>
)

if(MSVC)
    target_compile_definitions(optic PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# ==========================================
# Compiler options
# ==========================================

if(MSVC)
    target_compile_options(optic
        PRIVATE
            /W4
            /permissive-
            $<$<CONFIG:Debug>:/Od>
            $<$<CONFIG:Debug>:/Zi>
            $<$<CONFIG:Release>:/O2>
    )
else()
    target_compile_options(optic
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            $<$<CONFIG:Debug>:-O0>
            $<$<CONFIG:Debug>:-g>
            $<$<CONFIG:Release>:-O3>
    )
endif()

# ==========================================
# Installation
# ==========================================

install(TARGETS optic
    RUNTIME DESTINATION bin
)

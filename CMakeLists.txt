cmake_minimum_required(VERSION 3.16)
project(CicadaTools VERSION 1.0.0 LANGUAGES CXX)

# ==================== Global Settings ====================

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Output directory for executables
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Compiler-specific settings
if(MSVC)
    add_compile_options(/utf-8 /W4 /permissive-)
    # Avoid UTF-8 warnings in string literals
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wconversion)
endif()

# ==================== Dependencies ====================

find_package(Threads REQUIRED)

# ==================== Executable ====================

add_executable(cicada_tools
    ./src/main.cpp
    ./src/core.cpp
    ./src/Transformers.cpp
    ./src/solutions.cpp
    ./src/utils.cpp
    ./src/dictionary_attack.cpp
)

target_include_directories(cicada_tools PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(cicada_tools PRIVATE
    Threads::Threads
)

# ==================== Build Options ====================

# Default build type (Release if not specified)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the build type." FORCE)
endif()

# Option to enable aggressive optimizations
option(ENABLE_LTO "Enable Link Time Optimization" OFF)
if(ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT lto_supported OUTPUT lto_error)
    if(lto_supported)
        set_property(TARGET cicada_tools PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
        message(STATUS "LTO enabled")
    else()
        message(WARNING "LTO not supported: ${lto_error}")
    endif()
endif()

# ==================== Final Message ====================

message(STATUS "=======================================")
message(STATUS "Project: ${PROJECT_NAME} ${PROJECT_VERSION}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Executable: ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/cicada_tools")
message(STATUS "=======================================")
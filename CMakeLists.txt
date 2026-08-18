cmake_minimum_required(VERSION 3.20)
project(blueprint LANGUAGES CXX VERSION 0.1.0)

# No third-party dependency is fetched, found or linked. The whole tool builds
# from a C++20 compiler and CMake alone. The Clang binary is a RUN-time input
# of the extractor, not a build-time dependency, so a machine without Clang
# still builds and still runs every part of the tool except extraction.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_library(blueprint_core
    src/io.cpp
    src/json.cpp
    src/model.cpp
    src/reader.cpp
    src/emit.cpp
    src/diagram.cpp
    src/schema.cpp
    src/diff.cpp
    src/behavior.cpp
)
target_include_directories(blueprint_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(blueprint src/main.cpp)
target_link_libraries(blueprint PRIVATE blueprint_core)

# MinGW links the C++ runtime dynamically by default, and the resulting binary
# then picks up whichever libstdc++-6.dll happens to come first on PATH. That
# is how a build that runs from one shell fails with 0xc0000139 from another.
# Linking the runtime in removes the question.
if(MINGW)
    target_link_options(blueprint PRIVATE -static-libstdc++ -static-libgcc)
endif()

# ---------------------------------------------------------------------------
# Tests. The runner lives in tests/testing.hpp: an assert macro, a registry and
# a main. CTest sees one entry per suite; each entry prints its own cases.
# ---------------------------------------------------------------------------
enable_testing()
add_executable(blueprint_tests
    tests/test_main.cpp
    tests/test_json.cpp
    tests/test_reader.cpp
    tests/test_emit.cpp
    tests/test_diagram.cpp
    tests/test_schema.cpp
    tests/test_diff.cpp
    tests/test_behavior.cpp
)
target_link_libraries(blueprint_tests PRIVATE blueprint_core)
if(MINGW)
    target_link_options(blueprint_tests PRIVATE -static-libstdc++ -static-libgcc)
endif()

foreach(suite json reader emit diagram schema diff behavior)
    add_test(NAME ${suite} COMMAND blueprint_tests ${suite})
endforeach()

# ---------------------------------------------------------------------------
# Demo. Runs the tool over its own source tree and over the bundled annotated
# example, then closes the round trip and checks it.
# ---------------------------------------------------------------------------
add_custom_target(demo
    COMMAND ${CMAKE_COMMAND}
            -DBLUEPRINT_EXE=$<TARGET_FILE:blueprint>
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/demo.cmake
    DEPENDS blueprint
    USES_TERMINAL
    VERBATIM
)

# Reproduces every number in the report. Separate from the demo because a
# demonstration should be quick and a measurement should be repeated.
add_custom_target(measure
    COMMAND ${CMAKE_COMMAND}
            -DBLUEPRINT_EXE=$<TARGET_FILE:blueprint>
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
            "-DBUILD_COMPILER=${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/measure.cmake
    DEPENDS blueprint
    USES_TERMINAL
    VERBATIM
)

# The divergence experiment: one edit at a time, and what the checker says.
add_custom_target(divergence
    COMMAND ${CMAKE_COMMAND}
            -DBLUEPRINT_EXE=$<TARGET_FILE:blueprint>
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -DBINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/divergence.cmake
    DEPENDS blueprint
    USES_TERMINAL
    VERBATIM
)

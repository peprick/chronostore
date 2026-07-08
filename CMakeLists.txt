cmake_minimum_required(VERSION 3.24)

project(
        ChronoStore
        VERSION 0.1.0
        LANGUAGES CXX
)

add_library(
        chronostore
        STATIC
        src/model.cpp
        src/memtable.cpp
)

target_include_directories(
        chronostore
        PUBLIC
        ${PROJECT_SOURCE_DIR}/include
        PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_compile_features(
        chronostore
        PUBLIC
        cxx_std_20
)

set_target_properties(
        chronostore
        PROPERTIES
        CXX_EXTENSIONS OFF
)

if(MSVC)
    target_compile_options(
            chronostore
            PRIVATE
            /W4
    )
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
            chronostore
            PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wshadow
    )
endif()

include(CTest)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
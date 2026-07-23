cmake_minimum_required(VERSION 3.24)

project(
        ChronoStore
        VERSION 0.1.0
        LANGUAGES CXX
)

find_package(Threads REQUIRED)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(CHRONOSTORE_TOP_LEVEL_DEFAULT ${PROJECT_IS_TOP_LEVEL})
set(CHRONOSTORE_BUILD_CLI_DEFAULT ${CHRONOSTORE_TOP_LEVEL_DEFAULT})
set(CHRONOSTORE_BUILD_TESTS_DEFAULT ${CHRONOSTORE_TOP_LEVEL_DEFAULT})

if(PROJECT_IS_TOP_LEVEL AND DEFINED BUILD_TESTING)
    set(CHRONOSTORE_BUILD_TESTS_DEFAULT ${BUILD_TESTING})
endif()

if(DEFINED CHRONOSTORE_BUILD_TOOLS)
    set(CHRONOSTORE_BUILD_TOOLS_VALUE ${CHRONOSTORE_BUILD_TOOLS})
    message(DEPRECATION "CHRONOSTORE_BUILD_TOOLS is deprecated; use CHRONOSTORE_BUILD_CLI")

    if(NOT DEFINED CHRONOSTORE_BUILD_CLI)
        set(CHRONOSTORE_BUILD_CLI_DEFAULT ${CHRONOSTORE_BUILD_TOOLS_VALUE})
    endif()
endif()

option(
        CHRONOSTORE_BUILD_CLI
        "Build the ChronoStore command-line client"
        ${CHRONOSTORE_BUILD_CLI_DEFAULT}
)
option(
        CHRONOSTORE_BUILD_TESTS
        "Build the ChronoStore test suite"
        ${CHRONOSTORE_BUILD_TESTS_DEFAULT}
)
option(
        CHRONOSTORE_BUILD_EXAMPLES
        "Build ChronoStore examples"
        ${CHRONOSTORE_TOP_LEVEL_DEFAULT}
)
option(CHRONOSTORE_BUILD_BENCHMARKS "Build the ChronoStore benchmark driver" OFF)
option(CHRONOSTORE_BUILD_GUI "Build the native ChronoView database inspector" OFF)
option(CHRONOSTORE_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

configure_file(
        include/chronostore/version.hpp.in
        ${PROJECT_BINARY_DIR}/generated/chronostore/version.hpp
        @ONLY
)

function(chronostore_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(
                ${target}
                PRIVATE
                -Wall
                -Wextra
                -Wpedantic
                -Wconversion
                -Wshadow
        )
    endif()
endfunction()

function(chronostore_enable_sanitizers target)
    if(CHRONOSTORE_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(
                ${target}
                PRIVATE
                -fsanitize=address,undefined
                -fno-omit-frame-pointer
        )
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endif()
endfunction()

add_library(
        chronostore
        STATIC
        src/model.cpp
        src/database.cpp
        src/database_lock.cpp
        src/file_io.cpp
        src/manifest.cpp
        src/memtable.cpp
        src/byte_codec.cpp
        src/crc32c.cpp
        src/wal_record.cpp
        src/wal_file.cpp
        src/wal_recovery.cpp
        src/storage_engine.cpp
        src/segment_block.cpp
        src/segment_block_codec.cpp
        src/segment_file.cpp
)

add_library(ChronoStore::chronostore ALIAS chronostore)

target_include_directories(
        chronostore
        PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_compile_features(
        chronostore
        PUBLIC
        cxx_std_20
)

target_link_libraries(
        chronostore
        PUBLIC
        Threads::Threads
)

set_target_properties(
        chronostore
        PROPERTIES
        CXX_EXTENSIONS OFF
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
)

chronostore_enable_warnings(chronostore)
chronostore_enable_sanitizers(chronostore)

if(CHRONOSTORE_BUILD_CLI)
    add_executable(
            chronostore_cli
            tools/chronostore_cli/main.cpp
    )

    set_target_properties(
            chronostore_cli
            PROPERTIES
            OUTPUT_NAME chronostore
    )

    target_link_libraries(
            chronostore_cli
            PRIVATE
            ChronoStore::chronostore
    )

    chronostore_enable_warnings(chronostore_cli)
    chronostore_enable_sanitizers(chronostore_cli)
endif()

if(CHRONOSTORE_BUILD_EXAMPLES)
    add_executable(chronostore_basic_example examples/basic_usage.cpp)
    target_link_libraries(chronostore_basic_example PRIVATE ChronoStore::chronostore)
    chronostore_enable_warnings(chronostore_basic_example)
    chronostore_enable_sanitizers(chronostore_basic_example)
endif()

if(CHRONOSTORE_BUILD_BENCHMARKS)
    add_executable(chronostore_benchmark tools/chronostore_benchmark/main.cpp)
    set_target_properties(chronostore_benchmark PROPERTIES OUTPUT_NAME chronostore-benchmark)
    target_link_libraries(chronostore_benchmark PRIVATE ChronoStore::chronostore)
    chronostore_enable_warnings(chronostore_benchmark)
    chronostore_enable_sanitizers(chronostore_benchmark)
endif()

if(CHRONOSTORE_BUILD_GUI)
    include(cmake/ChronoView.cmake)
endif()

if(CHRONOSTORE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)

    if(CHRONOSTORE_BUILD_CLI)
        add_test(
                NAME ChronoStoreCliSmoke
                COMMAND
                ${CMAKE_COMMAND}
                -DCHRONOSTORE_CLI=$<TARGET_FILE:chronostore_cli>
                -DCHRONOSTORE_CLI_TEST_DIR=${PROJECT_BINARY_DIR}/cli-smoke-db
                -P ${PROJECT_SOURCE_DIR}/tests/cli_smoke.cmake
        )
    endif()
endif()

install(
        TARGETS chronostore
        EXPORT ChronoStoreTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

if(CHRONOSTORE_BUILD_CLI)
    install(TARGETS chronostore_cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

if(CHRONOSTORE_BUILD_GUI)
    install(TARGETS chronoview RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

install(
        DIRECTORY include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.hpp"
)

install(
        FILES ${PROJECT_BINARY_DIR}/generated/chronostore/version.hpp
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/chronostore
)

configure_package_config_file(
        cmake/ChronoStoreConfig.cmake.in
        ${PROJECT_BINARY_DIR}/ChronoStoreConfig.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ChronoStore
)

write_basic_package_version_file(
        ${PROJECT_BINARY_DIR}/ChronoStoreConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
)

install(
        FILES
        ${PROJECT_BINARY_DIR}/ChronoStoreConfig.cmake
        ${PROJECT_BINARY_DIR}/ChronoStoreConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ChronoStore
)

install(
        EXPORT ChronoStoreTargets
        FILE ChronoStoreTargets.cmake
        NAMESPACE ChronoStore::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ChronoStore
)

install(
        FILES
        LICENSE
        THIRD_PARTY_NOTICES.md
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/ChronoStore
)

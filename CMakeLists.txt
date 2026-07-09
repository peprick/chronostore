cmake_minimum_required(VERSION 3.24)

project(
        ChronoStore
        VERSION 0.1.0
        LANGUAGES CXX
)

find_package(Threads REQUIRED)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(CHRONOSTORE_BUILD_TOOLS "Build ChronoStore command-line tools" ON)
option(CHRONOSTORE_BUILD_EXAMPLES "Build ChronoStore examples" ON)
option(CHRONOSTORE_BUILD_BENCHMARKS "Build ChronoStore benchmarks" ON)
option(CHRONOSTORE_BUILD_GUI "Build the native ChronoView database inspector" OFF)
option(CHRONOSTORE_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

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
        target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
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

include(CTest)

if(CHRONOSTORE_BUILD_TOOLS)
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
            chronostore
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
    add_executable(chronostore_bench tools/chronostore_bench/main.cpp)
    target_link_libraries(chronostore_bench PRIVATE ChronoStore::chronostore)
    chronostore_enable_warnings(chronostore_bench)
    chronostore_enable_sanitizers(chronostore_bench)
endif()

if(CHRONOSTORE_BUILD_GUI)
    include(cmake/ChronoView.cmake)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)

    if(CHRONOSTORE_BUILD_TOOLS)
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

if(CHRONOSTORE_BUILD_TOOLS)
    install(TARGETS chronostore_cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

if(CHRONOSTORE_BUILD_GUI)
    install(TARGETS chronoview RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

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

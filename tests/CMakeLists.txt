include(FetchContent)

FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.17.0
        GIT_SHALLOW TRUE
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

add_executable(
        chronostore_tests
        model_test.cpp
        database_test.cpp
        memtable_test.cpp
        byte_codec_test.cpp
        crc32c_test.cpp
        manifest_test.cpp
        wal_record_test.cpp
        wal_file_test.cpp
        wal_recovery_test.cpp
        storage_engine_test.cpp
        segment_block_test.cpp
        segment_block_codec_test.cpp
        segment_file_test.cpp
)

target_include_directories(
        chronostore_tests
        PRIVATE
        ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(
        chronostore_tests
        PRIVATE
        chronostore
        GTest::gtest_main
)

chronostore_enable_warnings(chronostore_tests)
chronostore_enable_sanitizers(chronostore_tests)

include(GoogleTest)

gtest_discover_tests(chronostore_tests)

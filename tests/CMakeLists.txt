include(FetchContent)

FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.17.0
        GIT_SHALLOW TRUE
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

add_executable(
        chronostore_tests
        model_test.cpp
)

target_link_libraries(
        chronostore_tests
        PRIVATE
        chronostore
        GTest::gtest_main
)

include(GoogleTest)

gtest_discover_tests(chronostore_tests)

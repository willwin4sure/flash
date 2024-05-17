cmake_minimum_required(VERSION 3.27)

include(CTest)
include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.6.0
)

FetchContent_MakeAvailable(Catch2)

# Add test executable
add_executable(tests tests.cpp)
target_link_libraries(tests PRIVATE Catch2::Catch2WithMain)

# Add test
enable_testing()
add_test(NAME tests COMMAND tests)

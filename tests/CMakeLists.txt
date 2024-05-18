cmake_minimum_required(VERSION 3.27)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(CTest)
include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.6.0
)

FetchContent_MakeAvailable(Catch2)

# Add test executables
add_executable(test_message test_message.cpp)
add_executable(test_ts_deque test_ts_deque.cpp)

target_link_libraries(test_message PRIVATE Catch2::Catch2WithMain)
target_link_libraries(test_ts_deque PRIVATE Catch2::Catch2WithMain)

# Add tests to CTest
enable_testing()
add_test(NAME test_message COMMAND test_message)
add_test(NAME test_ts_deque COMMAND test_ts_deque)

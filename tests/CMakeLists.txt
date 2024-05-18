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
add_executable(test_tsdeque test_tsdeque.cpp)
add_executable(test_connection test_connection.cpp)
# add_executable(test_integration test_integration.cpp)

target_link_libraries(test_message PRIVATE Catch2::Catch2WithMain)
target_link_libraries(test_tsdeque PRIVATE Catch2::Catch2WithMain)
target_link_libraries(test_connection PRIVATE Catch2::Catch2WithMain)
# target_link_libraries(test_integration PRIVATE Catch2::Catch2WithMain)

# Add tests to CTest
enable_testing()
add_test(NAME test_message COMMAND test_message)
add_test(NAME test_tsdeque COMMAND test_tsdeque)
add_test(NAME test_connection COMMAND test_connection)
# add_test(NAME test_integration COMMAND test_integration)

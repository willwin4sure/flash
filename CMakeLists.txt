cmake_minimum_required(VERSION 3.27)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(Flash VERSION 0.1.0 LANGUAGES CXX)

find_package(Boost REQUIRED)
include_directories(${Boost_INCLUDE_DIRS})

if (WIN32)
    link_libraries(ws2_32)
endif()

include_directories(${PROJECT_SOURCE_DIR}/include)

enable_testing()

add_subdirectory(tests)
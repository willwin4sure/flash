cmake_minimum_required(VERSION 3.27)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add simple example executables
# add_executable(SimpleClient SimpleClient.cpp)
add_executable(SimpleServer SimpleServer.cpp)
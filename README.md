# Flash

C++ framework for message passing in a client-server model,
abstracting away asio networking and multithreading. 

Based on a [video series by javidx9](https://www.youtube.com/watch?v=2hNdkYInj4g),
with modifications.

## How to Develop

You need C++, CMake, and Boost.

If you are on a VSCode environment with the CMake extension,
I would recommend adding the path to your Boost library path
to the CMake configuration settings.

## How to Build

Run the following sequence of commands:

```bash
mkdir build
cd build
cmake ..
make
ctest
```
cmake_minimum_required(VERSION 3.5)
project(galois LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 14)
set(COMPILER_FLAGS "-O3 -march=native -mtune=native -Wall -g")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COMPILER_FLAGS}")

# Find dependencies
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/modules")
find_package(Threads REQUIRED)
include_directories(
    ${CMAKE_SOURCE_DIR}/include
)

# Store sources & libraries to variables
# This might violate modern CMake principles, but it's convenient :)
add_library(galois_base STATIC
    src/ecal.cpp
    src/config.cpp
    src/network/rdma.cpp
    src/network/rpc.cpp
)
set(LIBS
    ${CMAKE_THREAD_LIBS_INIT}
    isal
    ibverbs
    rdmacm
    gflags
    glog
    rocksdb
)
link_libraries(
    ${LIBS}
    galois_base
)

# Main executables
add_executable(main
    src/main.cpp
)

# Test executables
add_executable(block_rw_test
    src/test/block_rw.cpp
    src/ecal.cpp
)
add_executable(ec_type2_test
    src/test/ec_type2.cpp
)

# Copy cluster.conf to binary directory
configure_file(cluster.conf ${CMAKE_BINARY_DIR}/cluster.conf COPYONLY)
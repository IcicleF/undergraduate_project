cmake_minimum_required(VERSION 3.5)
project(galois LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 14)
set(COMPILER_FLAGS "-O3 -march=native -mtune=native -Wall -g")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COMPILER_FLAGS}")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

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
add_library(galois_base_repl STATIC
    src/test/ecal_repl.cpp
    src/config.cpp
    src/network/rdma.cpp
    src/network/rpc.cpp
)
add_library(galois_base_none STATIC
    src/test/ecal_none.cpp
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
    boost_system
	boost_filesystem
    boost_serialization
    kyotocabinet
)
link_libraries(
    ${LIBS}
    galois_base
)

# Main executables
add_executable(DMServer
    src/fs/DMServer.cpp
    src/fs/DMStore.cpp
    src/fs/KVStore.cpp
    src/fs/EntryList.cpp
)
add_executable(FMServer
    src/fs/FMServer.cpp
    src/fs/FMStore.cpp
    src/fs/KVStore.cpp
    src/fs/EntryList.cpp
)
# add_executable(DataServer
#     src/fs/DataServer.cpp
# )
add_executable(LocofsClient
    src/fs/LocofsClient.cpp
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
configure_file(cluster.conf ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/cluster.conf COPYONLY)

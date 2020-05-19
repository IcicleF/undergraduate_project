cmake_minimum_required(VERSION 3.5)
project(galois LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(COMPILER_FLAGS "-O3 -march=native -mtune=native -Wall -g")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COMPILER_FLAGS}")

add_definitions(-Wno-unused-function -Wno-nested-anon-types -Wno-keyword-macro)
add_definitions(-Wno-unneeded-internal-declaration)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

include_directories(${CMAKE_SOURCE_DIR}/include)

# Deal with eRPC issues
add_subdirectory(third_party/eRPC)
add_definitions(-DERPC_INFINIBAND=true)
include_directories(third_party/eRPC/src)
link_directories(${CMAKE_ARCHIVE_OUTPUT_DIRECTORY})

# Store sources & libraries to variables
# This might violate modern CMake principles, but it's convenient :)
add_library(galois_base STATIC
    src/ecal.cpp
    src/config.cpp
    src/network/rdma.cpp
    src/network/rpc.cpp
    src/network/netif.cpp
)
link_libraries(
    pthread
    isal
    ibverbs
    rdmacm
    gflags
    boost_system
    boost_filesystem
    boost_serialization
    kyotocabinet
    galois_base
    erpc
    numa
    dl
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

# Copy cluster.conf to binary directory
configure_file(cluster.conf ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/cluster.conf COPYONLY)

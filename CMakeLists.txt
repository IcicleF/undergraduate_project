cmake_minimum_required (VERSION 3.5)
project (galois LANGUAGES CXX)

set (CMAKE_CXX_STANDARD 17)

set (COMPILER_FLAGS "-O3 -march=native -mtune=native -Wall -g -D_FILE_OFFSET_BITS=64")
set (CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} ${COMPILER_FLAGS}")

# Find dependencies
set (CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/modules")
find_package (FUSE REQUIRED)
find_package (Threads REQUIRED)
include_directories (${FUSE_INCLUDE_DIR} ${PMEM_INCLUDE_DIR} src/include)

add_executable (galois
    src/main.cpp
    src/ecal.cpp

    src/global/config.cpp

    src/network/rdma.cpp
    src/network/rpc.cpp
)

target_link_libraries (galois
    ${FUSE_LIBS}
    ${CMAKE_THREAD_LIBS_INIT}
    isal
    pmem pmemobj pmemblk
    ibverbs
)

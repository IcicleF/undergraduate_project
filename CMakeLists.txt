cmake_minimum_required (VERSION 3.5)
project (galois LANGUAGES C)

set (CMAKE_C_STANDARD 11)

set (COMPILER_FLAGS "-O3 -march=native -mtune=native -Wall -g -D_FILE_OFFSET_BITS=64")
set (CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${COMPILER_FLAGS}")

# Find dependencies
set (CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/modules")
find_package (FUSE REQUIRED)
find_package (Threads REQUIRED)
include_directories (${FUSE_INCLUDE_DIR} ${PMEM_INCLUDE_DIR} src/include)

add_executable (galois
    src/main.c

    src/global/config.c
    src/global/alloc_table.c

    src/ecal/ecal.c

    src/network/rdma.c
    src/network/cluster.c
)

target_link_libraries (galois
    ${FUSE_LIBS}
    ${CMAKE_THREAD_LIBS_INIT}
    isal
    pmem pmemobj pmemblk
    ibverbs
)

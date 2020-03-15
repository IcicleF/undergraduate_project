cmake_minimum_required (VERSION 3.5)
project (galois LANGUAGES CXX)
set (CMAKE_CXX_STANDARD 14)
set (COMPILER_FLAGS "-O3 -march=native -mtune=native -Wall -g -D_FILE_OFFSET_BITS=64")
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${COMPILER_FLAGS}")

# Find dependencies
set (CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/modules")
find_package (Threads REQUIRED)
find_package (gflags REQUIRED)
include_directories (${gflags_INCLUDE_DIR} src/include)

# Store sources & libraries to variables
# This violates modern CMake principles, but it's convenient :)
add_library (galois_lib STATIC
    src/ecal.cpp
    src/config.cpp
    src/network/rdma.cpp
    src/network/rpc.cpp
)
set (LIBS
    ${CMAKE_THREAD_LIBS_INIT}
    isal
    ibverbs
    rdmacm
    gflags
    galois_lib
)

# Main & Test executables
add_executable (galois src/main.cpp ${SOURCES})
target_link_libraries (galois ${LIBS})

file (GLOB TEST_SOURCES src/test/*.cpp)
foreach (TEST_SRC ${TEST_SOURCES})
    get_filename_component (TEST_NAME ${TEST_SRC} NAME_WE)
    add_executable (${TEST_NAME} ${TEST_SRC} ${SOURCES})
    target_link_libraries (${TEST_NAME} ${LIBS})
endforeach ()

# Copy cluster.conf
configure_file (cluster.conf ${CMAKE_BINARY_DIR}/cluster.conf COPYONLY)
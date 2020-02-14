# Find the libpmem includes and libraries
# 
# Libraries to be found:
#  libpmem
#  libpmemblk
#  libpmemobj
#  libpmempool
#
#  PMEM_DIR         - where to find libpmem*.h
#  PMEM_LIBRARIES   - List of libraries when using libpmem.
#  PMEM_FOUND       - True if libpmem is found.

# check if already in cache, be silent
IF (PMEM_INCLUDE_DIR)
    SET (PMEM_FIND_QUIETLY TRUE)
ENDIF (PMEM_INCLUDE_DIR)

# find includes
FIND_PATH (PMEM_INCLUDE_DIR
        NAMES libpmem.h libpmemblk.h libpmemobj.h
        PATHS /usr/local/include /usr/include
)

# find lib
FIND_LIBRARY (PMEM_LIBRARIES
        NAMES pmem pmemblk pmemobj
        PATHS /usr/lib /usr/local/lib
)

include ("FindPackageHandleStandardArgs")
find_package_handle_standard_args ("libpmem" DEFAULT_MSG
    PMEM_INCLUDE_DIR PMEM_LIBRARIES)

mark_as_advanced (PMEM_INCLUDE_DIR PMEM_LIBRARIES)
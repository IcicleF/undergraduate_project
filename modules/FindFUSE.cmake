# Find the FUSE (version 3) includes and library
#
#  FUSE_INCLUDE_DIR - where to find fuse.h, etc.
#  FUSE_LIBRARIES   - List of libraries when using FUSE.
#  FUSE_FOUND       - True if FUSE lib is found.

# check if already in cache, be silent
IF (FUSE_INCLUDE_DIR)
    SET (FUSE_FIND_QUIETLY TRUE)
ENDIF (FUSE_INCLUDE_DIR)

# find includes
FIND_PATH (FUSE_INCLUDE_DIR fuse.h
        /usr/local/include/fuse3
        /usr/local/include
        /usr/include
)

# find lib
FIND_LIBRARY (FUSE_LIBRARIES
        NAMES fuse3
        PATHS /usr/lib /usr/local/lib
)

include ("FindPackageHandleStandardArgs")
find_package_handle_standard_args ("libfuse3" DEFAULT_MSG
    FUSE_INCLUDE_DIR FUSE_LIBRARIES)

mark_as_advanced (FUSE_INCLUDE_DIR FUSE_LIBRARIES)
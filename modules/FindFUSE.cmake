# Find the FUSE (version 3) includes and library
#
#  FUSE_INCLUDE_DIR - where to find fuse.h, etc.
#  FUSE_LIBS   - List of libraries when using FUSE.
#  FUSE_FOUND       - True if FUSE lib is found.

# check if already in cache, be silent
if (FUSE_INCLUDE_DIR)
    set (FUSE_FIND_QUIETLY TRUE)
endif (FUSE_INCLUDE_DIR)

# find includes
find_path (FUSE_INCLUDE_DIR fuse.h
        /usr/local/include/fuse3
        /usr/local/include
        /usr/include
)

# find lib
find_library (FUSE_LIBS
        NAMES fuse3
        PATHS /usr/lib /usr/local/lib
)

include ("FindPackageHandleStandardArgs")
find_package_handle_standard_args ("libfuse3" DEFAULT_MSG
    FUSE_INCLUDE_DIR FUSE_LIBS)

mark_as_advanced (FUSE_INCLUDE_DIR FUSE_LIBS)
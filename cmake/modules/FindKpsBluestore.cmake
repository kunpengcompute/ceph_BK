#Try to find kps bluestore
#Once done, this will define

#KPS_BLUESTORE_FOUND - system has libkpsbluestore.so
#KPS_BLUESTORE_INCLUDE_DIR - the libkpsbluestore include directories
#KPS_BLUESTORE_LIBRARIES - link these to use libkpsbluestore

if(KPS_BLUESTORE_INCLUDE_DIR AND KPS_BLUESTORE_LIBRARIES)
   set(KPSBLUESTORE_FIND_QUIETLY TRUE)
endif(KPS_BLUESTORE_INCLUDE_DIR AND KPS_BLUESTORE_LIBRARIES)

INCLUDE(CheckCXXSymbolExists)

# include dir

find_path(KPS_BLUESTORE_INCLUDE_DIR kps_bluestore.h NO_DEFAULT_PATH PATHS
  /usr/include
  /usr/local/include
)

# finally the library itself
find_library(LIBKPS_BLUESTORE NAMES kps_bluestore NO_DEFAULT_PATH PATHS
  /usr/lib64
  /usr/local/lib64
)
set(KPS_BLUESTORE_LIBRARIES ${LIBKPS_BLUESTORE})

# handle the QUIETLY and REQUIRED arguments and set KPS_BLUESTORE_FOUND to TRUE if
# all listed variables are TRUE
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(kps_bluestore DEFAULT_MSG KPS_BLUESTORE_LIBRARIES KPS_BLUESTORE_INCLUDE_DIR)

mark_as_advanced(KPS_BLUESTORE_LIBRARIES KPS_BLUESTORE_INCLUDE_DIR)

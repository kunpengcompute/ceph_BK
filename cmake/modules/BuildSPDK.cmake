macro(build_spdk)
  set(DPDK_DIR ${CMAKE_BINARY_DIR}/src/dpdk)
  if(NOT TARGET dpdk-ext)
    include(BuildDPDK)
    build_dpdk(${DPDK_DIR})
  endif()
  find_package(CUnit REQUIRED)
  if(LINUX)
    find_package(aio REQUIRED)
    find_package(uuid REQUIRED)
  endif()
  include(ExternalProject)

  ExternalProject_Add(spdk-ext
    DEPENDS dpdk-ext
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/src/spdk
    CONFIGURE_COMMAND ./configure
    # unset $CFLAGS, otherwise it will interfere with how SPDK sets
    # its include directory.
    # unset $LDFLAGS, otherwise SPDK will fail to mock some functions.
    BUILD_COMMAND env -i PATH=$ENV{PATH} CC=${CMAKE_C_COMPILER} $(MAKE) EXTRA_CFLAGS="-fPIC"
    BUILD_IN_SOURCE 1
    INSTALL_COMMAND "true")

  set(DPDK_LIB ${CMAKE_SOURCE_DIR}/src/spdk/dpdk/build/lib)
  set(DPDK_USE_PATH ${DPDK_DIR}/lib)
  ExternalProject_Add(dpdk-cp
  DEPENDS spdk-ext
  SOURCE_DIR ${CMAKE_SOURCE_DIR}/src/spdk/dpdk
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ""
  INSTALL_COMMAND rm -rf ${DPDK_USE_PATH} && cp -rf ${DPDK_LIB} ${DPDK_DIR}
  )

  ExternalProject_Get_Property(spdk-ext source_dir)
  foreach(c nvme lvol env_dpdk sock nvmf bdev conf thread trace notify accel event_accel blob vmd event_vmd event_bdev sock_posix event_sock event rpc jsonrpc json util log)
    add_library(spdk::${c} STATIC IMPORTED)
    add_dependencies(spdk::${c} spdk-ext)
    set_target_properties(spdk::${c} PROPERTIES
      IMPORTED_LOCATION "${source_dir}/build/lib/${CMAKE_STATIC_LIBRARY_PREFIX}spdk_${c}${CMAKE_STATIC_LIBRARY_SUFFIX}"
      INTERFACE_INCLUDE_DIRECTORIES "${source_dir}/include")
    list(APPEND SPDK_LIBRARIES spdk::${c})
  endforeach()
  set_target_properties(spdk::env_dpdk PROPERTIES
    INTERFACE_LINK_LIBRARIES "dpdk::dpdk;rt")
  set_target_properties(spdk::lvol PROPERTIES
    INTERFACE_LINK_LIBRARIES spdk::util)
  set_target_properties(spdk::util PROPERTIES
    INTERFACE_LINK_LIBRARIES ${UUID_LIBRARIES})
  set_target_properties(spdk::nvme PROPERTIES
    INTERFACE_LINK_LIBRARIES "spdk::env_dpdk")
  set_target_properties(spdk::trace PROPERTIES
    INTERFACE_LINK_LIBRARIES "spdk::env_dpdk")
  set(SPDK_INCLUDE_DIR "${source_dir}/include")
  unset(source_dir)
endmacro()

# zstd

# libzstd - build it statically
set(ZSTD_C_FLAGS "-fPIC -Wno-unused-variable -O3")

if (NOT WITH_SHARED_ZSTD)
    message(STATUS "use static zstd")
    include(ExternalProject)
    ExternalProject_Add(zstd_ext
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/src/zstd/build/cmake
    CMAKE_ARGS -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
                -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
                -DCMAKE_C_FLAGS=${ZSTD_C_FLAGS}
                -DCMAKE_AR=${CMAKE_AR}
                -DCMAKE_POSITION_INDEPENDENT_CODE=${ENABLE_SHARED}
    BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/libzstd
    BUILD_COMMAND $(MAKE) libzstd_static
    INSTALL_COMMAND "true")

    # force zstd make to be called on each time
    ExternalProject_Add_Step(zstd_ext forcebuild
    DEPENDEES configure
    DEPENDERS build
    COMMAND "true"
    ALWAYS 1)

    add_library(zstd STATIC IMPORTED)
    set_target_properties(zstd PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/src/zstd/lib"
    IMPORTED_LOCATION "${CMAKE_CURRENT_BINARY_DIR}/libzstd/lib/libzstd.a")
    add_dependencies(zstd zstd_ext)
endif()

set(zstd_sources
  CompressionPluginZstd.cc
  ZstdCompressor.cc
)

add_library(ceph_zstd SHARED ${zstd_sources})
target_link_libraries(ceph_zstd PRIVATE zstd)
if (WITH_SHARED_ZSTD)
    message(STATUS "use shared zstd")
    target_include_directories(ceph_zstd PRIVATE /usr/local/include)
    target_compile_definitions(ceph_zstd PRIVATE -DWITH_SHARED_ZSTD)
    target_link_directories(ceph_zstd PRIVATE /usr/local/lib)
endif()
set_target_properties(ceph_zstd PROPERTIES
  VERSION 2.0.0
  SOVERSION 2
  INSTALL_RPATH "")
install(TARGETS ceph_zstd DESTINATION ${compressor_plugin_dir})

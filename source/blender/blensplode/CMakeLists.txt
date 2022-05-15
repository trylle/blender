set (CMAKE_CXX_STANDARD 20)

include_directories(
  ../blenlib
  ../blenloader
  ../blenloader/intern
  ../blenkernel
  ../makesdna
  ../makesrna
  ../imbuf
  ../depsgraph
  ../editors/include
  ../../../intern/ghost
  ../../../intern/guardedalloc
  ../../../intern/clog
  ../../../extern/json/include
  ../../../extern/tinygltf

  # dna_type_offsets.h
  ${CMAKE_CURRENT_BINARY_DIR}/../makesdna/intern
)

include_directories(
  SYSTEM
  ${ZLIB_INCLUDE_DIRS}
)

set(SRC
  src/blensplode.cc
  src/explode.cc
  src/implode.cc
  src/common.cc
)

if(WITH_BUILDINFO)
  list(APPEND SRC
    "$<TARGET_OBJECTS:buildinfoobj>"
  )
endif()

add_executable(blensplode ${SRC})
setup_platform_linker_flags(blensplode)
target_link_libraries(blensplode bf_blenlib bf_blenloader bf_blenkernel)
target_link_libraries(blensplode ${PTHREADS_LIBRARIES})
target_link_options(blensplode PUBLIC -fuse-ld=lld)
target_precompile_headers(blensplode PUBLIC src/blensplode_pch.h)
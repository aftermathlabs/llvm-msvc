set(LLVM_LINK_COMPONENTS
  Support
  TargetParser
  )

add_lld_tool(lld
  lld.cpp

  SUPPORT_PLUGINS
  GENERATE_DRIVER
  )
export_executable_symbols_for_plugins(lld)

function(lld_target_link_libraries target type)
  target_link_libraries(${target} ${type} ${ARGN})
  if (TARGET obj.${target})
    target_link_libraries(obj.${target} ${ARGN})
  endif()
endfunction()

lld_target_link_libraries(lld
  PRIVATE
  lldCommon
  lldCOFF
  lldELF
  lldMachO
  lldMinGW
  lldWasm
  )

if(LLD_ENABLE_MIMALLOC)
  set(_lld_mimalloc_lib "${LLD_MIMALLOC_LIBRARY}")
  if(NOT _lld_mimalloc_lib)
    find_library(_lld_mimalloc_lib NAMES mimalloc-override mimalloc)
  endif()
  if(NOT _lld_mimalloc_lib)
    message(FATAL_ERROR
      "LLD_ENABLE_MIMALLOC=ON but no mimalloc library found. "
      "Set LLD_MIMALLOC_LIBRARY to an absolute library path.")
  endif()
  message(STATUS "LLD: linking mimalloc library: ${_lld_mimalloc_lib}")
  target_link_libraries(lld PRIVATE "${_lld_mimalloc_lib}")
endif()

install(TARGETS lld
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
if(WIN32 AND LLVM_ENABLE_PACK_PDB)
  set(LLD_PDB_SOURCE_PATH "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}/bin/lld.pdb")
  install(CODE "
    file(INSTALL
      \"${LLD_PDB_SOURCE_PATH}\"
      DESTINATION \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\")
    ")
endif()

if(NOT LLD_SYMLINKS_TO_CREATE)
  set(LLD_SYMLINKS_TO_CREATE
      lld-link ld.lld ld64.lld wasm-ld)
endif()

foreach(link ${LLD_SYMLINKS_TO_CREATE})
  add_lld_symlink(${link} lld)
endforeach()

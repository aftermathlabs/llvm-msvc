# Building shared libraries requires PIC objects.
if(LLVM_ENABLE_PIC)

  set(LLVM_LINK_COMPONENTS
    Remarks
    )

  set(SOURCES
    libremarks.cpp
    )

  if (NOT (BUILD_SHARED_LIBS OR LLVM_LINK_LLVM_DYLIB))
    set(LLVM_EXPORTED_SYMBOL_FILE ${CMAKE_CURRENT_SOURCE_DIR}/Remarks.exports)
  endif()

  add_llvm_library(Remarks SHARED INSTALL_WITH_TOOLCHAIN ${SOURCES})

  
  install(FILES ${LLVM_MAIN_INCLUDE_DIR}/llvm-c/Remarks.h
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/llvm-c"
    COMPONENT Remarks)

  if (APPLE)
    set(REMARKS_VERSION ${LLVM_VERSION_MAJOR})
    set_property(TARGET Remarks APPEND_STRING PROPERTY
                 LINK_FLAGS
                 " -compatibility_version 1 -current_version ${REMARKS_VERSION}.${LLVM_VERSION_MINOR}.${LLVM_VERSION_PATCH}")
  endif()

endif()

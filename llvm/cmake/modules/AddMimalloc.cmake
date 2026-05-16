# AddMimalloc.cmake — fetch & statically link mimalloc with MI_OVERRIDE=ON.
# Replaces malloc/free + operator new/delete across the whole process
# on all platforms (Windows, Linux, macOS).

if(_LLVM_ADD_MIMALLOC_INCLUDED)
  return()
endif()
set(_LLVM_ADD_MIMALLOC_INCLUDED TRUE)

include(FetchContent)

if(NOT DEFINED LLVM_MIMALLOC_GIT_TAG OR LLVM_MIMALLOC_GIT_TAG STREQUAL "")
  file(DOWNLOAD
    "https://api.github.com/repos/microsoft/mimalloc/releases/latest"
    "${CMAKE_BINARY_DIR}/_mimalloc_latest.json"
    STATUS _mi_dl_status
    TIMEOUT 10)
  list(GET _mi_dl_status 0 _mi_dl_rc)
  if(_mi_dl_rc EQUAL 0)
    file(READ "${CMAKE_BINARY_DIR}/_mimalloc_latest.json" _mi_json)
    string(JSON _mi_latest_tag GET "${_mi_json}" "tag_name")
    set(LLVM_MIMALLOC_GIT_TAG "${_mi_latest_tag}" CACHE STRING
        "Mimalloc git tag (auto-detected from GitHub)." FORCE)
    message(STATUS "mimalloc: auto-detected latest release ${_mi_latest_tag}")
  else()
    set(LLVM_MIMALLOC_GIT_TAG "v3.3.2" CACHE STRING
        "Mimalloc git tag (fallback, GitHub unreachable)." FORCE)
    message(STATUS "mimalloc: GitHub unreachable, falling back to v3.3.2")
  endif()
  mark_as_advanced(LLVM_MIMALLOC_GIT_TAG)
endif()

function(llvm_fetch_mimalloc)
  set(MI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
  set(MI_BUILD_STATIC ON  CACHE BOOL "" FORCE)
  set(MI_BUILD_OBJECT OFF CACHE BOOL "" FORCE)
  set(MI_BUILD_TESTS  OFF CACHE BOOL "" FORCE)
  set(MI_OVERRIDE     ON  CACHE BOOL "" FORCE)
  set(MI_INSTALL_TOPLEVEL OFF CACHE BOOL "" FORCE)
  set(MI_NO_PADDING   OFF CACHE BOOL "" FORCE)
  set(MI_SKIP_COLLECT_ON_EXIT ON CACHE BOOL "" FORCE)

  FetchContent_Declare(mimalloc
    GIT_REPOSITORY https://github.com/microsoft/mimalloc.git
    GIT_TAG        ${LLVM_MIMALLOC_GIT_TAG}
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   FALSE
  )
  FetchContent_MakeAvailable(mimalloc)

  if(NOT TARGET mimalloc-static)
    message(FATAL_ERROR
      "AddMimalloc: 'mimalloc-static' target was not created.")
  endif()

  if(NOT MSVC)
    target_compile_options(mimalloc-static PRIVATE -w)
  endif()
  set_target_properties(mimalloc-static PROPERTIES FOLDER "Third-Party")
endfunction()

# llvm_link_mimalloc(<target>)
# Attach the mimalloc static lib to <target> with whole-archive linking.
# Uses add_dependencies + target_link_options (not target_link_libraries)
# to avoid pulling mimalloc-static into LLVM's cmake export sets.
function(llvm_link_mimalloc target)
  if(NOT TARGET mimalloc-static)
    message(FATAL_ERROR
      "llvm_link_mimalloc: call llvm_fetch_mimalloc() first.")
  endif()

  get_target_property(_target_type ${target} TYPE)

  if(_target_type STREQUAL "STATIC_LIBRARY")
    set(_link_scope INTERFACE)
  else()
    set(_link_scope PRIVATE)
  endif()

  add_dependencies(${target} mimalloc-static)

  if(APPLE)
    target_link_options(${target} ${_link_scope}
      "LINKER:-force_load,$<TARGET_FILE:mimalloc-static>")
  elseif(MSVC)
    target_link_options(${target} ${_link_scope}
      "/WHOLEARCHIVE:$<TARGET_FILE:mimalloc-static>")
  else()
    target_link_options(${target} ${_link_scope}
      "LINKER:--whole-archive"
      "$<TARGET_FILE:mimalloc-static>"
      "LINKER:--no-whole-archive")
  endif()
endfunction()

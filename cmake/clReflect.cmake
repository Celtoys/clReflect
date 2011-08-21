
macro(add_header_files srcs)
  file(GLOB hds *.h *.def)
  if( hds )
    set_source_files_properties(${hds} PROPERTIES HEADER_FILE_ONLY ON)
    list(APPEND ${srcs} ${hds})
  endif()
endmacro(add_header_files)


function(clreflect_process_sources OUT_VAR)
  set( sources ${ARGN} )
  #if( MSVC_IDE )
    # This adds .h files to the Visual Studio solution:
    add_header_files(sources)
  #endif()

  #set( CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}" PARENT_SCOPE )
  set( ${OUT_VAR} ${sources} PARENT_SCOPE )
endfunction(clreflect_process_sources)

macro(add_clreflect_library name)
  clreflect_process_sources( ALL_FILES ${ARGN} )
  add_library( ${name} ${ALL_FILES} )
  set_target_properties(${name} PROPERTIES FOLDER "Libraries")
endmacro(add_clreflect_library name)

macro(add_clreflect_executable name)
  clreflect_process_sources( ALL_FILES ${ARGN} )
  add_executable(${name} ${ALL_FILES})
endmacro(add_clreflect_executable name)


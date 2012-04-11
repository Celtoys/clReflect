set(CL_REFLECT_EXPORT_SOURCES
  CppExport.cpp
  Main.cpp
  PtrRelocator.cpp
  )

if (MSVC)
  # TODO: merge this with sources when implementing map file parser on gcc
  set(CL_REFLECT_EXPORT_SOURCES
    ${CL_REFLECT_EXPORT_SOURCES} MapFileParser.cpp)

  set(CL_REFLECT_EXPORT_LIBS
    dbghelp.lib)
endif (MSVC)

add_clreflect_executable(clReflectExport
  ${CL_REFLECT_EXPORT_SOURCES})

target_link_libraries(clReflectExport
  clReflectCore
  clReflectCpp
  ${CL_REFLECT_EXPORT_LIBS}
  )
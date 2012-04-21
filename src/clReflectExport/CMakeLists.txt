set(CL_REFLECT_EXPORT_SOURCES
  CppExport.cpp
  Main.cpp
  PtrRelocator.cpp
  MapFileParser.cpp
  )

if (MSVC)
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
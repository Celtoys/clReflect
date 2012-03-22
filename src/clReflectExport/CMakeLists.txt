add_clreflect_executable(clReflectExport
  CppExport.cpp
  Main.cpp
  MapFileParser.cpp
  PtrRelocator.cpp
  )

target_link_libraries(clReflectExport
  clReflectCore
  clReflectCpp
  dbghelp.lib  			# FIXME: Only for MSVC
  )
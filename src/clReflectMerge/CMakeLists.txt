add_clreflect_executable(clReflectMerge
  CodeGen.cpp
  DatabaseMerge.cpp
  Main.cpp
  )

target_link_libraries(clReflectMerge
  clReflectCore
  clReflectCpp
  )
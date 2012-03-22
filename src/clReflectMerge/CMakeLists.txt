add_clreflect_executable(clReflectMerge
  DatabaseMerge.cpp
  Main.cpp
  )

target_link_libraries(clReflectMerge
  clReflectCore
  clReflectCpp
  )
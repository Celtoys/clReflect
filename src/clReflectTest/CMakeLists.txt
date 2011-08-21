project(clReflectTest)

add_clreflect_executable(clReflectTest
  Main.cpp
  Test.cpp
  TestAttributes.cpp
  TestClassImpl.cpp
  TestCollections.cpp
  TestReflectionSpecs.cpp
  TestTemplates.cpp
  )

target_link_libraries(clReflectTest
  clReflectCore
  clReflectCpp
  )
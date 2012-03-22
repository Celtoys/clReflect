add_clreflect_executable(clReflectTest
  Main.cpp
  Test.cpp
  TestArrays.cpp
  TestAttributes.cpp
  TestClassImpl.cpp
  TestCollections.cpp
  TestFunctionSerialise.cpp
  TestOffsets.cpp
  TestReflectionSpecs.cpp
  TestSerialise.cpp
  TestSerialiseJSON.cpp
  TestTemplates.cpp
  TestTypedefs.cpp
  )

target_link_libraries(clReflectTest
  clReflectCpp
  clReflectUtil
  )

# TODO: add custom commands to generated testing database file.

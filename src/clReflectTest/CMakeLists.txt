set(CL_REFLECT_TEST_SOURCES
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

add_clreflect_executable(clReflectTest ${CL_REFLECT_TEST_SOURCES})

target_link_libraries(clReflectTest
  clReflectCpp
  clReflectUtil
  )

# add project include path
get_property(inc_dirs DIRECTORY PROPERTY INCLUDE_DIRECTORIES)
foreach(inc ${inc_dirs})
  set(GEN_CPPBIN_INCLUDE_PATH ${GEN_CPPBIN_INCLUDE_PATH} -i ${inc})
endforeach(inc)

# add system include path
if (MSVC)
  set(MSVC_INSTALL_PATH $ENV{VS80COMNTOOLS}../..
    CACHE PATH "Installation path for MSVC")
  if (${MSVC_INSTALL_PATH} STREQUAL "")
    message(FATAL_ERROR "Cannot MSVC instal path")
  else()
    set(sys_inc_dirs ${sys_inc_dirs} ${MSVC_INSTALL_PATH}/VC/include)
  endif()
endif()

foreach(sys_inc ${sys_inc_dirs})
  set(GEN_CPPBIN_INCLUDE_PATH ${GEN_CPPBIN_INCLUDE_PATH} -isystem ${sys_inc})
endforeach(sys_inc)

foreach(src_file ${CL_REFLECT_TEST_SOURCES})
  string(REPLACE ".cpp" ".csv" csv_file ${src_file})
  string(REPLACE ".cpp" "_astlog.txt" astlog_file ${src_file})
  string(REPLACE ".cpp" "_speclog.txt" speclog_file ${src_file})

  # calling clscan
  add_custom_command(
    OUTPUT ${CL_REFLECT_GEN_DIRECTORY}/${csv_file}
    COMMAND clReflectScan ${CMAKE_CURRENT_SOURCE_DIR}/${src_file}
    -output ${CL_REFLECT_GEN_DIRECTORY}/${csv_file}
    -ast_log ${CL_REFLECT_GEN_DIRECTORY}/${astlog_file}
    -spec_log ${CL_REFLECT_GEN_DIRECTORY}/${speclog_file}
    ${GEN_CPPBIN_INCLUDE_PATH}
    DEPENDS clReflectScan ${csv_file})

  # adds to gen file list
  set(GEN_FILE_LIST ${GEN_FILE_LIST} ${CL_REFLECT_GEN_DIRECTORY}/${csv_file})
endforeach(src_file)

set(GEN_MERGED_CSV_FILE ${CL_REFLECT_GEN_DIRECTORY}/clReflectTest.csv)
set(GEN_CPPBIN_FILE ${CL_REFLECT_BIN_DIRECTORY}/clReflectTest.cppbin)

# merges all gen file into single csv file
add_custom_command(
  OUTPUT ${GEN_MERGED_CSV_FILE}
  COMMAND clReflectMerge ${GEN_MERGED_CSV_FILE}
  ${GEN_FILE_LIST}
  DEPENDS clReflectMerge ${GEN_FILE_LIST})

# TODO: add mapper file generation

# exports cppbin file
add_custom_command(
  OUTPUT ${GEN_CPPBIN_FILE}
  COMMAND clReflectExport ${GEN_MERGED_CSV_FILE}
  -cpp ${GEN_CPPBIN_FILE}
  -cpp_log ${GEN_CPPBIN_FILE}.log
  DEPENDS clReflectExport ${GEN_MERGED_CSV_FILE})

# This is a fake target to ensure when compiling clReflectTest,
# we also generate the corresponding cppbin file for testing.
add_custom_target(clReflect_gen_cppbin DEPENDS
  ${GEN_CPPBIN_FILE})

add_dependencies(clReflectTest clReflect_gen_cppbin)

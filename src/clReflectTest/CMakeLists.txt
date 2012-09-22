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
  clcppCodeGen.cpp
  )

add_clreflect_executable(clReflectTest ${CL_REFLECT_TEST_SOURCES})

target_link_libraries(clReflectTest
  clReflectCpp
  clReflectUtil
  )

# Map file handling
option(CL_REFLECT_GENERATE_MAP_FILE_FOR_TEST "Generate map file for test" ON)
if(CL_REFLECT_GENERATE_MAP_FILE_FOR_TEST)
  get_property(CL_REFLECT_TEST_EXECUTABLE TARGET clReflectTest PROPERTY LOCATION)

  # generates map file
  if (MSVC)
    # Generates path for map file
    string(REPLACE ".exe" ".map" CL_REFLECT_TEST_MAP ${CL_REFLECT_TEST_EXECUTABLE})

    # From what I see, currently cmake can set the map link flags,
    # but cmake does not support appending customized map file name(I may
    # be wrong on this). Luckily, by using default options of MSVC
    # we can generate a map file with same name and same path as exe file.
    # We will simply use this map file.
    set_target_properties(clReflectTest PROPERTIES LINK_FLAGS
      /MAP)
  endif()

  if (CMAKE_COMPILER_IS_GNUCXX)
    # On Linux/Mac, executables have no extensions
    set(CL_REFLECT_TEST_MAP "${CL_REFLECT_TEST_EXECUTABLE}.map")

    if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
      set(CL_REFLECT_TEST_MAP_CMD_ARGUMENT "-Wl,-map,${CL_REFLECT_TEST_MAP}")
    else ()
      set(CL_REFLECT_TEST_MAP_CMD_ARGUMENT "-Wl,-Map,${CL_REFLECT_TEST_MAP}")
    endif()

    set_target_properties(clReflectTest PROPERTIES LINK_FLAGS
      "${CL_REFLECT_TEST_MAP_CMD_ARGUMENT}")
  endif (CMAKE_COMPILER_IS_GNUCXX)

  # uses map file during exporting
  set(GEN_MAP_ARGUMENTS -map ${CL_REFLECT_TEST_MAP})
endif()

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
elseif(CMAKE_COMPILER_IS_GNUCXX)
  # automatically propagate g++ include path to clang
  EXEC_PROGRAM(sh ARGS -c '`gcc -print-prog-name=cc1plus` -v </dev/null 2>&1 | grep \" /usr\" | sed -e \"s|^[ \t]*||\" | tr \"\\\\n\" \"\;\" \; echo'
                  OUTPUT_VARIABLE GPP_SYSTEM_INCLUDES)
  set(sys_inc_dirs ${sys_inc_dirs} ${GPP_SYSTEM_INCLUDES})
endif()

if ((${CMAKE_SYSTEM_NAME} MATCHES "Darwin"))
  set(sys_inc_dirs "/usr/lib/gcc/i686-apple-darwin11/4.2.1/include")
endif ()

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
    DEPENDS clReflectScan ${src_file})

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

# exports cppbin file
add_custom_command(
  OUTPUT ${GEN_CPPBIN_FILE}
  COMMAND clReflectExport ${GEN_MERGED_CSV_FILE}
  -cpp ${GEN_CPPBIN_FILE}
  -cpp_log ${GEN_CPPBIN_FILE}.log
  ${GEN_MAP_ARGUMENTS}
  DEPENDS clReflectExport ${GEN_MERGED_CSV_FILE})

# This is a fake target to ensure when compiling clReflectTest,
# we also generate the corresponding cppbin file for testing.
add_custom_target(clReflectGenCppbin DEPENDS
  ${GEN_CPPBIN_FILE})

# clReflectGenCppbin should depends on clReflectTest since it may need map file
add_dependencies(clReflectGenCppbin clReflectTest)

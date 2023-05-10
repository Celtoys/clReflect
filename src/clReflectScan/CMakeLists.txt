# Our repository already contains one copy of llvm, so we do not
# need to use find_package
# NOTE: we use absolute path here so as we can use them for library paths
set(LLVM_INSTALL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../extern/llvm"
  CACHE PATH "Path to LLVM source.")

cmake_policy(SET CMP0057 NEW)

set( LLVM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../extern/llvm-build-gnu/lib/cmake/llvm" )
set( CLANG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../extern/llvm-build-gnu/lib/cmake/clang" )

find_package(LLVM REQUIRED CONFIG)
find_package(CLANG REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
message(STATUS "Found CLANG ${CLANG_PACKAGE_VERSION}")
message(STATUS "Using ClangConfig.cmake in: ${CLANG_DIR}")

if (CMAKE_COMPILER_IS_GNUCXX OR CMAKE_COMPILER_IS_CLANGXX)
  # GCC/Clang flags for stdint.h types
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D__STDC_LIMIT_MACROS -D__STDC_CONSTANT_MACROS -fno-rtti")
endif (CMAKE_COMPILER_IS_GNUCXX OR CMAKE_COMPILER_IS_CLANGXX)

set(LLVM_BASE_LIB_DIR "${LLVM_BUILD_DIR}/lib")
if (MSVC)
  set(LLVM_BASE_LIB_DIR "${LLVM_BASE_LIB_DIR}/$(ConfigurationName)")
endif (MSVC)

link_directories("${LLVM_BASE_LIB_DIR}")

## LLVM
include_directories(${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

# Clang
include_directories(${CLANG_INCLUDE_DIRS})
separate_arguments(CLANG_DEFINITIONS_LIST NATIVE_COMMAND ${CLANG_DEFINITIONS})
add_definitions(${CLANG_DEFINITIONS_LIST})

add_clreflect_executable(clReflectScan
  ASTConsumer.cpp
  AttributeParser.cpp
  Main.cpp
  ReflectionSpecs.cpp
  )

set(CL_REFLECT_SCAN_LIBS
  clangParse
  clangFrontend
  clangSema
  clangAnalysis
  clangLex
  clangBasic
  clangSerialization
  clangDriver
  clangAST
  clangEdit
  clangTooling
  )

if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
  # Linux version needs to be linked against pthread
  set(CL_REFLECT_SCAN_LIBS
    ${CL_REFLECT_SCAN_LIBS} pthread)
endif(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")

target_link_libraries(clReflectScan
  clReflectCore
  clReflectCpp
  ${CL_REFLECT_SCAN_LIBS}
  ${CMAKE_DL_LIBS}
  )
  
# Find the LLVM libraries that correspond to the LLVM components
# that we wish to use
llvm_map_components_to_libnames(llvm_libs support core mc x86asmparser x86codegen )
target_link_libraries(clReflectScan ${llvm_libs})

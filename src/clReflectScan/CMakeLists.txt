# Our repository already contains one copy of llvm, so we do not
# need to use find_package
# NOTE: we use absolute path here so as we can use them for library paths
set(LLVM_INSTALL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../extern/llvm"
  CACHE PATH "Path to LLVM source.")

set(LLVM_BUILD_DIR_PREDICTION "${CMAKE_CURRENT_SOURCE_DIR}/../../extern/llvm-build")
if (CMAKE_COMPILER_IS_GNUCXX)
  set(LLVM_BUILD_DIR_PREDICTION "${CMAKE_CURRENT_SOURCE_DIR}/../../extern/llvm-build-gnu")
endif (CMAKE_COMPILER_IS_GNUCXX)

set(LLVM_BUILD_DIR ${LLVM_BUILD_DIR_PREDICTION}
  CACHE PATH "Path to LLVM build.")

Include_directories("${LLVM_INSTALL_DIR}/include/")
include_directories("${LLVM_INSTALL_DIR}/tools/clang/include")
include_directories("${LLVM_BUILD_DIR}/include/")
include_directories("${LLVM_BUILD_DIR}/tools/clang/include")

if (CMAKE_COMPILER_IS_GNUCXX)
  # GCC flags for stdint.h types
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D__STDC_LIMIT_MACROS -D__STDC_CONSTANT_MACROS -fno-rtti")
endif (CMAKE_COMPILER_IS_GNUCXX)

set(LLVM_BASE_LIB_DIR "${LLVM_BUILD_DIR}/lib")
if (MSVC)
  set(LLVM_BASE_LIB_DIR "${LLVM_BASE_LIB_DIR}/$(ConfigurationName)")
endif (MSVC)

link_directories("${LLVM_BASE_LIB_DIR}")

add_clreflect_executable(clReflectScan
  ASTConsumer.cpp
  AttributeParser.cpp
  ClangFrontend.cpp
  Main.cpp
  ReflectionSpecs.cpp
  )

set(CL_REFLECT_SCAN_LIBS
  clangParse
  clangSema
  clangAnalysis
  clangFrontend
  clangLex
  clangBasic
  clangSerialization
  clangDriver
  clangAST
  LLVMSupport
  LLVMCore
  LLVMMC
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
  )

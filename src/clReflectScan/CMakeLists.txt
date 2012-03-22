# Our repository already contains one copy of llvm, so we do not
# need to use find_package
set(LLVM_INSTALL_DIR "../../extern/llvm"
  CACHE PATH "Path to LLVM source.")

set(LLVM_BUILD_DIR "../../extern/llvm-build"
  CACHE PATH "Path to LLVM build.")

include_directories("${LLVM_INSTALL_DIR}/include/")
include_directories("${LLVM_INSTALL_DIR}/tools/clang/include")
include_directories("${LLVM_BUILD_DIR}/include/")
include_directories("${LLVM_BUILD_DIR}/tools/clang/include")

set(LLVM_BASE_LIB_DIR "${LLVM_BUILD_DIR}/lib")

set(LLVM_DEBUG_LIB_DIR "${LLVM_BASE_LIB_DIR}")
set(LLVM_RELEASE_LIB_DIR "${LLVM_BASE_LIB_DIR}")

# TODO: add G++ Debug/Release directory handling when we got clReflectScan
# successfully compiling.
if (MSVC)
  # This is a hack, our source is located in src/clReflectScan,
  # so it needs two directory-up to get to ../../extern/llvm-build.
  # That's what we got in LLVM_INSTALL_DIR and LLVM_BUILD_DIR.
  #
  # But our MSVC proj file is at cmake-build/msvcsrc/clReflectScan, which
  # has one more level of directory, so we add another directory-up action
  # for library linking.
  set(LLVM_DEBUG_LIB_DIR "../${LLVM_BASE_LIB_DIR}/Debug")
  set(LLVM_RELEASE_LIB_DIR "../${LLVM_BASE_LIB_DIR}/Release")
endif (MSVC)

add_clreflect_executable(clReflectScan
  ASTConsumer.cpp
  AttributeParser.cpp
  ClangFrontend.cpp
  Main.cpp
  ReflectionSpecs.cpp
  )

set(CL_REFLECT_SCAN_CLANG_LIBS
  "clangParse"
  "clangSema"
  "clangAnalysis"
  "clangAST"
  "clangFrontend"
  "clangLex"
  "clangBasic"
  "clangSerialization"
  "clangDriver"
  "LLVMSupport"
  "LLVMCore"
  "LLVMMC")

# Add Debug and Release version of libraries separately.
foreach(lib ${CL_REFLECT_SCAN_CLANG_LIBS})
  set(CL_REFLECT_SCAN_DEBUG_LIBS ${CL_REFLECT_SCAN_DEBUG_LIBS}
    debug "${LLVM_DEBUG_LIB_DIR}/${lib}")
  set(CL_REFLECT_SCAN_RELEASE_LIBS ${CL_REFLECT_SCAN_RELEASE_LIBS}
    optimized "${LLVM_RELEASE_LIB_DIR}/${lib}")
endforeach(lib)

target_link_libraries(clReflectScan
  clReflectCore
  clReflectCpp
  ${CL_REFLECT_SCAN_DEBUG_LIBS}
  ${CL_REFLECT_SCAN_RELEASE_LIBS}
  )
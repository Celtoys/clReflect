project(clReflectScan)

# FIXME: Use CMake's find_package for this?
# http://www.vtk.org/Wiki/CMake:How_To_Find_Libraries
set(LLVM_INSTALL_DIR "../../extern/llvm"
  CACHE PATH "Path to LLVM source.")

if( LLVM_INSTALL_DIR STREQUAL "" )
  message(FATAL_ERROR "You must configure the path to the LLVM source.")
endif()

include_directories("${LLVM_INSTALL_DIR}/include/")

add_clreflect_executable(clReflectScan
  ASTConsumer.cpp
  AttributeParser.cpp
  ClangFrontend.cpp
  Main.cpp
  ReflectionSpecs.cpp
  )

target_link_libraries(clReflectScan
  clReflectCore
  clReflectCpp
  "${LLVM_INSTALL_DIR}/lib/clangParse.lib"
  "${LLVM_INSTALL_DIR}/lib/clangSema.lib"
  "${LLVM_INSTALL_DIR}/lib/clangAnalysis.lib"
  "${LLVM_INSTALL_DIR}/lib/clangAST.lib"
  "${LLVM_INSTALL_DIR}/lib/clangFrontend.lib"
  "${LLVM_INSTALL_DIR}/lib/clangLex.lib"
  "${LLVM_INSTALL_DIR}/lib/clangBasic.lib"
  "${LLVM_INSTALL_DIR}/lib/LLVMSupport.lib"
  "${LLVM_INSTALL_DIR}/lib/LLVMCore.lib"
  "${LLVM_INSTALL_DIR}/lib/LLVMMC.lib" 
  )
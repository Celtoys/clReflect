cmake_minimum_required(VERSION 2.8)
project(clReflect)

# Add path for custom modules
set(CMAKE_MODULE_PATH
  ${CMAKE_MODULE_PATH}
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake"
  )

include(clReflect)


include_directories(
  "${PROJECT_SOURCE_DIR}/src"
  "${PROJECT_SOURCE_DIR}/inc"
  )

if(MSVC)
  add_definitions(
    /D_SCL_SECURE_NO_WARNINGS
    /D_CRT_SECURE_NO_WARNINGS
  )

  # These are to allow clang/llvm to compile without hogging the output window
  add_definitions(
    /wd4146          # unary minus operator applied to unsigned type, result still unsigned
    /wd4624          # 'clang::Preprocessor::MacroInfoChain' : destructor could not be generated because a base class destructor is inaccessible
    /wd4800          # 'clang::NestedNameSpecifier *const ' : forcing value to bool 'true' or 'false' (performance warning)
    /wd4355          # 'this' : used in base member initializer list
    /wd4291          # 'void *operator new(size_t,void *) throw()' : no matching operator delete found; memory will not be freed if initialization throws an exception
    /wd4244          # 'return' : conversion from 'uint64_t' to 'unsigned int', possible loss of data  
  )
endif()


add_subdirectory(src)

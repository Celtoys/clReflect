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

if(DEFINED CMAKE_INSTALL_PREFIX)
    set(CL_REFLECT_BASE_OUTPUT_DIRECTORY "${CMAKE_INSTALL_PREFIX}")
else(DEFINED CMAKE_INSTALL_PREFIX)
    set(CL_REFLECT_BASE_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/binary/${CMAKE_BUILD_TYPE}")
endif(DEFINED CMAKE_INSTALL_PREFIX)

set(CL_REFLECT_BIN_DIRECTORY "${CL_REFLECT_BASE_OUTPUT_DIRECTORY}/bin")
set(CL_REFLECT_LIB_DIRECTORY "${CL_REFLECT_BASE_OUTPUT_DIRECTORY}/lib")
set(CL_REFLECT_GEN_DIRECTORY "${CL_REFLECT_BASE_OUTPUT_DIRECTORY}/gen")

# Sets binary output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CL_REFLECT_BIN_DIRECTORY}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CL_REFLECT_LIB_DIRECTORY}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CL_REFLECT_LIB_DIRECTORY}")

# Creates gen directory manually
file(MAKE_DIRECTORY "${CL_REFLECT_GEN_DIRECTORY}")

# Test for clang
if (CMAKE_CXX_COMPILER MATCHES ".*clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  set(CMAKE_COMPILER_IS_CLANGXX 1)
endif ()

if (CMAKE_COMPILER_IS_CLANGXX)
  # TODO: When using clang to compile clreflect, there will be many warnings says switch
  # does not handle all cases. It will be a big job to fix all cases, so we temporarily
  # disable this warning here. We may still think of fixing all the code later.
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-switch-enum")
endif (CMAKE_COMPILER_IS_CLANGXX)

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


# TODO: When we add this sub-directory, cmake on windows would actuall generate
# two directories, one is msvc(suppose we start generation in msvc),
# the other is msvcsrc, and the actual sub projects are in msvcsrc directory.
#
# Currently I do not know why, will come back to this later.
add_subdirectory(src)

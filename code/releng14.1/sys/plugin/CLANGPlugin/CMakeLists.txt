cmake_minimum_required(VERSION 3.5) # Compatibility with CMake < 3.5 will be removed from a future version of CMake.
project(clang-payload-identifier-plugin)

#===============================================================================
# 1. LOAD LLVM CONFIGURATION
#===============================================================================
# Set this to a valid LLVM installation dir
set(CT_Clang_INSTALL_DIR "" CACHE PATH "LLVM installation directory")

# Add the location of ClangConfig.cmake to CMake search paths (so that
# find_package can locate it)
list(APPEND CMAKE_PREFIX_PATH "${CT_Clang_INSTALL_DIR}/lib/cmake/clang/")

find_package(Clang REQUIRED CONFIG)

# HelloWorld includes headers from LLVM and Clang - update the include paths
# accordingly
include_directories(SYSTEM "${LLVM_INCLUDE_DIRS};${CLANG_INCLUDE_DIRS}")

#===============================================================================
# 2. LLVM-TUTOR BUILD CONFIGURATION
#===============================================================================
# Use the same C++ standard as LLVM does
set(CMAKE_CXX_STANDARD 14 CACHE STRING "")

# LLVM is normally built without RTTI. Be consistent with that.
if(NOT LLVM_ENABLE_RTTI)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rttic -shared ")
endif()

#===============================================================================
# 3. ADD THE TARGET
#===============================================================================
add_library(InsertPayload_Plugin SHARED insertPayload_Plugin.cpp)

# Allow undefined symbols in shared objects on Darwin (this is the default
# behaviour on Linux)
target_link_libraries(InsertPayload_Plugin
  "$<$<PLATFORM_ID:Darwin>:-undefined dynamic_lookup>")

# Add dir where to find "sys/metadata_payloads.h" header file
target_include_directories(InsertPayload_Plugin PUBLIC "/usr/src/sys")

  set(CMAKE_VERBOSE_MAKEFILE on)

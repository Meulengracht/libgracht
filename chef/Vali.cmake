# Cross toolchain configuration for using clang-cl on non-Windows hosts to
# target MSVC.
#
# Usage:
# cmake -G Ninja
#    -DCMAKE_TOOLCHAIN_FILE=/path/to/this/file
#    -DHOST_ARCH=[aarch64|arm64|armv7|arm|i686|x86|x86_64|x64]
#    -DMSVC_BASE=/path/to/MSVC/system/libraries/and/includes
#    -DWINSDK_BASE=/path/to/windows-sdk
#    -DWINSDK_VER=windows sdk version folder name
#
# VALI_ARCH:
#    The architecture to build for.
#

# Sanitize expected environmental variables
if(NOT DEFINED ENV{CROSS})
    message(FATAL_ERROR "Please set the CROSS environmental variable to the path of the Vali Crosscompiler.")
endif()

if(NOT DEFINED ENV{VALI_ARCH})
    message(STATUS "VALI_ARCH environmental variable was not set, defauling to amd64.")
    set(ENV{VALI_ARCH} amd64)
endif()

if(NOT DEFINED ENV{VALI_SDK_PATH})
    message(FATAL_ERROR "Please set the VALI_SDK_PATH environmental variable to the path of the Vali SDK.")
endif()

# Setup environment stuff for cmake configuration
set(CMAKE_SYSTEM_NAME valicc)
set(CMAKE_SYSTEM_VERSION 1)
set(CMAKE_C_COMPILER "$ENV{CROSS}/bin/clang" CACHE FILEPATH "")
set(CMAKE_CXX_COMPILER "$ENV{CROSS}/bin/clang++" CACHE FILEPATH "")
set(CMAKE_AR "$ENV{CROSS}/bin/llvm-ar" CACHE FILEPATH "")
set(CMAKE_RANLIB "$ENV{CROSS}/bin/llvm-ranlib" CACHE FILEPATH "")
set(VERBOSE 1)

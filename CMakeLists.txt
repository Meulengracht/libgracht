# Project setup
cmake_minimum_required (VERSION 3.13.4)
project (Gracht)
enable_language (C)

option (GRACHT_BUILD_TESTS "Build test server and client program for gracht" OFF)

include (CheckIncludeFiles)
check_include_files (threads.h HAVE_C11_THREADS)
check_include_files (pthread.h HAVE_PTHREAD)

configure_file(config.h.in config.h @ONLY)

add_subdirectory(runtime)

if (GRACHT_BUILD_TESTS)
    add_subdirectory(tests)
endif ()

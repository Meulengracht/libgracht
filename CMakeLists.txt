# Project setup
cmake_minimum_required (VERSION 3.8.2)
project (Gracht)
enable_language (C)

option (GRACHT_BUILD_TESTS "Build test server and client program for gracht" OFF)
option (GRACHT_INSTALL_HEADERS "Install headers and library")

set (WARNING_COMPILE_FLAGS "-Wall -Wextra -Wno-unused-function")
set (SRCS "")

macro (add_sources)
    file (RELATIVE_PATH _relPath "${PROJECT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")
    foreach (_src ${ARGN})
        if (_relPath)
            list (APPEND SRCS "${_relPath}/${_src}")
        else()
            list (APPEND SRCS "${_src}")
        endif()
    endforeach()
endmacro()

# Configure include paths
if (VALI_BUILD)
    include_directories (
        ${CMAKE_BINARY_DIR}
        ../libds/include
        ../libddk/include
        ../libc/include
        ../include
        ./include
    )
else ()
    include_directories(
        ${CMAKE_BINARY_DIR}
        ./include
    )
endif ()

include (CheckIncludeFiles)
check_include_files (threads.h HAVE_C11_THREADS)
check_include_files (pthread.h HAVE_PTHREAD)

if (MOLLENOS)
    add_definitions(${WARNING_COMPILE_FLAGS})
    add_sources (
            link/vali-ipc/client.c
            link/vali-ipc/message.c
            link/vali-ipc/os.c
            link/vali-ipc/server.c
    )
endif ()

if (UNIX)
    add_definitions(${WARNING_COMPILE_FLAGS})
endif ()

configure_file(config.h.in config.h @ONLY)
add_sources(link/socket/client.c link/socket/server.c link/socket/shared.c)
add_sources(client.c client_config.c crc.c server.c server_config.c shared.c arena.c dispatch.c hashtable.c control.c)

add_library(gracht ${SRCS})

if (VALI_BUILD)
    install(TARGETS gracht
        ARCHIVE DESTINATION vali-ddk/lib
        LIBRARY DESTINATION vali-ddk/lib
        RUNTIME DESTINATION vali-ddk/bin
    )
    install(DIRECTORY include/ DESTINATION vali-ddk/include)
    install(DIRECTORY generator/ DESTINATION vali-ddk/share/vali)
else ()
    install(TARGETS gracht
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin
    )
    install(DIRECTORY include/ DESTINATION include)
    install(DIRECTORY generator/ DESTINATION share/gracht)
    install(FILES protocol.xml DESTINATION share/gracht)
endif ()

if (GRACHT_BUILD_TESTS)
    add_subdirectory(tests)
endif ()

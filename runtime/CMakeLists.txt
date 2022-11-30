
option (GRACHT_C_BUILD_STATIC "Build the C runtime as a shared library" ON)
option (GRACHT_C_BUILD_SHARED "Build the C runtime as a shared library" ON)
option (GRACHT_C_LINK_SOCKET  "Build the C runtime link: socket" ON)
option (GRACHT_C_LINK_VALI    "Build the C runtime link: vali-ipc" OFF)

set (WARNING_COMPILE_FLAGS "-Wall -Wextra -Wno-unused-function")
set (SRCS "")

macro (add_sources)
    file (RELATIVE_PATH _relPath "${PROJECT_SOURCE_DIR}/runtime" "${CMAKE_CURRENT_SOURCE_DIR}")
    foreach (_src ${ARGN})
        if (_relPath)
            list (APPEND SRCS "${_relPath}/${_src}")
        else()
            list (APPEND SRCS "${_src}")
        endif()
    endforeach()
endmacro()

macro (add_vali_link_targets)
    if (VALI_BUILD)
        target_link_libraries(${ARGV0} libc-api libos-api libddk)
    else ()
        target_link_libraries(${ARGV0} ValiDDK::libddk)
    endif ()
endmacro ()

# On Vali builds we would like to also include some paths, but
# not during kernel builds, we need to get rid of this dependency at some
# point as we do not actually require this anymore
if (MOLLENOS AND NOT VALI_BUILD)
    if (NOT DEFINED ENV{VALI_SDK_PATH})
        message(FATAL_ERROR "Please set the VALI_SDK_PATH environmental variable to the path of the Vali SDK.")
    endif ()
    include($ENV{VALI_SDK_PATH}/lib/cmake/ValiDDKTargets.cmake)
endif ()

if (GRACHT_C_LINK_VALI)
    add_sources (
            link/vali-ipc/client.c
            link/vali-ipc/shared.c
            link/vali-ipc/server.c
    )
endif ()

if (GRACHT_C_LINK_SOCKET)
    add_sources(link/socket/client.c link/socket/server.c link/socket/shared.c)
endif()

if (UNIX OR MOLLENOS)
    add_definitions(${WARNING_COMPILE_FLAGS})
endif ()
add_definitions(-DGRACHT_BUILD)

# add all the generic sources that are required
add_sources(
        client.c
        client_config.c
        crc.c
        server.c
        server_config.c
        shared.c
        stack.c
        queue.c
        arena.c
        hashtable.c
        control.c
)

# determine which worker dispatch we should use, vali is using green threads
# and thus don't need a seperate system, as we can just use the builtin runtime
# system.
if (MOLLENOS)
    add_sources(dispatch_vali.c)
else ()
    add_sources(dispatch_generic.c)
endif ()

if (GRACHT_C_BUILD_STATIC)
    add_library(gracht_static STATIC ${SRCS})
    target_include_directories(
            gracht_static PUBLIC
            ${CMAKE_CURRENT_BINARY_DIR}/../
            ../include
    )
    if (MOLLENOS)
        add_vali_link_targets(gracht_static)
    endif ()
endif ()

if (GRACHT_C_BUILD_SHARED)
    add_library(gracht SHARED ${SRCS})
    target_include_directories(
            gracht PUBLIC
            ${CMAKE_CURRENT_BINARY_DIR}/../
            ../include
    )
    target_compile_definitions(gracht PUBLIC -DGRACHT_SHARED_LIBRARY)
    if (UNIX)
        target_link_libraries(gracht -lrt -lc)
        if (HAVE_PTHREAD)
            target_link_libraries(gracht -lpthread)
        endif ()
    elseif (WIN32)
        target_link_libraries(gracht ws2_32 wsock32)
    elseif (MOLLENOS)
        add_vali_link_targets(gracht)
        if (VALI_BUILD)
            target_link_libraries(gracht c librt)
            deploy_target_to_initrd(gracht)
        endif ()
    endif ()
endif ()

install(
        TARGETS gracht
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin
)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/gracht/ DESTINATION include/gracht)
install(
        DIRECTORY ${PROJECT_SOURCE_DIR}/generator/
        DESTINATION share/gracht
        FILES_MATCHING
        PATTERN "*.py"
        PATTERN "__init__.py" EXCLUDE
        PATTERN "__pycache__" EXCLUDE
)

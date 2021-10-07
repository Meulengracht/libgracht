
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

# Configure include paths
include_directories(
        ${CMAKE_CURRENT_BINARY_DIR}/../
        ../include
)

# On Vali builds we would like to also include some paths, but
# not during kernel builds, we need to get rid of this dependency at some
# point as we do not actually require this anymore
if (MOLLENOS AND NOT VALI_BUILD)
    if (NOT DEFINED ENV{VALI_DDK_PATH})
        message(FATAL_ERROR "Please set the VALI_DDK_PATH environmental variable to the path of the Vali DDK.")
    endif ()
    include($ENV{VALI_DDK_PATH}/lib/cmake/ValiDDKTargets.cmake)
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

add_sources(client.c client_config.c crc.c server.c server_config.c shared.c stack.c queue.c arena.c dispatch.c hashtable.c control.c)

if (GRACHT_C_BUILD_STATIC)
    add_library(gracht_static STATIC ${SRCS})
    if (MOLLENOS AND NOT VALI_BUILD)
        target_link_libraries(gracht_static ValiDDK::libddk)
    endif ()
endif ()

if (GRACHT_C_BUILD_SHARED)
    add_library(gracht SHARED ${SRCS})
    target_compile_definitions(gracht PUBLIC -DGRACHT_SHARED_LIBRARY)
    if (UNIX)
        target_link_libraries(gracht -lrt -lc)
        if (HAVE_PTHREAD)
            target_link_libraries(gracht -lpthread)
        endif ()
    elseif (WIN32)
        target_link_libraries(gracht ws2_32 wsock32)
    elseif (MOLLENOS)
        if (VALI_BUILD)
            target_link_libraries(gracht c)
        else ()
            target_link_libraries(gracht ValiDDK::libddk)
        endif ()
    endif ()
endif ()

if (VALI_BUILD)
    install(TARGETS gracht
        ARCHIVE DESTINATION vali-ddk/lib
        LIBRARY DESTINATION vali-ddk/lib
        RUNTIME DESTINATION vali-ddk/bin
    )
    install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/gracht/ DESTINATION vali-ddk/include/gracht)
    install(DIRECTORY ${PROJECT_SOURCE_DIR}/generator/ DESTINATION vali-ddk/share/vali)
else ()
    install(TARGETS gracht
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin
    )
    install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/gracht/ DESTINATION include/gracht)
    install(DIRECTORY ${PROJECT_SOURCE_DIR}/generator/ DESTINATION share/gracht)
endif ()

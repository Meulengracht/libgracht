
option (GRACHT_C_BUILD_STATIC "Build the C runtime as a shared library" ON)
option (GRACHT_C_BUILD_SHARED "Build the C runtime as a shared library" ON)

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
if (VALI_BUILD)
    include_directories (
        ${CMAKE_BINARY_DIR}
        ../libds/include
        ../libddk/include
        ../libc/include
        ../include
    )
else ()
    include_directories(
        ${CMAKE_BINARY_DIR}
        ../include
    )
endif ()

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

add_definitions(-DGRACHT_BUILD)

add_sources(link/socket/client.c link/socket/server.c link/socket/shared.c)
add_sources(client.c client_config.c crc.c server.c server_config.c shared.c stack.c queue.c arena.c dispatch.c hashtable.c control.c)

if (GRACHT_C_BUILD_STATIC)
    add_library(gracht_static ${SRCS})
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
    endif ()
endif ()

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

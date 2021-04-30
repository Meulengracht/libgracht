
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

add_sources(link/socket/client.c link/socket/server.c link/socket/shared.c)
add_sources(client.c client_config.c crc.c server.c server_config.c shared.c stack.c arena.c dispatch.c hashtable.c control.c)

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

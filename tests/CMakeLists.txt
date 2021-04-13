
macro (add_server_test)
    set (TEST_SOURCES "${ARGN}")
    list (POP_FRONT TEST_SOURCES) # target

    add_executable(${ARGV0} ${TEST_SOURCES} init_server_socket.c test_utils_protocol_server.c)
    add_dependencies(${ARGV0} test_protocols)
    target_link_libraries(${ARGV0} gracht)
    if (UNIX)
        target_link_libraries(${ARGV0} -lrt -lc)
        if (HAVE_PTHREAD)
            target_link_libraries(${ARGV0} -lpthread)
        endif ()
    elseif (WIN32)
        target_link_libraries(${ARGV0} ws2_32)
    endif ()
endmacro()

macro (add_client_test)
    set (TEST_SOURCES "${ARGN}")
    list (POP_FRONT TEST_SOURCES) # target

    add_executable(${ARGV0} ${TEST_SOURCES} init_client_socket.c test_utils_protocol_client.c)
    add_dependencies(${ARGV0} test_protocols)
    target_link_libraries(${ARGV0} gracht)
    if (UNIX)
        target_link_libraries(${ARGV0} -lrt -lc)
        if (HAVE_PTHREAD)
            target_link_libraries(${ARGV0} -lpthread)
        endif ()
    elseif (WIN32)
        target_link_libraries(${ARGV0} ws2_32)
    endif ()
endmacro()

include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_custom_command(
    OUTPUT  test_utils_protocol_server.c test_utils_protocol_client.c
    COMMAND python ${CMAKE_SOURCE_DIR}/generator/parser.py --protocol ${CMAKE_CURRENT_SOURCE_DIR}/test_protocol.xml --out ${CMAKE_CURRENT_BINARY_DIR} --lang-c --server --client
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/test_protocol.xml
)
add_custom_target(
    test_protocols
    DEPENDS test_utils_protocol_server.c test_utils_protocol_client.c
)

# Client test applications
add_client_test(gclient_single client/main.c)
add_client_test(gclient_many client_parallel/main.c)

# Server test applications
add_server_test(gserver server/main.c)
add_server_test(gserver_mt server_mt/main.c)
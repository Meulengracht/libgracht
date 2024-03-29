
macro (add_server_test)
    set (TEST_SOURCES "${ARGN}")
    list (POP_FRONT TEST_SOURCES) # target

    add_executable(${ARGV0} ${TEST_SOURCES} test_data.c server_handlers.c init_server_socket.c test_utils_service_server.c)
    add_dependencies(${ARGV0} test_protocols)
    if (GRACHT_C_BUILD_SHARED)
        target_compile_definitions(${ARGV0} PUBLIC -DGRACHT_SHARED_LIBRARY)
        target_link_libraries(${ARGV0} gracht)
    else ()
        target_link_libraries(${ARGV0} gracht_static)
    endif ()
    if (UNIX)
        target_link_libraries(${ARGV0} -lrt -lc)
        if (HAVE_PTHREAD)
            target_link_libraries(${ARGV0} -lpthread)
        endif ()
    elseif (WIN32)
        target_link_libraries(${ARGV0} ws2_32 wsock32)
    endif ()
endmacro()

macro (add_client_test)
    set (TEST_SOURCES "${ARGN}")
    list (POP_FRONT TEST_SOURCES) # target

    add_executable(${ARGV0} ${TEST_SOURCES} test_data.c init_client_socket.c test_utils_service_client.c)
    add_dependencies(${ARGV0} test_protocols)
    if (GRACHT_C_BUILD_SHARED)
        target_compile_definitions(${ARGV0} PUBLIC -DGRACHT_SHARED_LIBRARY)
        target_link_libraries(${ARGV0} gracht)
    else ()
        target_link_libraries(${ARGV0} gracht_static)
    endif ()
    if (UNIX)
        target_link_libraries(${ARGV0} -lrt -lc)
        if (HAVE_PTHREAD)
            target_link_libraries(${ARGV0} -lpthread)
        endif ()
    elseif (WIN32)
        target_link_libraries(${ARGV0} ws2_32 wsock32)
    endif ()
endmacro()

include_directories(${CMAKE_BINARY_DIR} ${CMAKE_CURRENT_BINARY_DIR} ../include)

add_custom_command(
    OUTPUT  test_utils_service_server.c test_utils_service_server.h test_utils_service_client.c test_utils_service_client.h test_utils_service.h
    COMMAND python3 ${CMAKE_SOURCE_DIR}/generator/parser.py --service ${CMAKE_CURRENT_SOURCE_DIR}/protocols/test_service.gr --out ${CMAKE_CURRENT_BINARY_DIR} --lang-c --server --client
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/protocols/test_service.gr
)
add_custom_target(
    test_protocols
    DEPENDS test_utils_service_server.c test_utils_service_client.c
)

configure_file(run-tests.sh ${CMAKE_BINARY_DIR}/run-tests.sh COPYONLY)

if (UNIX)
    add_definitions("-ggdb")
endif ()

# Client test applications
add_client_test(gclient_0 client/test_string.c)
add_client_test(gclient_1 client/test_structure.c)
add_client_test(gclient_2 client/test_events.c)
add_client_test(gclient_3 client/test_variable.c)
add_client_test(gclient_4 client/test_deferring.c)
add_client_test(gclient_5 client/test_multiple.c)
add_client_test(gclient_6 client/test_shutdown.c)

# Server test applications
add_server_test(gserver server/main.c)
add_server_test(gserver_mt server_mt/main.c)
/** Check connection and sender credentials using real Linux sockets. */
#define _GNU_SOURCE
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <gracht/server.h>
#include <gracht/link/socket.h>
#include <utils.h>
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/un.h>
#include <sys/wait.h>

static int accepted;
static unsigned invocations;
static pid_t expectedPid;
static int packetMode;
static struct gracht_message* deferred;
static struct gracht_security_context original;

static void
__Connected(gracht_conn_t client)
{
    accepted = client;
}

static void
__Invoke(struct gracht_message* message, struct gracht_buffer* buffer)
{
    const struct gracht_security_context* security;
    (void)buffer;

    security = gracht_server_get_security_context(message);
    if (expectedPid == 0) {
        assert(security == NULL);
    } else {
        assert(security && security->source == GRACHT_SECURITY_KERNEL);
        assert(security->pid == expectedPid);
        assert(security->uid == (packetMode ? getuid() : geteuid()));
        assert(security->gid == (packetMode ? getgid() : getegid()));
        assert(security->capture == (packetMode ? GRACHT_SECURITY_CAPTURE_MESSAGE :
            GRACHT_SECURITY_CAPTURE_CONNECTION));
        if (!deferred) {
            deferred = malloc(GRACHT_MESSAGE_DEFERRABLE_SIZE(message));
            assert(deferred);
            gracht_server_defer_message(message, deferred);
            original = *security;
        }
    }
    invocations++;
}

static socklen_t
__UnixAddress(struct sockaddr_storage* storage, const char* name)
{
    struct sockaddr_un* address = (struct sockaddr_un*)storage;
    int length;

    memset(storage, 0, sizeof(*storage));
    address->sun_family = AF_UNIX;
    length = snprintf(address->sun_path + 1, sizeof(address->sun_path) - 1,
        "gracht-security-%ld-%s", (long)getpid(), name);
    assert(length > 0 && (size_t)length < sizeof(address->sun_path) - 1);
    return (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + length);
}

static int
__AddLink(gracht_server_t* server, int domain, struct sockaddr_storage* address, socklen_t* length)
{
    struct gracht_link_socket* link;
    struct sockaddr_in* internet = (struct sockaddr_in*)address;
    int handle;

    if (domain == AF_UNIX) {
        *length = __UnixAddress(address, "server");
    } else {
        memset(address, 0, sizeof(*address));
        internet->sin_family = AF_INET;
        internet->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        *length = sizeof(*internet);
    }
    assert(gracht_link_socket_create(&link) == 0);
    gracht_link_socket_set_domain(link, domain);
    gracht_link_socket_set_type(link, packetMode ? gracht_link_packet_based : gracht_link_stream_based);
    gracht_link_socket_set_listen(link, 1);
    gracht_link_socket_set_bind_address(link, address, *length);
    assert(gracht_server_add_link(server, (struct gracht_link*)link) == 0);
    handle = gracht_link_get_handle((struct gracht_link*)link);
    *length = sizeof(*address);
    assert(getsockname(handle, (struct sockaddr*)address, length) == 0);
    return handle;
}

static int
__OpenSender(int domain, const struct sockaddr_storage* destination, socklen_t length)
{
    struct sockaddr_storage local;
    socklen_t localLength;
    int descriptor;

    descriptor = socket(domain, packetMode ? SOCK_DGRAM : SOCK_STREAM, 0);
    assert(descriptor >= 0);
    if (domain == AF_UNIX && packetMode) {
        localLength = __UnixAddress(&local, "client");
        assert(bind(descriptor, (struct sockaddr*)&local, localLength) == 0);
    }
    assert(connect(descriptor, (const struct sockaddr*)destination, length) == 0);
    return descriptor;
}

static void
__WireMessage(unsigned char* bytes, size_t length)
{
    memset(bytes, 0, length);
    __gracht_write_u32(bytes + MSG_INDEX_LEN, GRACHT_MESSAGE_HEADER_SIZE);
    bytes[MSG_INDEX_SID] = 61;
    bytes[MSG_INDEX_AID] = 1;
    bytes[MSG_INDEX_FLG] = MESSAGE_FLAG_ASYNC;
}

static void
__Send(int descriptor)
{
    unsigned char bytes[GRACHT_MESSAGE_HEADER_SIZE];

    __WireMessage(bytes, sizeof(bytes));
    assert(send(descriptor, bytes, sizeof(bytes), 0) == sizeof(bytes));
}

static unsigned
__DescriptorCount(void)
{
    DIR* directory = opendir("/proc/self/fd");
    unsigned count = 0;

    assert(directory);
    while (readdir(directory)) {
        count++;
    }
    closedir(directory);
    return count;
}

static void
__SendDescriptors(int descriptor)
{
    unsigned char bytes[GRACHT_MESSAGE_HEADER_SIZE];
    int descriptors[8];
    union {
        struct cmsghdr alignment;
        unsigned char bytes[CMSG_SPACE(sizeof(descriptors))];
    } control = {0};
    struct iovec data = {.iov_base = bytes, .iov_len = sizeof(bytes)};
    struct msghdr message = {
        .msg_iov = &data, .msg_iovlen = 1,
        .msg_control = control.bytes, .msg_controllen = sizeof(control.bytes)
    };
    struct cmsghdr* header = CMSG_FIRSTHDR(&message);
    unsigned i;

    __WireMessage(bytes, sizeof(bytes));
    for (i = 0; i < 8; ++i) {
        descriptors[i] = descriptor;
    }
    header->cmsg_level = SOL_SOCKET;
    header->cmsg_type = SCM_RIGHTS;
    header->cmsg_len = CMSG_LEN(sizeof(descriptors));
    memcpy(CMSG_DATA(header), descriptors, sizeof(descriptors));
    assert(sendmsg(descriptor, &message, 0) == sizeof(bytes));
}

static void
__RejectedDatagrams(gracht_server_t* server, int receiver, int sender)
{
    unsigned calls = invocations;
    unsigned descriptors = __DescriptorCount();
    unsigned char oversized[8192];
    int enabled = 0;

    // Reject a truncated datagram even if its header looks valid.
    __WireMessage(oversized, sizeof(oversized));
    assert(send(sender, oversized, sizeof(oversized), 0) == sizeof(oversized));
    assert(gracht_server_handle_event(server, receiver, 0) == -1 && errno == EMSGSIZE);
    __SendDescriptors(sender);
    assert(gracht_server_handle_event(server, receiver, 0) == -1 && errno == EMSGSIZE);
    assert(__DescriptorCount() == descriptors);

    // Requests must be rejected when SO_PASSCRED is disabled after setup.
    assert(setsockopt(receiver, SOL_SOCKET, SO_PASSCRED, &enabled, sizeof(enabled)) == 0);
    __Send(sender);
    assert(gracht_server_handle_event(server, receiver, 0) == -1 && errno == EPROTO);
    __SendDescriptors(sender);
    assert(gracht_server_handle_event(server, receiver, 0) == -1 && errno == EMSGSIZE);
    assert(__DescriptorCount() == descriptors);
    assert(invocations == calls);
    enabled = 1;
    assert(setsockopt(receiver, SOL_SOCKET, SO_PASSCRED, &enabled, sizeof(enabled)) == 0);
    expectedPid = getpid();
    __Send(sender);
    assert(gracht_server_handle_event(server, receiver, 0) == 0);
    assert(invocations == calls + 1);
}

int
main(int argc, char** argv)
{
    gracht_server_configuration_t configuration;
    gracht_server_t* server;
    gracht_protocol_function_t functions[] = {{1, (void*)__Invoke}};
    gracht_protocol_t protocol = GRACHT_PROTOCOL_INIT(61, "socket-security", 1, functions);
    struct sockaddr_storage address;
    socklen_t length;
    const struct gracht_security_context* security;
    int receiver;
    int sender;
    int handle;
    int status;
    unsigned calls;
    pid_t child;

    assert(argc >= 2);
    packetMode = strcmp(argv[1], "datagram") == 0;
    if (argc > 2) {
        protocol.flags = GRACHT_PROTOCOL_FLAG_STREAM;
    }
    gracht_server_configuration_init(&configuration);
    gracht_server_configuration_set_num_workers(&configuration, 0);
    configuration.callbacks.clientConnected = __Connected;
    assert(gracht_server_create(&configuration, &server) == 0);
    assert(gracht_server_register_protocol(server, &protocol) == 0);
    receiver = __AddLink(server, AF_UNIX, &address, &length);
    sender = __OpenSender(AF_UNIX, &address, length);
    if (!packetMode) {
        assert(gracht_server_handle_event(server, receiver, 0) == 0);
    }
    handle = packetMode ? receiver : accepted;
    expectedPid = getpid();
    __Send(sender);
    assert(gracht_server_handle_event(server, handle, 0) == 0);
    assert(invocations == 1);

    // Have a child write through the same socket. Streams should still identify
    // the parent that connected; datagrams should identify the child that sent.
    child = fork();
    assert(child >= 0);
    if (child == 0) {
        __Send(sender);
        _exit(0);
    }
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    expectedPid = packetMode ? child : getpid();
    assert(gracht_server_handle_event(server, handle, 0) == 0);
    assert(invocations == 2);
    if (packetMode) {
        __RejectedDatagrams(server, receiver, sender);
    }
    close(sender);

    // An Internet request must not inherit credentials from the reused buffer.
    receiver = __AddLink(server, AF_INET, &address, &length);
    sender = __OpenSender(AF_INET, &address, length);
    if (!packetMode) {
        assert(gracht_server_handle_event(server, receiver, 0) == 0);
    }
    handle = packetMode ? receiver : accepted;
    expectedPid = 0;
    calls = invocations;
    __Send(sender);
    assert(gracht_server_handle_event(server, handle, 0) == 0);
    assert(invocations == calls + 1);
    close(sender);
    gracht_server_request_shutdown(server);
    assert(gracht_server_handle_event(server, receiver, 0) == -1);

    security = gracht_server_get_security_context(deferred);
    assert(security && security->source == original.source && security->capture == original.capture);
    assert(security->pid == original.pid && security->uid == original.uid && security->gid == original.gid);
    free(deferred);
    return 0;
}

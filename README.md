# libgracht
A lightweight, cross-platform, low-dependency, and modular protocol/rpc library. Gracht is built upon an abstracted concept of links, which can be anything and hence allows gracht to work on any platform. The links that are provided by default are unix/windows-sockets and a native interface for the Vali OS IPC (which the library is originally built for). Gracht protocols are defined in the in its own format, where we support different kinds of primitives:
 - Types
 - Enums
 - Structures (nested, variable)
 - Synchronous function calls
 - Asynchronous function calls
 - Asynchronous events with parameters

The library itself supports different kind of ways to send/receive messages. This can be done individually or in bulk. Multiple messages can be invoked and awaited with a single call.

Links are defined in include/gracht/links and are seperate objects that must be created before the client or server gets created/initialized. If you want to implement your own link interface you can take a look at the required functions under /include/gracht/link/link.h.

The library itself relies on very little functionality from the OS itself to work.

Required operating system functionality:
 - Epoll/completion port-like interface. (aio.h)
 - Threading model with conditions and mutexes. (thread_api.h)

Supported links:
 - Socket   (link/socket/*)
 - Vali-IPC (link/vali-ipc/*)

Supported languages for code generation are:
 - C

Supported platforms:
 - Windows
 - Linux
 - Vali/MollenOS

## Protocol Format

```
/**
 * Test example of gracht protocol message version 2
 * Includes example of a test protocol and some test structures
 */

namespace test

struct payment {
    uint32 id;
    int    amount;
}

struct owner_business {
    uint32 id;
    string name;
}

struct owner_person {
    uint32 id;
    string name;
}

struct account_owner {
    variant type {
        owner_business b;
        owner_person p;
    }
}

struct account {
    string        name;
    uint32        id;
    account_owner owner;
    int           balance;
    payment[]     payments;
}

struct transaction {
    uint32  test_id;
    string  serial;
    uint8[] data;
}

struct transfer_status {
    uint32 test_id;
    int    code;
}

service utils : message {
    func print(string text) : (int result) = 1;
    func transfer(transaction transaction) : (transfer_status result) = 2;
    func transfer_many(transaction[] transactions) : (transfer_status[] results) = 3;
    func transfer_data(uint8[] data) : () = 4;
    func receive_data() : (uint8[] data) = 5;
    func receive_string() : (string text) = 6;
    func get_event(int count) : () = 7;
    func shutdown() : () = 8;

    func get_account(string name) : (account result) = 9;
    func add_payment(account account, payment payment) : (int result) = 10;

    event myevent : (int n) = 11;
    event transfer_status : transfer_status = 12;
}

service small_upload : stream {
    option direction = to_server;
    option mode = bounded;
    option chunk_size = 64;
}

service large_download : stream {
    option direction = to_client;
    option mode = bounded;
    option chunk_size = 4K;
}
```

## Protocol generator
The protocol generator is located in /generator/ folder and can be used to generate headers and implementation files. Three header files can be generated
and two implementation files can be generated per protocol.

The header files generated are:
namespace_svcname_service.h
namespace_svcname_service_server.h
namespace_svcname_service_client.h

The source file generated is:
namespace_svcname_service_server.c
namespace_svcname_service_client.c

In the header files are instructions for how to declare the protocols in your files and documentation for how to setup the protocol callbacks (if any).

```
usage: parser.py

arguments:
-h, --help            show this help message and exit
--service servicefile The service file that should be parsed
--include INCLUDE     The services that should be generated from the file, comma-seperated list, default is all
--out OUT             Protocol files output directory
--client              Generate client side files
--server              Generate server side files
--lang-c              Generate c-language headers and implementation files
```

## Examples

Examples for libgracht are located under tests/ directory and show minimal implementations for using the client and server in combination with the socket link.

## Security contexts for received messages

`gracht_server_get_security_context(message)` returns credentials authenticated by
the receiving link, or `NULL` if the link supplies none. The context is stored
directly in the message, with fields that depend on the platform. Links fill it
from authenticated credentials, never from RPC arguments or return addresses.

| Platform | Representation | Current support |
| --- | --- | --- |
| Vali | Source, boot-local principal, Vali capability mask | Kernel credentials captured when the message is sent. |
| Linux Unix stream | Source, capture, PID, UID, GID | `SO_PEERCRED`: the connecting peer's credentials, read once at accept. |
| Linux Unix datagram | Source, capture, PID, UID, GID | `SO_PASSCRED` / `SCM_CREDENTIALS`: kernel-validated credentials attached to each datagram. |
| Other POSIX links | Source, capture, PID, UID, GID | No credentials collected, including on Internet socket links. |
| Windows | Reserved storage | The accessor always returns `NULL`. |

On Vali and POSIX, `GRACHT_SECURITY_KERNEL` means the kernel supplied or checked
the credentials. The POSIX `capture` field describes when they were captured:

- `GRACHT_SECURITY_CAPTURE_CONNECTION`: the original connecting peer's PID and
  effective UID/GID at connection time. They stay the same if another process
  inherits or receives the socket, or the peer changes credentials.
- `GRACHT_SECURITY_CAPTURE_MESSAGE`: credentials accompanying this datagram.
  Linux supplies the sender's PID and real UID/GID by default. A sender can also
  supply credentials explicitly. The kernel checks them under Linux's rules,
  which allow privileged processes to substitute some values. These credentials
  do not include a Linux capability mask.
- `GRACHT_SECURITY_CAPTURE_UNSPECIFIED`: the link does not specify when it
  captured credentials. Check `source` or call the accessor to find out whether
  credentials are present.

UID zero is valid. IDs use the receiver's namespaces, and PIDs can be reused.
The accessor does not promise the sender's identity at send time for
every transport.
See the [Linux Unix socket documentation](https://man7.org/linux/man-pages/man7/unix.7.html).

Linux Unix links collect credentials automatically. If enabling collection fails,
link setup fails. If reading peer credentials fails, the connection is rejected.
Datagrams are rejected before dispatch if their credentials are missing, invalid
or truncated, or if their payload is truncated. Descriptor passing is unsupported;
the receiver closes any received descriptors and rejects the datagram. Datagram
senders still need a return address for replies. TCP/UDP addresses and ports are
not treated as authenticated identities.

The returned pointer refers to storage inside the message. To defer a message,
provide `GRACHT_MESSAGE_DEFERRABLE_SIZE(message)` bytes and call
`gracht_server_defer_message`. This copies the credentials by value, so they
survive reuse of the receive buffer. They need no separate allocation or cleanup;
free or reuse your deferred storage when finished. Deferral keeps the original
credentials. It does not refresh them or keep the server or connection alive.

The message header stays local and is not part of the RPC wire format. Its layout
can differ between platforms, so code that reads credential fields must handle
the relevant platform. Remote identities that do not fit these OS credentials
would need a separate representation.

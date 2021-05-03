# libgracht
A lightweight, cross-platform, low-dependency, and modular protocol/rpc library. Gracht is built upon an abstracted concept of links, which can be anything and hence allows gracht to work on any platform. The links that are provided by default are unix/windows-sockets and a native interface for the Vali OS IPC (which the library is originally built for). Gracht protocols are defined in the in its own format, where we support different kinds of primitives:
 - Datatypes
 - Enums
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

## Protocol Format

```
/**
 * Test example of gracht protocol message version 2
 * Includes example of a test protocol and some test structures
 */

import "shared_types" // imports file shared_types.gr
namespace test

define uint32_t from "stdint.h"

enum error_codes {
    ok = 0,
    invalid_parameters = -1,
    invalid_result = -2
}

struct transfer_device {
    string device;
}

struct transfer_bit {
    int start;
    int length;
}

struct transfer_request {
    transfer_device device;
    transfer_bit[] bits;
}

struct transfer_complete_event {
    uint32_t id;
}

service disk (1) {
    func transfer(transfer_request request) : (int status) = 1;
    func transfer_many(transfer_request[] request) : (int[] statuses) = 2;
    event transfer_complete : transfer_complete_event = 3;
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
--lang-c              Generate c-style headers and implementation files
```

## Examples

Examples for libgracht are located under tests/ directory and show minimal implementations for using the client and server in combination with the socket link.

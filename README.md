# libgracht
A lightweight, cross-platform, modular and dependency free protocol library. Gracht is built upon an abstracted concept of links, which can be anything and hence allows gracht to work on any platform. The links that are provided by default are unix/windows-sockets and a native interface for the Vali OS IPC (which the library is originally built for). Gracht protcols are defined in the XML format, where we support different kinds of primitives:
 - Datatypes
 - Enums
 - Synchronous function calls
 - Asynchronous function calls
 - Asynchronous events with parameters

The library itself supports different kind of ways to send/receive messages. This can be done individually or in bulk. Multiple messages can be invoked and awaited with a single call.

Links are defined in include/gracht/links and are seperate objects that must be created before the client or server gets created/initialized. If you want to implement your own link interface you can take a look at the required functions under /include/gracht/link/link.h.

Supported links:
 - Socket

Supported languages for code generation are:
 - C

Libraries the core library depends on:
 - C11 threads or pthreads (linux only)

## Protocol Format

```
<?xml version="1.0" encoding="UTF-8" standalone="no" ?>
<root>
    <types>
        <type name="uint8_t" header="stdint.h" />
    </types>
    <protocols namespace="sys">
        <protocol name="utils" id="0xF0">
            <enums>
                <enum name="error_kinds">
                    <value name="error_zero" value="0x0" />
                    <value name="error_one" value="0x0" />
                </enum>
            </enums>
            
            <functions>
                <function name="print">
                    <request>
                        <param name="message" type="string" />
                    </request>
                    <response>
                        <param name="status" type="int" />
                    </response>
                </function>
            </functions>
            
            <events>
                <event name="error">
                    <param name="kind" type="error_kinds" />
                </event>
            </events>
        </protocol>
    </protocols>
</root>
```

## Protocol generator
The protocol generator is located in /generator/ folder and can be used to generate headers and implementation files. Three header files can be generated
and two implementation files can be generated per protocol.

The header files generated are:
sys_utils_protocol.h
sys_utils_protocol_server.h
sys_utils_protocol_client.h

The source file generated is:
sys_utils_protocol_server.c
sys_utils_protocol_client.c

In the header files are instructions for how to declare the protocols in your files and documentation for how to setup the protocol callbacks (if any).

```
usage: parser.py

arguments:
-h, --help           show this help message and exit
--protocol PROTOCOL  The protocol that should be parsed
--include INCLUDE    The protocols that should be generated from the file, comma-seperated list, default is all
--out OUT            Protocol files output directory
--client             Generate client side files
--server             Generate server side files
--lang-c             Generate c-style headers and implementation files
```

## Examples

Examples for libgracht are located under tests/ directory and show minimal implementations for using the client and server in combination with the socket link.

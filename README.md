# libgracht
A lightweight, cross-platform, modular and dependency free protocol library. Gracht is built upon an abstracted concept of links, which can be anything and hence allows gracht to work on any platform. The links that are provided by default are unix/windows-sockets and a native interface for the Vali OS IPC (which the library is originally built for). Gracht protcols are defined in the XML format, where we support different kinds of primitives:
 - Datatypes
 - Enums
 - Synchronous function calls
 - Asynchronous function calls
 - Asynchronous events with parameters

The library itself supports different kind of ways to send/receive messages. This can be done individually or in bulk. Multiple messages can be invoked and awaited with a single call.

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

## Client Interface

TODO: Cleanup inteface

## Server Inteface

TODO: Cleanup inteface

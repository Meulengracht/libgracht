from common.shared import *


def create_param(typename, name, is_variable=False):
    return VariableObject(typename, name, is_variable)


def synthesize_stream_functions(service: ServiceObject):
    direction = service.get_option("direction")
    mode = service.get_option("mode")
    functions = []
    events = []

    if mode == "bounded":
        if direction == "to_server":
            functions = [
                FunctionObject("open", 1, [create_param("string", "resource_id"), create_param("uint", "size")],
                               [create_param("string", "session_id")]),
                FunctionObject("write_chunk", 2, [create_param("string", "session_id"), create_param("uint", "index"),
                                                   create_param("uint8", "data", True)], []),
                FunctionObject("finish", 3, [create_param("string", "session_id")], []),
                FunctionObject("close", 4, [create_param("string", "session_id")], [])
            ]
        elif direction == "to_client":
            functions = [
                FunctionObject("open", 1, [create_param("string", "resource_id")],
                               [create_param("string", "session_id"), create_param("uint", "size")]),
                FunctionObject("read_chunk", 2, [create_param("string", "session_id"), create_param("uint", "index")],
                               [create_param("uint8", "data", True)]),
                FunctionObject("close", 3, [create_param("string", "session_id")], [])
            ]
    elif mode == "live":
        if direction == "to_server":
            functions = [
                FunctionObject("open", 1, [], [create_param("string", "session_id")]),
                FunctionObject("write_chunk", 2, [create_param("string", "session_id"), create_param("uint8", "data", True)], []),
                FunctionObject("pause", 3, [create_param("string", "session_id")], []),
                FunctionObject("resume", 4, [create_param("string", "session_id")], []),
                FunctionObject("close", 5, [create_param("string", "session_id")], [])
            ]
        elif direction == "to_client":
            functions = [
                FunctionObject("open", 1, [create_param("string", "resource_id")],
                               [create_param("string", "session_id")]),
                FunctionObject("pause", 2, [create_param("string", "session_id")], []),
                FunctionObject("resume", 3, [create_param("string", "session_id")], []),
                FunctionObject("close", 4, [create_param("string", "session_id")], [])
            ]
            events = [
                EventObject("chunk", 5, [create_param("string", "session_id"), create_param("uint8", "data", True)])
            ]

    service.set_functions(functions)
    service.set_events(events)


def pass_synthesize(service: ServiceObject):
    if service.is_stream():
        synthesize_stream_functions(service)
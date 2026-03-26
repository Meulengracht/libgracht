
from common.shared import *
import zlib


def derive_service_id(service: ServiceObject):
    if service.get_namespace().lower() == "gracht" and service.get_name().lower() == "control":
        return 0

    basis = f"{service.get_namespace()}{service.get_name()}".encode("utf-8")
    return (zlib.crc32(basis) % 255) + 1


def validate_stream_options(service: ServiceObject):
    options = service.get_options()
    direction = options.get("direction")
    mode = options.get("mode")
    chunk_size = options.get("chunk_size")

    valid_directions = {"to_server", "to_client"}
    valid_modes = {"bounded", "live"}
    valid_keys = {"direction", "mode", "chunk_size"}

    unknown_options = set(options.keys()) - valid_keys
    if unknown_options:
        raise ValueError(f"Unknown stream option(s) for service {service.get_name()}: {', '.join(sorted(unknown_options))}")
    if direction not in valid_directions:
        raise ValueError(f"Stream service {service.get_name()} must declare option direction = to_server|to_client")
    if mode not in valid_modes:
        raise ValueError(f"Stream service {service.get_name()} must declare option mode = bounded|live")
    if chunk_size is None or not isinstance(chunk_size, int) or chunk_size <= 0:
        raise ValueError(f"Stream service {service.get_name()} must declare a positive numeric chunk_size")


def validate_service(service: ServiceObject):
    if service.is_stream():
        validate_stream_options(service)

def resolve_type(service: ServiceObject, service_imports: list, param):
    if isinstance(param, VariableVariantObject):
        for entry in param.get_entries():
            service_imports = resolve_type(service, service_imports, entry)
        return service_imports

    if service.typename_is_builtin(param.get_typename()):
        return service_imports
    if service.typename_is_enum(param.get_typename()):
        return service_imports
    if service.typename_is_struct(param.get_typename()):
        return service_imports

    value_type = [x for x in service.get_types() if x.get_type_name().lower() == param.get_typename().lower()]
    if len(value_type):
        service_imports.append(value_type[0].get_type_source())
    else:
        raise ValueError(f"Type {param.get_typename()} cannot be resolved")
    return service_imports

def resolve_all_types(service: ServiceObject):
    service_imports = service.get_imports()
    for func in service.get_functions():
        for param in func.get_request_params():
            service_imports = resolve_type(service, service_imports, param)
        for param in func.get_response_params():
            service_imports = resolve_type(service, service_imports, param)
    for evt in service.get_events():
        for param in evt.get_params():
            service_imports = resolve_type(service, service_imports, param)
    for struct in service.get_structs():
        for member in struct.get_members():
            service_imports = resolve_type(service, service_imports, member)

    unique_imports = set(service_imports)
    service.set_imports(list(unique_imports))
    return

def pass_validate(services):
    service_ids = {}
    for service in services:
        validate_service(service)

        if service.get_id() is None:
            service.set_id(derive_service_id(service))

        service_id = service.get_id()
        if service_id < 0 or service_id > 255:
            raise ValueError(f"The id of service {service.get_name()} must be in range of 0..255")
        if service_id in service_ids:
            other = service_ids[service_id]
            raise ValueError(f"The id of service {service.get_name()} ({service_id}) collides with service {other}")
        service_ids[service_id] = service.get_name()

    for service in services:
        pass_validate_service(service)


def pass_validate_service(service: ServiceObject):
    if service.get_id() < 1 or service.get_id() > 255:
        if not (service.get_namespace().lower() == "gracht" and service.get_name().lower() == "control" and service.get_id() == 0):
            raise ValueError(f"The id of service {service.get_name()} must be in range of 1..255")

    # we want to validate the ids of our functions and events that
    # they are in range of 1-255 and that there are no conflicts
    ids_parsed = []
    for func in service.get_functions():
        if func.get_id() in ids_parsed:
            raise ValueError(f"The id of function {func.get_name()} ({func.get_id()}) is already in use")
        if func.get_id() < 1 or func.get_id() > 255:
            raise ValueError(f"The id of function {func.get_name()} must be in range of 1..255")
        ids_parsed.append(func.get_id())
    for evt in service.get_events():
        if evt.get_id() in ids_parsed:
            raise ValueError(f"The id of event {evt.get_name()} ({evt.get_id()}) is already in use")
        if evt.get_id() < 1 or evt.get_id() > 255:
            raise ValueError(f"The id of event {evt.get_name()} must be in range of 1..255")
        ids_parsed.append(evt.get_id())

    # also resolve all external types to gather a list of imports or usings
    resolve_all_types(service)

import os

from common.shared import *


class CONST(object):
    __slots__ = ()
    TYPENAME_CASE_SIZEOF = 0
    TYPENAME_CASE_FUNCTION_CALL = 1
    TYPENAME_CASE_FUNCTION_STATUS = 2
    TYPENAME_CASE_FUNCTION_RESPONSE = 3
    TYPENAME_CASE_MEMBER = 4

class CodeWriter(object):
    def __init__(self, outfile):
        self.outfile = outfile
        self.indent = 0
        return

    def append(self, text):
        self.outfile.write(text)
        return

    def write(self, text):
        self.outfile.write(" " * self.indent + text)
        return

    def writeln(self, text):
        self.outfile.write(" " * self.indent + text + "\n")
        return

    def indent_inc(self):
        self.indent += 4
        return

    def indent_dec(self):
        self.indent -= 4
        return

    def indent_reset(self):
        self.indent = 0
        return

def get_c_typename(service: ServiceObject, typename:  str):
    system_types = [
        ["uint8", "uint8_t"],
        ["int8", "int8_t"],
        ["uint16", "uint16_t"],
        ["int16", "int16_t"],
        ["uint32", "uint32_t"],
        ["int32", "int32_t"],
        ["uint64", "uint64_t"],
        ["int64", "int64_t"],
        ["long", "long"],
        ["ulong", "size_t"],
        ["uint", "unsigned int"],
        ["int", "int"],
        ["string", "char*"],
        ["bool", "uint8_t"],
        ["float", "float"],
        ["double", "double"]
    ]
    for systype in system_types:
        if systype[0] == typename.lower():
            return systype[1]
    if service.typename_is_struct(typename):
        return get_scoped_typename(service.lookup_struct(typename))
    elif service.typename_is_enum(typename):
        return get_scoped_typename(service.lookup_enum(typename))
    return typename


def is_variable_value_type(service, param):
    if service.typename_is_struct(param.get_typename()):
        return False
    if param.get_is_variable():
        return False
    return True


def should_define_parameter(param: VariableObject, case, is_output):
    is_hidden = param.get_fixed() and param.get_default_value() is not None

    if case == CONST.TYPENAME_CASE_SIZEOF or case == CONST.TYPENAME_CASE_MEMBER:
        return not is_output
    elif case == CONST.TYPENAME_CASE_FUNCTION_CALL:
        return not is_output and not is_hidden
    elif case == CONST.TYPENAME_CASE_FUNCTION_STATUS:
        return is_output
    elif case == CONST.TYPENAME_CASE_FUNCTION_RESPONSE:
        return is_output
    return False


def should_define_param_length_component(param, case):
    if case == CONST.TYPENAME_CASE_SIZEOF or case == CONST.TYPENAME_CASE_MEMBER:
        return False
    elif case == CONST.TYPENAME_CASE_FUNCTION_CALL:
        return param.get_is_variable()
    elif case == CONST.TYPENAME_CASE_FUNCTION_STATUS:
        return param.get_is_variable() or param.get_typename().lower() == "string"
    elif case == CONST.TYPENAME_CASE_FUNCTION_RESPONSE:
        return param.get_is_variable()
    return False


def get_scoped_name(namespaced_type):
    return namespaced_type.get_namespace().lower() + "_" + namespaced_type.get_name().lower()


def get_scoped_typename(namespaced_type):
    if isinstance(namespaced_type, StructureObject):
        return f"struct {get_scoped_name(namespaced_type)}"
    elif isinstance(namespaced_type, EnumObject):
        return f"enum {get_scoped_name(namespaced_type)}"


def get_message_flags_func(func):
    if len(func.get_response_params()) == 0:
        return "MESSAGE_FLAG_ASYNC"
    return "MESSAGE_FLAG_SYNC"


def define_headers(headers, outfile: CodeWriter):
    for header in headers:
        outfile.writeln(f"#include {header}")
    outfile.writeln("")
    return


def include_shared_header(service: ServiceObject, outfile: CodeWriter):
    outfile.writeln("#include \"" + service.get_namespace() + "_" + service.get_name() + "_service.h\"")
    return


def define_service_headers(service: ServiceObject, outfile: CodeWriter):
    for imp in service.get_imports():
        outfile.writeln("#include <" + imp + ">")
    outfile.writeln("")
    return


def is_param_const(param, is_output):
    if is_output:
        return False

    # skip reserved names
    if "gracht_message_context" in param.get_typename():
        return False
    if "gracht_client_t" in param.get_typename():
        return False
    if "gracht_server_t" in param.get_typename():
        return False
    if "gracht_message" in param.get_typename():
        return False
    return True


def get_param_typename(service: ServiceObject, param, case, is_output):
    # resolve enums and structs
    param_typename = get_c_typename(service, param.get_typename())
    if service.typename_is_enum(param_typename):
        param_typename = get_scoped_typename(service.lookup_enum(param_typename))
    elif service.typename_is_struct(param_typename):
        param_typename = get_scoped_typename(service.lookup_struct(param_typename))

    # format parameter, unfortunately there are 5 cases to do this
    # TYPENAME_CASE_SIZEOF means we would like the typename for a sizeof call
    if case == CONST.TYPENAME_CASE_SIZEOF:
        return param_typename

    # TYPENAME_CASE_FUNCTION_CALL is used for event prototypes and normal
    # async function prototypes
    elif case == CONST.TYPENAME_CASE_FUNCTION_CALL or case == CONST.TYPENAME_CASE_FUNCTION_STATUS:
        param_name = param.get_name()
        if param.get_is_variable() or service.typename_is_struct(param.get_typename().lower()):
            param_typename = param_typename + "*"

        if is_output:
            if "*" not in param_typename:
                param_typename = param_typename + "*"
            param_name = param_name + "_out"

        if is_param_const(param, is_output):
            param_typename = "const " + param_typename

        param_typename = param_typename + " " + param_name
        return param_typename

    elif case == CONST.TYPENAME_CASE_FUNCTION_RESPONSE:
        if param.get_is_variable() or service.typename_is_struct(param.get_typename().lower()):
            param_typename = param_typename + "*"
        param_typename = param_typename + " " + param.get_name()
        return param_typename

    elif case == CONST.TYPENAME_CASE_MEMBER:
        if param.get_is_variable():
            param_typename = param_typename + "*"
        param_typename = param_typename + " " + param.get_name()
        return param_typename
    return param_typename


def get_parameter_string(service: ServiceObject, params, case, is_output):
    parameters_valid = []
    for param in params:
        if should_define_parameter(param, case, is_output):
            parameters_valid.append(get_param_typename(service, param, case, is_output))
        if should_define_param_length_component(param, case):
            if not param.get_is_variable() and param.get_typename().lower() == "string":
                length_param = VariableObject("uint32", f"{param.get_name()}_max_length", False)
            else:
                length_param = VariableObject("uint32", f"{param.get_name()}_count", False)
            parameters_valid.append(get_param_typename(service, length_param, case, False))

    return ", ".join(parameters_valid)


def get_server_service_response_name(service: ServiceObject, func):
    return service.get_namespace() + "_" + service.get_name() + "_" + func.get_name() + "_response"


def get_service_callback_name(service: ServiceObject, cb):
    return service.get_namespace() + "_" + service.get_name() + "_" + cb.get_name() + "_invocation"


def get_client_event_callback_name(service: ServiceObject, evt):
    return service.get_namespace() + "_" + service.get_name() + "_event_" + evt.get_name() + "_invocation"


def get_service_internal_callback_name(service: ServiceObject, act):
    return f"__{service.get_namespace()}_{service.get_name()}_{act.get_name()}_internal"


def get_event_prototype_name_single(service: ServiceObject, evt, case):
    evt_server_param = get_param_typename(service, VariableObject("gracht_server_t*", "server", False), case, False)
    evt_client_param = get_param_typename(service, VariableObject("gracht_conn_t", "client", False), case, False)
    evt_name = "int " + service.get_namespace() + "_" + service.get_name() + "_event_" + evt.get_name() + "_single("
    evt_name += evt_server_param + ", " + evt_client_param

    if len(evt.get_params()) > 0:
        evt_name += ", " + get_parameter_string(service, evt.get_params(), case, False)
    return evt_name + ")"


def get_event_prototype_name_all(service: ServiceObject, evt, case):
    evt_server_param = get_param_typename(service, VariableObject("gracht_server_t*", "server", False), case, False)
    evt_name = "int " + service.get_namespace() + "_" + service.get_name() + "_event_" + evt.get_name() + "_all("
    evt_name += evt_server_param

    if len(evt.get_params()) > 0:
        evt_name += ", " + get_parameter_string(service, evt.get_params(), case, False)
    return evt_name + ")"


def write_header(outfile: CodeWriter):
    outfile.writeln("/**")
    outfile.writeln(
        "* This file was generated by the gracht service generator script. Any changes done here will be "
        "overwritten.")
    outfile.writeln(" */")
    outfile.writeln("")
    return


def write_header_guard_start(file_name, outfile: CodeWriter):
    outfile.writeln("#ifndef __" + str.replace(file_name, ".", "_").upper() + "__")
    outfile.writeln("#define __" + str.replace(file_name, ".", "_").upper() + "__")
    outfile.writeln("")
    return


def write_header_guard_end(file_name, outfile: CodeWriter):
    outfile.writeln("#endif //! __" + str.replace(file_name, ".", "_").upper() + "__")
    return


def write_c_guard_start(outfile: CodeWriter):
    outfile.writeln("#ifdef __cplusplus")
    outfile.writeln("extern \"C\" {")
    outfile.writeln("#endif //! __cplusplus")
    return


def write_c_guard_end(outfile: CodeWriter):
    outfile.writeln("#ifdef __cplusplus")
    outfile.writeln("}")
    outfile.writeln("#endif //! __cplusplus")
    outfile.writeln("")
    return


def define_shared_ids(service: ServiceObject, outfile: CodeWriter):
    prefix = service.get_namespace().upper() + "_" + service.get_name().upper()
    outfile.writeln(f"#define SERVICE_{prefix}_ID {str(service.get_id())}")
    outfile.writeln(f"#define SERVICE_{prefix}_FUNCTION_COUNT {str(len(service.get_functions()))}")
    outfile.writeln("")

    if len(service.get_functions()) > 0:
        for func in service.get_functions():
            func_prefix = prefix + "_" + func.get_name().upper()
            outfile.writeln("#define SERVICE_" + func_prefix + "_ID " + str(func.get_id()))
        outfile.writeln("")

    if len(service.get_events()) > 0:
        for evt in service.get_events():
            evt_prefix = prefix + "_EVENT_" + evt.get_name().upper()
            outfile.writeln("#define SERVICE_" + evt_prefix + "_ID " + str(evt.get_id()))
        outfile.writeln("")
    return


def write_variable_count(members, outfile: CodeWriter):
    needs_count = False
    for member in members:
        if member.get_is_variable():
            needs_count = True

    if needs_count:
        outfile.writeln("uint32_t __count;")


def write_variable_struct_member_serializer(service: ServiceObject, member, outfile: CodeWriter):
    name = member.get_name()
    typename = member.get_typename()
    outfile.writeln(f"serialize_uint32(buffer, in->{name}_count);")
    if service.typename_is_struct(member.get_typename()):
        struct_type = service.lookup_struct(typename)
        outfile.writeln(f"for (uint32_t __i = 0; __i < in->{name}_count; __i++) {{")
        outfile.indent_inc()
        outfile.writeln(f"serialize_{get_scoped_name(struct_type)}(buffer, &in->{name}[__i]);")
        outfile.indent_dec()
        outfile.writeln("}")
    elif typename.lower() == "string":
        print("error: variable string arrays are not supported at this moment for the C-code generator")
        exit(-1)
    else:
        outfile.writeln(
            f"memcpy(&buffer->data[buffer->index], &in->{name}[0], sizeof({get_c_typename(service, typename)}) * in->{name}_count);")
        outfile.writeln(f"buffer->index += sizeof({get_c_typename(service, typename)}) * in->{name}_count;")


def write_variable_member_serializer(service: ServiceObject, member, outfile: CodeWriter):
    name = member.get_name()
    typename = member.get_typename()
    outfile.writeln(f"serialize_uint32(&__buffer, {name}_count);")
    if service.typename_is_struct(typename):
        struct_type = service.lookup_struct(typename)
        outfile.writeln(f"for (uint32_t __i = 0; __i < {name}_count; __i++) {{")
        outfile.indent_inc()
        outfile.writeln(f"serialize_{get_scoped_name(struct_type)}(&__buffer, &{name}[__i]);")
        outfile.indent_dec()
        outfile.writeln("}")
    elif typename.lower() == "string":
        outfile.writeln(f"for (uint32_t __i = 0; __i < {name}_count; __i++) {{")
        outfile.indent_inc()
        outfile.writeln(f"serialize_string(&__buffer, {name}[__i]);")
        outfile.indent_dec()
        outfile.writeln("}")
    else:
        outfile.writeln(f"if ({name}_count) {{")
        outfile.indent_inc()
        outfile.writeln(
            f"memcpy(&__buffer.data[__buffer.index], &{name}[0], sizeof({get_c_typename(service, typename)}) * {name}_count);")
        outfile.writeln(f"__buffer.index += sizeof({get_c_typename(service, typename)}) * {name}_count;")
        outfile.indent_dec()
        outfile.writeln("}")


def write_struct_variant_serializer(service: ServiceObject, struct: StructureObject, member: VariableVariantObject, outfile: CodeWriter):
    outfile.writeln(f"serialize_uint8(buffer, in->{member.get_name()}_type);")
    outfile.writeln(f"switch (in->{member.get_name()}_type) {{")
    outfile.writeln(f"default: break;")
    for entry in member.get_entries():
        outfile.writeln(f"case {get_variant_enum_name(struct, member, entry)}:")
        outfile.indent_inc()
        write_struct_member_serializer(service, f"{member.get_name()}.", struct, entry, outfile)
        outfile.writeln("break;")
        outfile.indent_dec()
    outfile.writeln("}")
    return


def write_struct_member_serializer(service: ServiceObject, prefix, struct, member, outfile: CodeWriter):
    if isinstance(member, VariableVariantObject):
        write_struct_variant_serializer(service, struct, member, outfile)
    elif member.get_is_variable():
        write_variable_struct_member_serializer(service, member, outfile)
    elif service.typename_is_struct(member.get_typename()):
        struct_type = service.lookup_struct(member.get_typename())
        outfile.writeln(f"serialize_{get_scoped_name(struct_type)}(buffer, &in->{prefix}{member.get_name()});")
    elif service.typename_is_enum(member.get_typename()):
        outfile.writeln(f"serialize_int(buffer, (int)(in->{prefix}{member.get_name()}));")
    else:
        outfile.writeln(f"serialize_{member.get_typename()}(buffer, in->{prefix}{member.get_name()});")


def write_member_serializer(service: ServiceObject, member, outfile: CodeWriter):
    if member.get_is_variable():
        write_variable_member_serializer(service, member, outfile)
    elif service.typename_is_struct(member.get_typename()):
        struct_type = service.lookup_struct(member.get_typename())
        outfile.writeln(f"serialize_{get_scoped_name(struct_type)}(&__buffer, {member.get_name()});")
    elif service.typename_is_enum(member.get_typename()):
        outfile.writeln(f"serialize_int(&__buffer, (int){member.get_name()});")
    else:
        value = member.get_name()
        if member.get_fixed():
            value = member.get_default_value()
        outfile.writeln(f"serialize_{member.get_typename()}(&__buffer, {value});")


def write_variable_struct_member_deserializer(service: ServiceObject, member, outfile: CodeWriter):
    name = member.get_name()
    typename = member.get_typename()
    outfile.writeln(f"out->{name}_count = deserialize_uint32(buffer);")
    outfile.writeln(f"if (out->{name}_count) {{")
    outfile.indent_inc()
    outfile.writeln(f"out->{name} = malloc(sizeof({get_c_typename(service, typename)}) * out->{name}_count);")
    outfile.writeln(f"assert(out->{name} != NULL);")
    if service.typename_is_struct(typename):
        struct_type = service.lookup_struct(typename)
        struct_name = get_scoped_name(struct_type)
        outfile.writeln(f"for (int __i = 0; __i < out->{name}_count; __i++) {{")
        outfile.indent_inc()
        outfile.writeln(f"deserialize_{struct_name}(buffer, &out->{name}[__i]);")
        outfile.indent_dec()
        outfile.writeln("}")
    elif member.get_typename().lower() == "string":
        print("error: variable string arrays are not supported at this moment for the C-code generator")
        exit(-1)
    else:
        outfile.writeln(f"memcpy(&out->{name}[0], &buffer->data[buffer->index], sizeof({get_c_typename(service, typename)}) * out->{name}_count);")
        outfile.writeln(f"buffer->index += sizeof({get_c_typename(service, typename)}) * out->{name}_count;")
    outfile.indent_dec()
    outfile.writeln("} else {")
    outfile.indent_inc()
    outfile.writeln(f"out->{name} = NULL;")
    outfile.indent_dec()
    outfile.writeln("}")


def write_variable_member_deserializer2(service: ServiceObject, member, outfile: CodeWriter):
    name = member.get_name()
    typename = member.get_typename()
    c_typename = get_c_typename(service, typename)

    # get the count of elements to deserialize, and then allocate buffer space
    outfile.writeln(f"{name}_count = deserialize_uint32(__buffer);")
    outfile.writeln(f"if ({name}_count) {{")
    outfile.indent_inc()
    outfile.writeln(f"{name} = malloc(sizeof({c_typename}) * {name}_count);")
    outfile.writeln(f"if (!{name}) {{")
    outfile.indent_inc()
    outfile.writeln(f"return;\n")
    outfile.indent_dec()
    outfile.writeln("}")
    outfile.writeln("")

    if typename.lower() == "string" or service.typename_is_struct(typename):
        outfile.writeln(f"for (uint32_t __i = 0; __i < {name}_count; __i++) {{")
        outfile.indent_inc()
        if typename.lower() == "string":
            outfile.writeln(f"{name}[__i] = deserialize_string_nocopy(__buffer);")
            print("error: variable string arrays are not supported at this moment for the C-code generator")
            exit(-1)
        elif service.typename_is_struct(member.get_typename()):
            struct_type = service.lookup_struct(typename)
            struct_name = get_scoped_name(struct_type)
            outfile.writeln(f"deserialize_{struct_name}(__buffer, &{name}[__i]);")
        outfile.indent_dec()
        outfile.writeln("}")
        outfile.writeln("")
    else:
        outfile.writeln(f"memcpy(&{name}[0], &__buffer->data[__buffer->index], sizeof({c_typename}) * {name}_count);")
        outfile.writeln(f"__buffer->index += sizeof({c_typename}) * {name}_count;")
    outfile.indent_dec()
    outfile.writeln("}")
    outfile.writeln("")


def write_variable_member_deserializer(service: ServiceObject, member, outfile: CodeWriter):
    typename = member.get_typename()
    name = member.get_name()
    outfile.writeln("__count = deserialize_uint32(&__buffer);")
    if service.typename_is_struct(typename):
        outfile.writeln(f"for (uint32_t __i = 0; __i < GRMIN(__count, {name}_count); __i++) {{")
        outfile.indent_inc()
        struct_type = service.lookup_struct(typename)
        struct_name = get_scoped_name(struct_type)
        outfile.writeln(f"deserialize_{struct_name}(&__buffer, &{name}_out[__i]);")
        outfile.indent_dec()
        outfile.writeln("}")
    elif typename.lower() == "string":
        outfile.writeln(f"for (uint32_t __i = 0; __i < GRMIN(__count, {name}_max_length); __i++) {{")
        outfile.indent_inc()
        outfile.writeln(f"{name}_out[__i] = deserialize_string_nocopy(&__buffer);")
        outfile.indent_dec()
        outfile.writeln("}")
    else:
        outfile.writeln(f"if (__count) {{")
        outfile.indent_inc()
        outfile.writeln(f"memcpy(&{name}_out[0], &__buffer.data[__buffer.index], sizeof({get_c_typename(service, typename)}) * GRMIN(__count, {name}_count));")
        outfile.writeln(f"__buffer.index += sizeof({get_c_typename(service, typename)}) * __count;")
        outfile.indent_dec()
        outfile.writeln("}")


def write_struct_variant_deserializer(service: ServiceObject, struct: StructureObject, member, outfile: CodeWriter):
    name = member.get_name()
    outfile.writeln(f"out->{name}_type = deserialize_uint8(buffer);")
    outfile.writeln(f"switch (out->{name}_type) {{")
    outfile.writeln(f"default: break;")
    for entry in member.get_entries():
        outfile.writeln(f"case {get_variant_enum_name(struct, member, entry)}:")
        outfile.indent_inc()
        write_struct_member_deserializer(service, f"{name}.", struct, entry, outfile)
        outfile.writeln("break;")
        outfile.indent_dec()
    outfile.writeln("}")
    return

def write_struct_member_deserializer(service: ServiceObject, prefix, struct, member, outfile: CodeWriter):
    if isinstance(member, VariableVariantObject):
        write_struct_variant_deserializer(service, struct, member, outfile)
        return

    name = member.get_name()
    typename = member.get_typename()
    if member.get_is_variable():
        write_variable_struct_member_deserializer(service, member, outfile)
    elif typename.lower() == "string":
        outfile.writeln(f"uint32_t _{name}_length = *((uint32_t*)&buffer->data[buffer->index]);")
        outfile.writeln(f"out->{name} = malloc(_{name}_length + 1);")
        outfile.writeln(f"assert(out->{name} != NULL);")
        outfile.writeln(f"deserialize_string_copy(buffer, &out->{prefix}{name}[0], 0);")
    elif service.typename_is_struct(typename):
        struct_type = service.lookup_struct(typename)
        struct_name = get_scoped_name(struct_type)
        outfile.writeln(f"deserialize_{struct_name}(buffer, &out->{prefix}{name});")
    elif service.typename_is_enum(typename):
        enum_type = service.lookup_enum(typename)
        enum_typename = get_scoped_typename(enum_type)
        outfile.writeln(f"out->{prefix}{name} = ({enum_typename})deserialize_int(buffer);")
    else:
        outfile.writeln(f"out->{prefix}{name} = deserialize_{typename}(buffer);")


def write_member_deserializer2(service: ServiceObject, member, outfile: CodeWriter):
    name = member.get_name()
    typename = member.get_typename()

    if member.get_is_variable():
        write_variable_member_deserializer2(service, member, outfile)
    elif typename.lower() == "string":
        outfile.writeln(f"{name} = deserialize_string_nocopy(__buffer);")
    elif service.typename_is_struct(typename):
        struct_type = service.lookup_struct(typename)
        struct_name = get_scoped_name(struct_type)
        outfile.writeln(f"deserialize_{struct_name}(__buffer, &{name});")
    elif service.typename_is_enum(member.get_typename()):
        enum_type = service.lookup_enum(typename)
        enum_name = get_scoped_typename(enum_type)
        outfile.writeln(f"{name} = ({enum_name})deserialize_int(__buffer);")
    else:
        outfile.writeln(f"{name} = deserialize_{typename}(__buffer);")


def write_member_deserializer(service: ServiceObject, member, outfile: CodeWriter):
    typename = member.get_typename()
    name = member.get_name()
    if member.get_is_variable():
        write_variable_member_deserializer(service, member, outfile)
    elif typename.lower() == "string":
        outfile.writeln(f"deserialize_string_copy(&__buffer, &{name}_out[0], {name}_max_length);")
    elif service.typename_is_struct(typename):
        struct_type = service.lookup_struct(typename)
        struct_name = get_scoped_name(struct_type)
        outfile.writeln(f"deserialize_{struct_name}(&__buffer, {name}_out);")
    elif service.typename_is_enum(member.get_typename()):
        enum_type = service.lookup_enum(typename)
        enum_name = get_scoped_typename(enum_type)
        outfile.writeln(f"*{name}_out = ({enum_name})deserialize_int(&__buffer);")
    else:
        outfile.writeln(f"*{name}_out = deserialize_{typename}(&__buffer);")


def write_function_body_prologue(service: ServiceObject, action_id, flags, params, is_server, outfile: CodeWriter):
    outfile.writeln("gracht_buffer_t __buffer;")
    outfile.writeln("int __status;")
    outfile.writeln("")

    if is_server:
        if "MESSAGE_FLAG_RESPONSE" in flags:
            outfile.writeln("__status = gracht_server_get_buffer(message->server, &__buffer);")
        else:
            outfile.writeln("__status = gracht_server_get_buffer(server, &__buffer);")
    else:
        outfile.writeln("__status = gracht_client_get_buffer(client, &__buffer);")
    outfile.writeln("if (__status) {")
    outfile.writeln("    return __status;")
    outfile.writeln("}")
    outfile.writeln("")

    # build message header
    # id and length are filled out by the server/client
    outfile.writeln(f"serialize_uint32(&__buffer, 0);")
    outfile.writeln(f"serialize_uint32(&__buffer, 0);")
    outfile.writeln(f"serialize_uint8(&__buffer, {str(service.get_id())});")
    outfile.writeln(f"serialize_uint8(&__buffer, {str(action_id)});")
    outfile.writeln(f"serialize_uint8(&__buffer, {str(flags)});")

    for param in params:
        write_member_serializer(service, param, outfile)


def write_function_body_epilogue(service: ServiceObject, func: FunctionObject, outfile: CodeWriter):
    # dunno yet
    outfile.write("return __status;\n")


def define_function_body(service: ServiceObject, func: FunctionObject, outfile: CodeWriter):
    flags = get_message_flags_func(func)
    write_function_body_prologue(service, func.get_id(), flags, func.get_request_params(), False, outfile)
    outfile.write("__status = gracht_client_invoke(client, context, &__buffer);\n")
    write_function_body_epilogue(service, func, outfile)
    return


def write_status_body_prologue(service: ServiceObject, func: FunctionObject, outfile: CodeWriter):
    outfile.writeln("gracht_buffer_t __buffer;")
    outfile.writeln("int __status;")
    write_variable_count(func.get_response_params(), outfile)
    outfile.writeln("")

    outfile.writeln("__status = gracht_client_get_status_buffer(client, context, &__buffer);")
    outfile.writeln("if (__status != GRACHT_MESSAGE_COMPLETED) {")
    outfile.writeln("    return __status;")
    outfile.writeln("}")
    outfile.writeln("")

    # in the status body we must use string_copy versions as the buffer dissappears shortly after
    # deserialization. That unfortunately means all deserialization code that is not known before-hand
    # which situtation must use _copy code instead of _nocopy
    for param in func.get_response_params():
        write_member_deserializer(service, param, outfile)


def write_status_body_epilogue(service: ServiceObject, func: FunctionObject, outfile: CodeWriter):
    # dunno yet
    outfile.write("return __status;\n")


def define_status_body(service: ServiceObject, func: FunctionObject, outfile: CodeWriter):
    write_status_body_prologue(service, func, outfile)
    outfile.write("__status = gracht_client_status_finalize(client, context);\n")
    write_status_body_epilogue(service, func, outfile)


def define_event_body_single(service: ServiceObject, evt, outfile: CodeWriter):
    flags = "MESSAGE_FLAG_EVENT"
    write_function_body_prologue(service, evt.get_id(), flags, evt.get_params(), True, outfile)
    outfile.write("__status = gracht_server_send_event(server, client, &__buffer, 0);\n")
    write_function_body_epilogue(service, evt, outfile)


def define_event_body_all(service: ServiceObject, evt, outfile: CodeWriter):
    flags = "MESSAGE_FLAG_EVENT"
    write_function_body_prologue(service, evt.get_id(), flags, evt.get_params(), True, outfile)
    outfile.write("__status = gracht_server_broadcast_event(server, &__buffer, 0);\n")
    write_function_body_epilogue(service, evt, outfile)


def define_response_body(service: ServiceObject, func, flags, outfile: CodeWriter):
    flags = "MESSAGE_FLAG_RESPONSE"
    write_function_body_prologue(service, func.get_id(), flags, func.get_response_params(), True, outfile)
    outfile.write("__status = gracht_server_respond(message, &__buffer);\n")
    write_function_body_epilogue(service, func, outfile)


def define_shared_serializers(service: ServiceObject, outfile: CodeWriter):
    system_types = [
        ["uint8", "uint8_t"],
        ["int8", "int8_t"],
        ["uint16", "uint16_t"],
        ["int16", "int16_t"],
        ["uint32", "uint32_t"],
        ["int32", "int32_t"],
        ["uint64", "uint64_t"],
        ["int64", "int64_t"],
        ["long", "long"],
        ["ulong", "size_t"],
        ["uint", "unsigned int"],
        ["int", "int"],
        ["bool", "uint8_t"],
        ["float", "float"],
        ["double", "double"]
    ]

    outfile.writeln("""
#ifndef GRMIN
#define GRMIN(a, b) (((a)<(b))?(a):(b))
#endif //! GRMIN

#ifndef __GRACHT_SERVICE_SHARED_SERIALIZERS
#define __GRACHT_SERVICE_SHARED_SERIALIZERS
#define SERIALIZE_VALUE(name, type) static inline void serialize_##name(gracht_buffer_t* buffer, type value) { \\
                                        *((type*)&buffer->data[buffer->index]) = value; buffer->index += sizeof(type); \\
                                    }

#define DESERIALIZE_VALUE(name, type) static inline type deserialize_##name(gracht_buffer_t* buffer) { \\
                                          type value = *((type*)&buffer->data[buffer->index]); \\
                                          buffer->index += sizeof(type); \\
                                          return value; \\
                                       }

""")

    for system_type in system_types:
        outfile.writeln(f"SERIALIZE_VALUE({system_type[0]}, {system_type[1]})")
        outfile.writeln(f"DESERIALIZE_VALUE({system_type[0]}, {system_type[1]})")

    # The spec for string is that we write their length, then their contents including the zero terminator
    # The length is only for the data excluding the zero terminator
    # 0-4: length
    # 5-N: data
    # N+1: zero terminator
    outfile.writeln("""
static inline void serialize_string(gracht_buffer_t* buffer, const char* string) {
    uint32_t length = string != NULL ? (uint32_t)strlen(string) : 0;
    *((uint32_t*)&buffer->data[buffer->index]) = length;
    if (length == 0) {
        buffer->data[buffer->index + sizeof(uint32_t)] = 0;
        buffer->index += sizeof(uint32_t) + 1;
        return;
    }
    memcpy(&buffer->data[buffer->index + sizeof(uint32_t)], string, length);
    buffer->data[buffer->index + sizeof(uint32_t) + length] = 0;
    buffer->index += (sizeof(uint32_t) + length + 1);
}

static inline void deserialize_string_copy(gracht_buffer_t* buffer, char* out, uint32_t maxLength) {
    uint32_t length = *((uint32_t*)&buffer->data[buffer->index]);
    uint32_t clampedLength = GRMIN(length, maxLength - 1);
    if (clampedLength > 0) {
        memcpy(out, &buffer->data[buffer->index + sizeof(uint32_t)], clampedLength);
    }
    out[clampedLength] = 0;
    buffer->index += sizeof(uint32_t) + length + 1;
}

static inline char* deserialize_string_nocopy(gracht_buffer_t* buffer) {
    uint32_t length = *((uint32_t*)&buffer->data[buffer->index]);
    char*    string = &buffer->data[buffer->index + sizeof(uint32_t)];
    buffer->index += sizeof(uint32_t) + length + 1;
    return string;
}
#endif //! __GRACHT_SERVICE_SHARED_SERIALIZERS

""")

    for struct in service.get_structs():
        struct_name = get_scoped_name(struct)
        struct_typename = get_scoped_typename(struct)
        outfile.writeln(f"{struct_typename};")
        outfile.writeln(f"static void serialize_{struct_name}(gracht_buffer_t* buffer, const {struct_typename}* in);")
        outfile.writeln(f"static void deserialize_{struct_name}(gracht_buffer_t* buffer, {struct_typename}* out);")
        outfile.writeln("")
    outfile.writeln("")


def define_type_serializers(service: ServiceObject, outfile: CodeWriter):
    for defined_type in service.get_types():
        guard_name = defined_type.get_namespace().upper() + "_" + defined_type.get_type_name().upper()
        outfile.write(f"#ifndef __GRACHT_{guard_name}_DEFINED__\n")
        outfile.write(f"#define __GRACHT_{guard_name}_DEFINED__\n")
        outfile.write(f"SERIALIZE_VALUE({defined_type.get_type_name()}, {defined_type.get_type_name()})\n")
        outfile.write(f"DESERIALIZE_VALUE({defined_type.get_type_name()}, {defined_type.get_type_name()})\n")
        outfile.write(f"#endif //! __GRACHT_{guard_name}_DEFINED__\n\n")


def define_struct_serializers(service: ServiceObject, outfile: CodeWriter):
    for struct in service.get_structs():
        struct_name = get_scoped_name(struct)
        struct_typename = get_scoped_typename(struct)
        guard_name = f"{struct_name.upper()}_SERIALIZER"
        outfile.writeln(f"#ifndef __GRACHT_{guard_name}_DEFINED__")
        outfile.writeln(f"#define __GRACHT_{guard_name}_DEFINED__")
        outfile.writeln(f"static void serialize_{struct_name}(gracht_buffer_t* buffer, const {struct_typename}* in) {{")
        outfile.indent_inc()

        for member in struct.get_members():
            write_struct_member_serializer(service, "", struct, member, outfile)
        outfile.indent_dec()
        outfile.writeln("}")
        outfile.writeln("")

        outfile.writeln(f"static void deserialize_{struct_name}(gracht_buffer_t* buffer, {struct_typename}* out) {{")
        outfile.indent_inc()

        for member in struct.get_members():
            write_struct_member_deserializer(service, "", struct, member, outfile)
        outfile.indent_dec()
        outfile.writeln("}") 
        outfile.writeln(f"#endif //! __GRACHT_{guard_name}_DEFINED__")
        outfile.writeln("")


def write_enum(enum, outfile: CodeWriter):
    enum_name = get_scoped_name(enum)
    enum_typename = get_scoped_typename(enum)
    outfile.writeln("#ifndef __" + enum_name.upper() + "_DEFINED__")
    outfile.writeln("#define __" + enum_name.upper() + "_DEFINED__")
    outfile.writeln(enum_typename + " {")
    outfile.indent_inc()
    for value in enum.get_values():
        value_name = enum_name.upper() + "_" + value.get_name()
        if value.get_value() is None:
            outfile.writeln(f"{value_name},")
        else:
            outfile.writeln(f"{value_name} = {str(value.get_value())},")
    outfile.indent_dec()
    outfile.writeln("};")
    outfile.writeln("#endif //! __" + enum_name.upper() + "_DEFINED__")
    outfile.writeln("")


def define_enums(service: ServiceObject, outfile: CodeWriter):
    if len(service.get_enums()) > 0:
        for enum in service.get_enums():
            if len(enum.get_values()):
                write_enum(enum, outfile)
        outfile.writeln("")


def write_structure_union(service: ServiceObject, union: VariableVariantObject, case, outfile: CodeWriter):
    outfile.writeln(f"uint8_t {union.get_name()}_type;")
    outfile.writeln("union {")
    outfile.indent_inc()
    write_structure_members(service, union.get_entries(), case, outfile)
    outfile.indent_dec()
    outfile.writeln(f"}} {union.get_name()};")


def write_structure_members(service: ServiceObject, members, case, outfile: CodeWriter):
    for member in members:
        if isinstance(member, VariableVariantObject):
            write_structure_union(service, member, case, outfile)
            continue
        elif member.get_is_variable():
            outfile.writeln(f"uint32_t {member.get_name()}_count;")
        outfile.writeln(get_param_typename(service, member, case, False) + ";")


def get_variant_enum_name(struct: StructureObject, member, variant):
    return f"{get_scoped_name(struct).upper()}_{member.get_name().upper()}_{variant.get_name().upper()}"


def write_structure_variant_enums(struct, outfile: CodeWriter):
    for member in struct.get_members():
        if isinstance(member, VariableVariantObject):
            outfile.writeln(f"enum {get_scoped_name(struct)}_{member.get_name()}_type {{")
            outfile.indent_inc()
            outfile.writeln(f"{get_scoped_name(struct).upper()}_{member.get_name().upper()}_INVALID = 0,")
            for value in member.get_entries():
                outfile.writeln(f"{get_variant_enum_name(struct, member, value)},")
            outfile.indent_dec()
            outfile.writeln("};")
            outfile.writeln("")


def write_structure(service: ServiceObject, struct, case, outfile: CodeWriter):
    outfile.writeln(f"{get_scoped_typename(struct)} {{")
    outfile.indent_inc()
    write_structure_members(service, struct.get_members(), case, outfile)
    outfile.indent_dec()
    outfile.writeln("};")


def write_structure_variant_member_functions(service: ServiceObject, struct: StructureObject, variant: VariableVariantObject, outfile: CodeWriter):
    struct_name = get_scoped_name(struct)
    struct_typename = get_scoped_typename(struct)

    for i, entry in enumerate(variant.get_entries()):
        outfile.writeln(f"static int {struct_name}_{variant.get_name()}_is_{entry.get_typename()}({struct_typename}* in) {{")
        outfile.indent_inc()
        outfile.writeln(f"return in->{variant.get_name()}_type == {get_variant_enum_name(struct, variant, entry)};")
        outfile.indent_dec()
        outfile.writeln("}")
        outfile.writeln("")
    
    for i, entry in enumerate(variant.get_entries()):
        param_string = get_param_typename(service, entry, CONST.TYPENAME_CASE_FUNCTION_CALL, False)
        outfile.writeln(f"static void {struct_name}_{variant.get_name()}_set_{entry.get_typename()}({struct_typename}* in, {param_string}) {{")
        outfile.indent_inc()
        if service.typename_is_struct(entry.get_typename()):
            struct_type = service.lookup_struct(entry.get_typename())
            member_name = get_scoped_name(struct_type)
            outfile.writeln(f"{member_name}_copy({entry.get_name()}, &in->{variant.get_name()}.{entry.get_name()});")
            outfile.writeln(f"in->{variant.get_name()}_type = {get_variant_enum_name(struct, variant, entry)};")
        elif entry.get_typename() == "string":
            outfile.writeln(f"if ({entry.get_typename()} != NULL) {{")
            outfile.indent_inc()
            outfile.writeln(f"in->{variant.get_name()}.{entry.get_name()} = strdup({entry.get_typename()});")
            outfile.indent_dec()
            outfile.writeln("}")
        else:
            outfile.writeln(f"in->{variant.get_name()}.{entry.get_name()} = {entry.get_typename()};")
        outfile.indent_dec()
        outfile.writeln("}")
        outfile.writeln("")
    
    # write clear function
    outfile.writeln(f"static void {struct_name}_{variant.get_name()}_clear({struct_typename}* in) {{")
    outfile.indent_inc()
    outfile.writeln(f"switch (in->{variant.get_name()}_type) {{")
    outfile.writeln("default: break;")
    for i, entry in enumerate(variant.get_entries()):
        outfile.writeln(f"case {get_variant_enum_name(struct, variant, entry)}:")
        outfile.indent_inc()
        write_structure_member_destructor(service, f"in->{variant.get_name()}.", struct, entry, outfile)
        outfile.writeln("break;")
        outfile.indent_dec()
    outfile.writeln("}")
    outfile.writeln(f"in->{variant.get_name()}_type = {get_scoped_name(struct).upper()}_{variant.get_name().upper()}_INVALID;")
    outfile.indent_dec()
    outfile.writeln("}")
    outfile.writeln("")


def write_structure_member_functions(service: ServiceObject, struct: StructureObject, outfile: CodeWriter):
    struct_name = get_scoped_name(struct)
    struct_typename = get_scoped_typename(struct)

    for member in struct.get_members():
        if isinstance(member, VariableVariantObject):
            write_structure_variant_member_functions(service, struct, member, outfile)
        elif member.get_is_variable():
            if service.typename_is_struct(member.get_typename()):
                struct_type = service.lookup_struct(member.get_typename())
                member_name = get_scoped_name(struct_type)
                member_typename = get_scoped_typename(struct_type)

                # write allocator
                outfile.writeln(f"static void {struct_name}_{member.get_name()}_add({struct_typename}* in, uint32_t count) {{")
                outfile.indent_inc()
                outfile.writeln(f"if (in->{member.get_name()}) {{")
                outfile.indent_inc()
                outfile.writeln(f"in->{member.get_name()} = realloc(in->{member.get_name()}, sizeof({member_typename}) * (in->{member.get_name()}_count + count));")
                outfile.indent_dec()
                outfile.writeln("} else {")
                outfile.indent_inc()
                outfile.writeln(f"in->{member.get_name()} = malloc(sizeof({member_typename}) * count);")
                outfile.indent_dec()
                outfile.writeln("}")
                outfile.writeln("")
                outfile.writeln("for (uint32_t i = 0; i < count; i++) {")
                outfile.indent_inc()
                outfile.writeln(f"{member_name}_init(&in->{member.get_name()}[in->{member.get_name()}_count + i]);")
                outfile.indent_dec()
                outfile.writeln("}")
                outfile.writeln(f"in->{member.get_name()}_count += count;")
                outfile.indent_dec()
                outfile.writeln("}")
                outfile.writeln("")

                # write indexer
                outfile.writeln(f"static {member_typename}* {struct_name}_{member.get_name()}_get({struct_typename}* in, uint32_t index) {{")
                outfile.indent_inc()
                outfile.writeln(f"if (index >= in->{member.get_name()}_count) {{")
                outfile.indent_inc()
                outfile.writeln(f"{struct_name}_{member.get_name()}_add(in, (index - in->{member.get_name()}_count) + 1);")
                outfile.indent_dec()
                outfile.writeln("}")
                outfile.writeln(f"return &in->{member.get_name()}[index];")
                outfile.indent_dec()
                outfile.writeln("}")
                outfile.writeln("")

def write_structure_member_destructor(service: ServiceObject, prefix, struct: StructureObject, member, outfile: CodeWriter) -> int:
    if isinstance(member, VariableVariantObject):
        calls = 0
        outfile.writeln(f"switch ({prefix}{member.get_name()}_type) {{")
        outfile.writeln("default: break;")
        for i, entry in enumerate(member.get_entries()):
            outfile.writeln(f"case {get_variant_enum_name(struct, member, entry)}:")
            outfile.indent_inc()
            calls += write_structure_member_destructor(service, f"{prefix}{member.get_name()}.", struct, entry, outfile)
            outfile.writeln("break;")
            outfile.indent_dec()
        outfile.writeln("}")
        return calls
    elif member.get_is_variable():
        outfile.writeln(f"if ({prefix}{member.get_name()}) {{")
        outfile.indent_inc()
        if service.typename_is_struct(member.get_typename()):
            struct_type = service.lookup_struct(member.get_typename())
            member_name = get_scoped_name(struct_type)
            outfile.writeln(f"for (int __i = 0; __i < {prefix}{member.get_name()}_count; __i++) {{")
            outfile.indent_inc()
            outfile.writeln(f"{member_name}_destroy(&{prefix}{member.get_name()}[__i]);")
            outfile.indent_dec()
            outfile.writeln("}")
        outfile.writeln(f"free({prefix}{member.get_name()});")
        outfile.indent_dec()
        outfile.writeln("}")
        return 1
    elif member.get_typename() == "string":
        outfile.writeln(f"if ({prefix}{member.get_name()}) {{")
        outfile.indent_inc()
        outfile.writeln(f"free({prefix}{member.get_name()});")
        outfile.indent_dec()
        outfile.writeln("}")
        return 1
    elif service.typename_is_struct(member.get_typename()):
        struct_type = service.lookup_struct(member.get_typename())
        member_name = get_scoped_name(struct_type)
        outfile.writeln(f"{member_name}_destroy(&{prefix}{member.get_name()});")
        return 1
    return 0

def write_structure_copy_function_members(service: ServiceObject, prefix, struct: StructureObject, member, outfile: CodeWriter):
    if isinstance(member, VariableVariantObject):
        outfile.writeln(f"out->{prefix}{member.get_name()}_type = in->{prefix}{member.get_name()}_type;")
        outfile.writeln(f"switch (in->{prefix}{member.get_name()}_type) {{")
        outfile.writeln("default: break;")
        for i, entry in enumerate(member.get_entries()):
            outfile.writeln(f"case {get_variant_enum_name(struct, member, entry)}:")
            outfile.indent_inc()
            write_structure_copy_function_members(service, f"{member.get_name()}.", struct, entry, outfile)
            outfile.writeln("break;")
            outfile.indent_dec()
        outfile.writeln("}")
    elif member.get_is_variable():
        outfile.writeln(f"out->{prefix}{member.get_name()}_count = in->{prefix}{member.get_name()}_count;")
        outfile.writeln(f"if (in->{prefix}{member.get_name()}_count) {{")
        outfile.indent_inc()
        outfile.writeln(f"out->{prefix}{member.get_name()} = malloc(sizeof({get_c_typename(service, member.get_typename())}) * in->{prefix}{member.get_name()}_count);")
        outfile.writeln(f"assert(out->{prefix}{member.get_name()} != NULL);")
        if service.typename_is_struct(member.get_typename()):
            struct_type = service.lookup_struct(member.get_typename())
            member_name = get_scoped_name(struct_type)
            outfile.writeln(f"for (int __i = 0; __i < in->{prefix}{member.get_name()}_count; __i++) {{")
            outfile.indent_inc()
            outfile.writeln(f"{member_name}_copy(&in->{prefix}{member.get_name()}[__i], &out->{prefix}{member.get_name()}[__i]);")
            outfile.indent_dec()
            outfile.writeln("}")
        else:
            outfile.writeln(f"memcpy(out->{prefix}{member.get_name()}, in->{prefix}{member.get_name()}, in->{prefix}{member.get_name()}_count);")
        outfile.indent_dec()
        outfile.writeln("} else {")
        outfile.indent_inc()
        outfile.writeln(f"out->{prefix}{member.get_name()} = NULL;")
        outfile.indent_dec()
        outfile.writeln("}")

    elif member.get_typename() == "string":
        outfile.writeln(f"if (in->{prefix}{member.get_name()} != NULL) {{")
        outfile.indent_inc()
        outfile.writeln(f"out->{prefix}{member.get_name()} = strdup(in->{prefix}{member.get_name()});")
        outfile.indent_dec()
        outfile.writeln("} else {")
        outfile.indent_inc()
        outfile.writeln(f"out->{prefix}{member.get_name()} = NULL;")
        outfile.indent_dec()
        outfile.writeln("}")
    elif service.typename_is_struct(member.get_typename()):
        struct_type = service.lookup_struct(member.get_typename())
        member_name = get_scoped_name(struct_type)
        outfile.writeln(f"{member_name}_copy(&in->{prefix}{member.get_name()}, &out->{prefix}{member.get_name()});")
    else:
        outfile.writeln(f"out->{prefix}{member.get_name()} = in->{prefix}{member.get_name()};")


def write_structure_functionality(service: ServiceObject, struct: StructureObject, outfile: CodeWriter):
    # write constructor
    struct_name = get_scoped_name(struct)
    struct_typename = get_scoped_typename(struct)
    outfile.writeln(f"static void {struct_name}_init({struct_typename}* in) {{")
    outfile.indent_inc()
    outfile.writeln(f"memset(in, 0, sizeof({struct_typename}));")
    outfile.indent_dec()
    outfile.writeln("}")
    outfile.writeln("")

    # write member-specific functions
    write_structure_member_functions(service, struct, outfile)

    # write copy constructor
    outfile.writeln(f"static void {struct_name}_copy(const {struct_typename}* in, {struct_typename}* out) {{")
    outfile.indent_inc()
    for member in struct.get_members():
        write_structure_copy_function_members(service, "", struct, member, outfile)
    outfile.indent_dec()
    outfile.writeln("}")
    outfile.writeln("")

    # write destructor
    outfile.writeln(f"static void {struct_name}_destroy({struct_typename}* in) {{")
    outfile.indent_inc()
    num_destroy_calls = 0
    for member in struct.get_members():
        num_destroy_calls += write_structure_member_destructor(service, "in->", struct, member, outfile)
    if num_destroy_calls == 0:
        outfile.writeln("(void)in;")
    outfile.indent_dec()
    outfile.writeln("}")
    outfile.writeln("")


def define_structures(service: ServiceObject, outfile: CodeWriter):
    for struct in service.get_structs():
        struct_name = get_scoped_name(struct)
        outfile.writeln("#ifndef __" + struct_name.upper() + "_DEFINED__")
        outfile.writeln("#define __" + struct_name.upper() + "_DEFINED__")
        write_structure_variant_enums(struct, outfile)
        write_structure(service, struct, CONST.TYPENAME_CASE_MEMBER, outfile)
        outfile.writeln("\n")
        write_structure_functionality(service, struct, outfile)
        outfile.writeln("#endif //! __" + struct_name.upper() + "_DEFINED__")
        outfile.writeln("")


def write_client_api(service: ServiceObject, outfile: CodeWriter):
    outfile.writeln("""
GRACHTAPI int gracht_client_get_buffer(gracht_client_t*, gracht_buffer_t*);
GRACHTAPI int gracht_client_get_status_buffer(gracht_client_t*, struct gracht_message_context*, gracht_buffer_t*);
GRACHTAPI int gracht_client_status_finalize(gracht_client_t*, struct gracht_message_context*);
GRACHTAPI int gracht_client_invoke(gracht_client_t*, struct gracht_message_context*, gracht_buffer_t*);
""")


def write_server_api(service: ServiceObject, outfile: CodeWriter):
    outfile.writeln("""
GRACHTAPI int gracht_server_get_buffer(gracht_server_t*, gracht_buffer_t*);
GRACHTAPI int gracht_server_respond(struct gracht_message*, gracht_buffer_t*);
GRACHTAPI int gracht_server_send_event(gracht_server_t*, gracht_conn_t client, gracht_buffer_t*, unsigned int flags);
GRACHTAPI int gracht_server_broadcast_event(gracht_server_t*, gracht_buffer_t*, unsigned int flags);
""")


# Define the client callback array - this is the one that will be registered with the client
# and handles the delegation of deserializing of incoming events.
def write_client_callback_array(service: ServiceObject, outfile: CodeWriter):
    if len(service.get_events()) == 0:
        return

    # define the internal callback prototypes first
    for evt in service.get_events():
        write_client_deserializer_prototype(service, evt, outfile)
        outfile.write(";\n")
    outfile.write("\n")

    callback_array_name = service.get_namespace() + "_" + service.get_name() + "_callbacks"
    callback_array_size = str(len(service.get_events()))
    outfile.write("static gracht_protocol_function_t ")
    outfile.write(f"{callback_array_name}[{callback_array_size}] = ")
    outfile.write("{\n")
    for evt in service.get_events():
        evt_name = service.get_namespace().upper() + "_" \
                   + service.get_name().upper() + "_EVENT_" + evt.get_name().upper()
        evt_definition = "SERVICE_" + evt_name + "_ID"
        outfile.write("    { " + evt_definition + ", ")
        outfile.write(get_service_internal_callback_name(service, evt))
        outfile.write(" },\n")
    outfile.write("};\n\n")

    outfile.write(f"gracht_protocol_t {service.get_namespace()}_{service.get_name()}_client_protocol = ")
    outfile.write(f"GRACHT_PROTOCOL_INIT({str(service.get_id())}, \""
                  + f"{service.get_namespace().lower()}_{service.get_name().lower()}"
                  + f"\", {callback_array_size}, {callback_array_name});\n\n")


# Shared deserializer logic subunits
def write_deserializer_prologue(service: ServiceObject, members, outfile: CodeWriter):
    # write definitions
    for param in members:
        star_modifier = ""
        default_value = ""
        if param.get_is_variable():
            outfile.writeln(f"uint32_t {param.get_name()}_count;")
            star_modifier = "*"
            default_value = " = NULL"
        if param.get_typename().lower() == "string":
            outfile.writeln(f"char*{star_modifier} {param.get_name() + default_value};")
        elif service.typename_is_struct(param.get_typename()):
            struct_type = service.lookup_struct(param.get_typename())
            typename = get_scoped_typename(struct_type)
            outfile.writeln(f"{typename}{star_modifier} {param.get_name() + default_value};")
        elif service.typename_is_enum(param.get_typename()):
            enum_type = service.lookup_enum(param.get_typename())
            typename = get_scoped_typename(enum_type)
            outfile.writeln(f"{typename}{star_modifier} {param.get_name() + default_value};")
        else:
            outfile.writeln(
                f"{get_c_typename(service, param.get_typename())}{star_modifier} {param.get_name() + default_value};")


def write_deserializer_invocation_members(service: ServiceObject, members, outfile: CodeWriter):
    for index, member in enumerate(members):
        if index == 0:
            outfile.append(", ")
        if not member.get_is_variable() and service.typename_is_struct(member.get_typename()):
            outfile.append("&")
        outfile.append(f"{member.get_name()}")
        if member.get_is_variable():
            outfile.append(f", {member.get_name()}_count")
        if index < (len(members) - 1):
            outfile.append(", ")


def write_deserializer_destroy_members(service: ServiceObject, members, outfile: CodeWriter):
    for member in members:
        if service.typename_is_struct(member.get_typename()):
            struct_type = service.lookup_struct(member.get_typename())
            struct_name = get_scoped_name(struct_type)
            indexer = ""
            if member.get_is_variable():
                outfile.writeln(f"for (uint32_t __i = 0; __i < {member.get_name()}_count; __i++) {{")
                outfile.indent_inc()
                indexer = "[__i]"
            outfile.writeln(f"{struct_name}_destroy(&{member.get_name()}{indexer});")
            if member.get_is_variable():
                outfile.indent_dec()
                outfile.writeln("}")
        if member.get_is_variable():
            outfile.writeln(f"free({member.get_name()});")


# Define the client deserializers. These are builtin callbacks that will
# be invoked on event receptions. These callbacks will then deserialize
# the wire format back to the their normal format, and invoke the user-specificed
# callbacks.
def write_client_deserializers(service: ServiceObject, outfile):
    for evt in service.get_events():
        write_client_deserializer(service, evt, outfile)


def write_client_deserializer(service: ServiceObject, evt: EventObject, outfile):
    write_client_deserializer_prototype(service, evt, outfile)
    outfile.write("\n")
    write_client_deserializer_body(service, evt, outfile)


def write_client_deserializer_prototype(service: ServiceObject, evt: EventObject, outfile):
    outfile.write(
        f"void {get_service_internal_callback_name(service, evt)}(gracht_client_t* __client, gracht_buffer_t* __buffer)")


def write_client_deserializer_body(service: ServiceObject, evt: EventObject, outfile: CodeWriter):
    outfile.writeln("{")
    outfile.indent_inc()

    # write pre-definition
    write_deserializer_prologue(service, evt.get_params(), outfile)

    # write deserializer calls
    for param in evt.get_params():
        write_member_deserializer2(service, param, outfile)

    # write invocation line
    outfile.write(f"{get_client_event_callback_name(service, evt)}(__client")
    write_deserializer_invocation_members(service, evt.get_params(), outfile)
    outfile.append(");\n")

    # write destroy calls
    write_deserializer_destroy_members(service, evt.get_params(), outfile)
    outfile.indent_dec()
    outfile.writeln("}")
    outfile.writeln("")


# Define the server callback array - this is the one that will be registered with the server
# and handles the delegation of deserializing of incoming calls.
def write_server_callback_array(service: ServiceObject, outfile):
    if len(service.get_functions()) == 0:
        return

    # define the internal callback prototypes first
    for func in service.get_functions():
        write_server_deserializer_prototype(service, func, outfile)
        outfile.write(";\n")
    outfile.write("\n")

    callback_array_name = f"{service.get_namespace()}_{service.get_name()}_callbacks"
    callback_array_size = str(len(service.get_functions()))
    outfile.write("static gracht_protocol_function_t ")
    outfile.write(f"{callback_array_name}[{callback_array_size}] = ")
    outfile.write("{\n")
    for func in service.get_functions():
        func_name = service.get_namespace().upper() + "_" \
                    + service.get_name().upper() + "_" + func.get_name().upper()
        func_definition = "SERVICE_" + func_name + "_ID"
        outfile.write("    { " + func_definition + ", ")
        outfile.write(get_service_internal_callback_name(service, func))
        outfile.write(" },\n")
    outfile.write("};\n\n")

    outfile.write(f"gracht_protocol_t {service.get_namespace()}_{service.get_name()}_server_protocol = ")
    outfile.write("GRACHT_PROTOCOL_INIT(" + str(service.get_id()) + ", \""
                  + service.get_namespace().lower() + "_" + service.get_name().lower()
                  + f"\", {callback_array_size}, {callback_array_name});\n\n")


# Define the server deserializers. These are builtin callbacks that will
# be invoked on message receptions. These callbacks will then deserialize
# the wire format back to the their normal format, and invoke the user-specificed
# callbacks.
def write_server_deserializers(service: ServiceObject, outfile):
    for func in service.get_functions():
        write_server_deserializer(service, func, outfile)


def write_server_deserializer(service: ServiceObject, func: FunctionObject, outfile):
    write_server_deserializer_prototype(service, func, outfile)
    outfile.write("\n")
    write_server_deserializer_body(service, func, outfile)


def write_server_deserializer_prototype(service: ServiceObject, func: FunctionObject, outfile):
    outfile.write(
        f"void {get_service_internal_callback_name(service, func)}(struct gracht_message* __message, gracht_buffer_t* __buffer)")


def write_server_deserializer_body(service: ServiceObject, func: FunctionObject, outfile):
    outfile.writeln("{")
    outfile.indent_inc()

    # write pre-definition
    write_deserializer_prologue(service, func.get_request_params(), outfile)

    # write deserializer calls
    for param in func.get_request_params():
        write_member_deserializer2(service, param, outfile)

    # write invocation line
    outfile.write(f"{get_service_callback_name(service, func)}(__message")
    write_deserializer_invocation_members(service, func.get_request_params(), outfile)
    outfile.append(");\n")

    # write destroy calls
    write_deserializer_destroy_members(service, func.get_request_params(), outfile)
    outfile.indent_dec()
    outfile.writeln("}")
    outfile.writeln("")


class CGenerator:
    def get_server_callback_prototype(self, service, func):
        function_prototype = "void " + get_service_callback_name(service, func) + "("
        function_message_param = get_param_typename(service, VariableObject("struct gracht_message*", "message", False),
                                                    CONST.TYPENAME_CASE_FUNCTION_CALL, False)
        parameter_string = function_message_param
        if len(func.get_request_params()) > 0:
            parameter_string = parameter_string + ", " + get_parameter_string(service, func.get_request_params(),
                                                                              CONST.TYPENAME_CASE_FUNCTION_CALL, False)
        return function_prototype + parameter_string + ")"

    def get_response_prototype(self, service, func, case):
        function_prototype = "int " + get_server_service_response_name(service, func) + "("
        function_message_param = get_param_typename(service, VariableObject("struct gracht_message*", "message", False),
                                                    case, False)
        parameter_string = function_message_param + ", "
        parameter_string = parameter_string + get_parameter_string(service, func.get_response_params(), case, True)
        return function_prototype + parameter_string + ")"

    def get_function_prototype(self, service, func, case):
        function_prototype = "int " + service.get_namespace().lower() + "_" \
                             + service.get_name().lower() + "_" + func.get_name()
        function_client_param = get_param_typename(service, VariableObject("gracht_client_t*", "client", False), case,
                                                   False)
        function_context_param = get_param_typename(service,
                                                    VariableObject("struct gracht_message_context*", "context", False),
                                                    case, False)
        function_prototype = function_prototype + "(" + function_client_param + ", " + function_context_param
        input_parameters = get_parameter_string(service, func.get_request_params(), case, False)

        if input_parameters != "":
            input_parameters = ", " + input_parameters

        return function_prototype + input_parameters + ")"

    def get_function_status_prototype(self, service, func):
        case = CONST.TYPENAME_CASE_FUNCTION_STATUS
        client_param = VariableObject("gracht_client_t*", "client", False)
        context_param = VariableObject("struct gracht_message_context*", "context", False)

        function_prototype = "int " + service.get_namespace().lower() + "_" \
                             + service.get_name().lower() + "_" + func.get_name() + "_result"
        function_client_param = get_param_typename(service, client_param, case, False)
        function_context_param = get_param_typename(service, context_param, case, False)

        function_prototype = function_prototype + "(" + function_client_param + ", " + function_context_param
        output_param_string = get_parameter_string(service, func.get_response_params(), case, True)
        return function_prototype + ", " + output_param_string + ")"

    def define_prototypes(self, service, outfile):
        # This actually defines the client functions implementations, to support the subscribe/unsubscribe we must
        # generate two additional functions that have special ids
        self.define_client_subscribe_prototype(service, outfile)
        self.define_client_unsubscribe_prototype(service, outfile)

        for func in service.get_functions():
            outfile.write("    " +
                          self.get_function_prototype(service, func, CONST.TYPENAME_CASE_FUNCTION_CALL) + ";\n")
            if len(func.get_response_params()) > 0:
                outfile.write("    " + self.get_function_status_prototype(service, func) + ";\n")
        outfile.write("\n")

    def define_client_service_extern(self, service, outfile):
        if len(service.get_events()) == 0:
            return
        outfile.write(
            f"    extern gracht_protocol_t {service.get_namespace()}_{service.get_name()}_client_protocol;\n\n")

    def define_server_service_extern(self, service, outfile):
        if len(service.get_functions()) == 0:
            return
        outfile.write(
            f"    extern gracht_protocol_t {service.get_namespace()}_{service.get_name()}_server_protocol;\n\n")

    def define_client_subscribe_prototype(self, service, outfile):
        subscribe_arg = VariableObject("uint8", "service", False, "1", str(service.get_id()), True)
        subscribe_fn = FunctionObject("subscribe", 0, [subscribe_arg], [])
        control_service = ServiceObject(service.get_namespace(), 0, service.get_name(), [], [], [], [], [])
        outfile.write("    " +
                      self.get_function_prototype(control_service,
                                                  subscribe_fn, CONST.TYPENAME_CASE_FUNCTION_CALL) + ";\n")
        return

    def define_client_subscribe(self, service, outfile):
        subscribe_arg = VariableObject("uint8", "service", False, "1", str(service.get_id()), True)
        subscribe_fn = FunctionObject("subscribe", 0, [subscribe_arg], [])
        control_service = ServiceObject(service.get_namespace(), 0, service.get_name(), [], [], [], [], [])
        outfile.writeln(self.get_function_prototype(control_service, subscribe_fn, CONST.TYPENAME_CASE_FUNCTION_CALL))
        outfile.writeln("{")
        outfile.indent_inc()
        define_function_body(control_service, subscribe_fn, outfile)
        outfile.indent_dec()
        outfile.writeln("}")
        outfile.writeln("")
        return

    def define_client_unsubscribe_prototype(self, service, outfile):
        unsubscribe_arg = VariableObject("uint8", "service", False, "1", str(service.get_id()), True)
        unsubscribe_fn = FunctionObject("unsubscribe", 1, [unsubscribe_arg], [])
        control_service = ServiceObject(service.get_namespace(), 0, service.get_name(), [], [], [], [], [])
        outfile.write("    " +
                      self.get_function_prototype(control_service,
                                                  unsubscribe_fn, CONST.TYPENAME_CASE_FUNCTION_CALL) + ";\n")
        return

    def define_client_unsubscribe(self, service: ServiceObject, outfile: CodeWriter):
        unsubscribe_arg = VariableObject("uint8", "service", False, "1", str(service.get_id()), True)
        unsubscribe_fn = FunctionObject("unsubscribe", 1, [unsubscribe_arg], [])
        control_service = ServiceObject(service.get_namespace(), 0, service.get_name(), [], [], [], [], [])
        outfile.writeln(self.get_function_prototype(control_service, unsubscribe_fn, CONST.TYPENAME_CASE_FUNCTION_CALL))
        outfile.writeln("{")
        outfile.indent_inc()
        define_function_body(control_service, unsubscribe_fn, outfile)
        outfile.indent_dec()
        outfile.writeln("}")
        outfile.writeln("")
        return

    def define_client_functions(self, service: ServiceObject, outfile: CodeWriter):
        # This actually defines the client functions implementations, to support the subscribe/unsubscribe we must
        # generate two additional functions that have special ids
        self.define_client_subscribe(service, outfile)
        self.define_client_unsubscribe(service, outfile)

        for func in service.get_functions():
            outfile.writeln(f"{self.get_function_prototype(service, func, CONST.TYPENAME_CASE_FUNCTION_CALL)} {{")
            outfile.indent_inc()
            define_function_body(service, func, outfile)
            outfile.indent_dec()
            outfile.writeln("}")
            outfile.writeln("")

            if len(func.get_response_params()) > 0:
                outfile.writeln(f"{self.get_function_status_prototype(service, func)} {{")
                outfile.indent_inc()
                define_status_body(service, func, outfile)
                outfile.indent_dec()
                outfile.writeln("}")
                outfile.writeln("")
        return

    def define_server_responses(self, service: ServiceObject, outfile: CodeWriter):
        for func in service.get_functions():
            if len(func.get_response_params()) > 0:
                outfile.writeln(self.get_response_prototype(service, func, CONST.TYPENAME_CASE_FUNCTION_RESPONSE))
                outfile.writeln("{")
                outfile.indent_inc()
                define_response_body(service, func, "MESSAGE_FLAG_RESPONSE", outfile)
                outfile.indent_dec()
                outfile.writeln("}")
                outfile.writeln("")

    def define_events(self, service: ServiceObject, outfile: CodeWriter):
        for evt in service.get_events():
            outfile.writeln(get_event_prototype_name_single(service, evt, CONST.TYPENAME_CASE_FUNCTION_CALL))
            outfile.writeln("{")
            outfile.indent_inc()
            define_event_body_single(service, evt, outfile)
            outfile.indent_dec()
            outfile.writeln("}\n")
            outfile.writeln("")

            outfile.writeln(get_event_prototype_name_all(service, evt, CONST.TYPENAME_CASE_FUNCTION_CALL))
            outfile.writeln("{")
            outfile.indent_inc()
            define_event_body_all(service, evt, outfile)
            outfile.indent_dec()
            outfile.writeln("}\n")
            outfile.writeln("")
        return

    def write_server_callback(self, service: ServiceObject, func, outfile: CodeWriter):
        outfile.write("    " + self.get_server_callback_prototype(service, func))
        outfile.write(";\n")

        if len(func.get_response_params()) > 0:
            outfile.write("    " + self.get_response_prototype(service, func, CONST.TYPENAME_CASE_FUNCTION_RESPONSE))
            outfile.write(";\n")
        return

    def write_service_event_prototype(self, service, evt, outfile):
        outfile.write("    " + get_event_prototype_name_single(service, evt,
                                                               CONST.TYPENAME_CASE_FUNCTION_CALL) + ";\n")
        outfile.write("    " + get_event_prototype_name_all(service, evt,
                                                            CONST.TYPENAME_CASE_FUNCTION_CALL) + ";\n")
        return

    def get_client_callback_prototype(self, service, evt):
        prototype = "void " + get_client_event_callback_name(service, evt) + "("
        prototype = prototype + "gracht_client_t* client"
        if len(evt.get_params()) > 0:
            prototype = prototype + ", " + get_parameter_string(service, evt.get_params(),
                                                                CONST.TYPENAME_CASE_FUNCTION_CALL, False)
        return prototype + ")"

    def write_server_response_prototypes(self, service, outfile):
        if len(service.get_functions()) > 0:
            outfile.write("    /**\n")
            outfile.write(
                "     * Response functions that can be invoked from invocations. These will format a response\n")
            outfile.write("     * message based on a message context.\n")
            outfile.write("     */\n")
            for func in service.get_functions():
                if len(func.get_response_params()) > 0:
                    outfile.write("    " + self.get_response_prototype(
                        service, func, CONST.TYPENAME_CASE_FUNCTION_RESPONSE))
                    outfile.write(";\n")
            outfile.write("\n")

    def write_server_event_prototypes(self, service, outfile):
        if len(service.get_events()) > 0:
            outfile.write("    /**\n")
            outfile.write("     * Event functions that can be invoked at any point in time.\n")
            outfile.write("     */\n")
            for evt in service.get_events():
                self.write_service_event_prototype(service, evt, outfile)
            outfile.write("\n")
        return

    def write_callback_prototypes(self, service, callbacks, outfile):
        if len(callbacks) > 0:
            outfile.write("    /**\n")
            outfile.write("     * Invocation callback prototypes that must be defined. These are the functions\n")
            outfile.write("     * that will be called when a message is received.\n")
            outfile.write("     */\n")
            for cb in callbacks:
                if isinstance(cb, EventObject):
                    outfile.write("    " + self.get_client_callback_prototype(service, cb) + ";\n")
                else:
                    outfile.write("    " + self.get_server_callback_prototype(service, cb) + ";\n")
            outfile.write("\n")

    def generate_shared_header(self, service, directory):
        file_name = service.get_namespace() + "_" + service.get_name() + "_service.h"
        file_path = os.path.join(directory, file_name)
        with open(file_path, 'w') as f:
            cout = CodeWriter(f)
            write_header(cout)
            write_header_guard_start(file_name, cout)
            define_headers(["<assert.h>", "<gracht/types.h>", "<stdint.h>", "<stdlib.h>", "<string.h>"], cout)
            define_service_headers(service, cout)
            define_shared_ids(service, cout)
            define_shared_serializers(service, cout)
            define_enums(service, cout)
            define_structures(service, cout)
            define_type_serializers(service, cout)
            define_struct_serializers(service, cout)
            write_header_guard_end(file_name, cout)
        return

    def generate_client_header(self, service, directory):
        file_name = service.get_namespace() + "_" + service.get_name() + "_service_client.h"
        file_path = os.path.join(directory, file_name)
        with open(file_path, 'w') as f:
            cout = CodeWriter(f)
            write_header(cout)
            write_header_guard_start(file_name, cout)
            define_headers(["<gracht/client.h>"], cout)
            include_shared_header(service, cout)
            write_c_guard_start(cout)
            self.write_callback_prototypes(service, service.get_events(), cout)
            self.define_prototypes(service, cout)
            self.define_client_service_extern(service, cout)
            write_c_guard_end(cout)
            write_header_guard_end(file_name, cout)
        return

    def generate_client_impl(self, service, directory):
        file_name = service.get_namespace() + "_" + service.get_name() + "_service_client.c"
        file_path = os.path.join(directory, file_name)
        with open(file_path, 'w') as f:
            cout = CodeWriter(f)
            write_header(cout)
            define_headers([
                "\"" + service.get_namespace() + "_" + service.get_name() + "_service_client.h\"",
                "<string.h>", "<stdlib.h>"], cout)
            write_client_api(service, cout)
            write_client_callback_array(service, cout)
            write_client_deserializers(service, cout)
            self.define_client_functions(service, cout)
        return

    def generate_server_header(self, service, directory):
        file_name = service.get_namespace() + "_" + service.get_name() + "_service_server.h"
        file_path = os.path.join(directory, file_name)
        with open(file_path, 'w') as f:
            cout = CodeWriter(f)
            write_header(cout)
            write_header_guard_start(file_name, cout)
            define_headers(["<gracht/server.h>"], cout)
            include_shared_header(service, cout)
            write_c_guard_start(cout)
            self.write_callback_prototypes(service, service.get_functions(), cout)
            self.write_server_response_prototypes(service, cout)
            self.write_server_event_prototypes(service, cout)
            self.define_server_service_extern(service, cout)
            write_c_guard_end(cout)
            write_header_guard_end(file_name, cout)
        return

    def generate_server_impl(self, service, directory):
        file_name = service.get_namespace() + "_" + service.get_name() + "_service_server.c"
        file_path = os.path.join(directory, file_name)
        with open(file_path, 'w') as f:
            cout = CodeWriter(f)
            write_header(cout)
            define_headers([
                "\"" + service.get_namespace() + "_" + service.get_name() + "_service_server.h\"",
                "<string.h>", "<stdlib.h>"], cout)
            write_server_api(service, cout)
            write_server_callback_array(service, cout)
            write_server_deserializers(service, cout)
            self.define_server_responses(service, cout)
            self.define_events(service, cout)
        return

    def generate_shared_files(self, out, services, include_services):
        for svc in services:
            if (len(include_services) == 0) or (svc.get_name() in include_services):
                self.generate_shared_header(svc, out)
        return

    def generate_client_files(self, out, services, include_services):
        for svc in services:
            if (len(include_services) == 0) or (svc.get_name() in include_services):
                self.generate_client_header(svc, out)
                self.generate_client_impl(svc, out)
        return

    def generate_server_files(self, out, services, include_services):
        for svc in services:
            if (len(include_services) == 0) or (svc.get_name() in include_services):
                self.generate_server_header(svc, out)
                self.generate_server_impl(svc, out)
        return

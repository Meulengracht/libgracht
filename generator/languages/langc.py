import os
from .shared import *


class CONST(object):
    __slots__ = ()
    TYPENAME_CASE_SIZEOF = 0
    TYPENAME_CASE_FUNCTION_CALL = 1
    TYPENAME_CASE_FUNCTION_STATUS = 2
    TYPENAME_CASE_FUNCTION_RESPONSE = 3
    TYPENAME_CASE_MEMBER = 4

def is_variable_value_type(service, param):
    if service.typename_is_struct(param.get_typename()):
        return False
    if param.get_is_variable():
        return False
    return True

def should_define_parameter(param, case, is_output):
    is_hidden = False

    if case == CONST.TYPENAME_CASE_SIZEOF or case == CONST.TYPENAME_CASE_MEMBER:
        return not is_output
    elif case == CONST.TYPENAME_CASE_FUNCTION_CALL:
        return not is_output and not is_hidden
    elif case == CONST.TYPENAME_CASE_FUNCTION_STATUS:
        return is_output and not is_hidden
    elif case == CONST.TYPENAME_CASE_FUNCTION_RESPONSE:
        return is_output and not is_hidden
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

def get_struct_name(namespace, struct):
    return namespace.lower() + "_" + struct.get_name().lower()

def get_enum_name(service, enum):
    return service.get_namespace().lower() + "_" + enum.get_name().lower()

def get_message_flags_func(func):
    if len(func.get_response_params()) == 0:
        return "MESSAGE_FLAG_ASYNC"
    return "MESSAGE_FLAG_SYNC"

def define_headers(headers, outfile):
    for header in headers:
        outfile.write("#include " + header + "\n")
    outfile.write("\n")
    return


def include_shared_header(service, outfile):
    outfile.write("#include \"" + service.get_namespace() + "_" + service.get_name() + "_service.h\"\n")
    return


def define_service_headers(service, outfile):
    for imp in service.get_imports():
        outfile.write("#include <" + imp + ">\n")
    outfile.write("\n")
    return

def is_param_const(param, is_output):
    if is_output:
        return False
    
    # skip reserved names
    if "gracht_message_context" in param.get_typename():
        return False
    if "gracht_client_t" in param.get_typename():
        return False
    if "gracht_recv_message" in param.get_typename():
        return False
    return True

def get_param_typename(service, param, case, is_output):
    # resolve enums and structs
    param_typename = param.get_typename().lower()
    if service.typename_is_enum(param_typename):
        param_typename = "enum " + service.get_namespace().lower() + "_" + param_typename
    elif service.typename_is_struct(param_typename):
        param_typename = "struct " + service.get_namespace().lower() + "_" + param_typename

    # convert system types
    if param_typename == "string":
        param_typename = "char*"

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
        param_typename = param_typename + " " + param.get_name()
        if param.get_is_variable() or service.typename_is_struct(param.get_typename().lower()):
            param_typename = param_typename + "*"
        return param_typename

    elif case == CONST.TYPENAME_CASE_MEMBER:
        param_typename = param_typename + " " + param.get_name()
        if param.get_is_variable():
            param_typename = param_typename + "[" + str(param.get_count()) + "]"
        return param_typename
    return param_typename


def get_parameter_string(service, params, case, is_output):
    parameters_valid = []
    for param in params:
        if should_define_parameter(param, case, is_output):
            parameters_valid.append(get_param_typename(service, param, case, is_output))
        if should_define_param_length_component(param, case):
            if param.get_typename().lower() == "string":
                length_param = VariableObject("size_t", f"{param.get_name()}_max_length", False)
            else:
                length_param = VariableObject("size_t", f"{param.get_name()}_count", False)
            parameters_valid.append(get_param_typename(service, length_param, case, False))

    return ", ".join(parameters_valid)


def get_server_service_response_name(service, func):
    return service.get_namespace() + "_" + service.get_name() + "_" + func.get_name() + "_response"


def get_service_callback_name(service, cb):
    return service.get_namespace() + "_" + service.get_name() + "_" + cb.get_name() + "_invocation"


def get_client_event_callback_name(service, evt):
    return service.get_namespace() + "_" + service.get_name() + "_event_" + evt.get_name() + "_callback"

def get_service_internal_callback_name(service, act):
    return f"__{service.get_namespace()}_{act.get_name()}_internal"

def get_event_prototype_name_single(service, evt, case):
    evt_client_param = get_param_typename(service, VariableObject("gracht_conn_t", "client", False), case, False)
    evt_name = "int " + service.get_namespace() + "_" + service.get_name() + "_event_" + evt.get_name() + "_single(" + evt_client_param

    if len(evt.get_params()) > 0:
        evt_name = evt_name + ", " + get_parameter_string(service, evt.get_params(), case, False)
    return evt_name + ")"


def get_event_prototype_name_all(service, evt, case):
    evt_name = "int " + service.get_namespace() + "_" + service.get_name() + "_event_" + evt.get_name() + "_all("

    if len(evt.get_params()) > 0:
        evt_name = evt_name + get_parameter_string(service, evt.get_params(), case, False)
    else:
        evt_name = evt_name + "void"
    return evt_name + ")"


def write_header(outfile):
    outfile.write("/**\n")
    outfile.write(
        " * This file was generated by the gracht service generator script. Any changes done here will be overwritten.\n")
    outfile.write(" */\n\n")
    return


def write_header_guard_start(file_name, outfile):
    outfile.write("#ifndef __" + str.replace(file_name, ".", "_").upper() + "__\n")
    outfile.write("#define __" + str.replace(file_name, ".", "_").upper() + "__\n\n")
    return


def write_header_guard_end(file_name, outfile):
    outfile.write("#endif //!__" + str.replace(file_name, ".", "_").upper() + "__\n")
    return


def write_c_guard_start(outfile):
    outfile.write("#ifdef __cplusplus\n")
    outfile.write("extern \"C\" {\n")
    outfile.write("#endif\n")
    return


def write_c_guard_end(outfile):
    outfile.write("#ifdef __cplusplus\n")
    outfile.write("}\n")
    outfile.write("#endif\n\n")
    return

def define_shared_ids(service, outfile):
    prefix = service.get_namespace().upper() + "_" + service.get_name().upper()
    outfile.write(f"#define SERVICE_{prefix}_ID {str(service.get_id())}\n")
    outfile.write(f"#define SERVICE_{prefix}_FUNCTION_COUNT {str(len(service.get_functions()))}\n\n")

    if len(service.get_functions()) > 0:
        for func in service.get_functions():
            func_prefix = prefix + "_" + func.get_name().upper()
            outfile.write("#define SERVICE_" + func_prefix + "_ID " + str(func.get_id()) + "\n")
        outfile.write("\n")

    if len(service.get_events()) > 0:
        for evt in service.get_events():
            evt_prefix = prefix + "_EVENT_" + evt.get_name().upper()
            outfile.write("#define SERVICE_" + evt_prefix + "_ID " + str(evt.get_id()) + "\n")
        outfile.write("\n")
    return

def write_function_body_prologue(service: ServiceObject, actionId, flags, params, isServer, outfile):
    outfile.write("    gracht_buffer_t __buffer;\n")
    outfile.write("    int             __status;\n")

    hasVariable = False
    for param in params:
        if param.get_is_variable():
            hasVariable = True

    if hasVariable:
        outfile.write("    int             __i;\n")
    outfile.write("\n")

    if isServer:
        outfile.write("    __status = gracht_server_get_buffer(&__buffer);\n")
    else:
        outfile.write("    __status = gracht_client_get_buffer(client, &__buffer);\n")
    outfile.write("    if (__status) {\n")
    outfile.write("        return __status;\n")
    outfile.write("    }\n\n")

    # build message header
    # id and length are filled out by the server/client
    outfile.write(f"    serialize_uint32_t(&__buffer, 0);\n")
    outfile.write(f"    serialize_uint32_t(&__buffer, 0);\n")
    outfile.write(f"    serialize_uint8_t(&__buffer, {str(service.get_id())});\n")
    outfile.write(f"    serialize_uint8_t(&__buffer, {str(actionId)});\n")
    outfile.write(f"    serialize_uint8_t(&__buffer, {str(flags)});\n")

    for param in params:
        if param.get_is_variable():
            outfile.write(f"    serialize_uint32_t(&__buffer, {param.get_name()}_count);\n")
            outfile.write(f"    for (__i = 0; __i < {param.get_name()}_count; __i++) ")
            outfile.write("{\n")
            outfile.write(f"        serialize_{param.get_typename()}(&__buffer, &{param.get_name()}[__i]);\n")
            outfile.write("    }\n")
        else:
            outfile.write(f"    serialize_{param.get_typename()}(&__buffer, {param.get_name()});\n")

def write_function_body_epilogue(service: ServiceObject, func: FunctionObject, outfile):
    # dunno yet
    outfile.write("    return __status;\n")

def define_function_body(service: ServiceObject, func: FunctionObject, outfile):
    flags = get_message_flags_func(func)
    write_function_body_prologue(service, func.get_id(), flags, func.get_request_params(), False, outfile)
    outfile.write("    __status = gracht_client_invoke(client, context, &__buffer);\n")
    write_function_body_epilogue(service, func, outfile)
    return

def write_status_body_prologue(service: ServiceObject, func: FunctionObject, outfile):
    outfile.write("    gracht_buffer_t __buffer;\n")
    outfile.write("    int             __status;\n")

    hasVariable = False
    for param in func.get_response_params():
        if param.get_is_variable():
            hasVariable = True
    
    if hasVariable:
        outfile.write("    int       __i;\n")
        outfile.write("    uint32_t  __count;\n")
    outfile.write("\n")

    outfile.write("    __status = gracht_client_get_status_buffer(client, context, &__buffer);\n")
    outfile.write("    if (__status != GRACHT_MESSAGE_COMPLETED) {\n")
    outfile.write("        return __status;\n")
    outfile.write("    }\n\n")

    for param in func.get_response_params():
        if param.get_is_variable():
            outfile.write("    __count = deserialize_uint32_t(&__buffer);\n")
            outfile.write(f"    for (__i = 0; __i < __count; __i++) ")
            outfile.write("{\n")
            if service.typename_is_struct(param.get_typename()):
                outfile.write(f"        deserialize_{param.get_typename()}(&__buffer, &{param.get_name()}[__i]);\n")
            elif param.get_typename().lower() == "string":
                outfile.write(f"        deserialize_string(&__buffer, {param.get_name()}_out[__i], {param.get_name()}_max_length);\n")
            else:
                outfile.write(f"        {param.get_name()}[__i] = deserialize_{param.get_typename()}(&__buffer));\n")
            outfile.write("    }\n")
        elif param.get_typename().lower() == "string":
            outfile.write(f"    deserialize_string(&__buffer, &{param.get_name()}_out[0], {param.get_name()}_max_length);\n")
        else:
            outfile.write(f"    *{param.get_name()}_out = deserialize_{param.get_typename()}(&__buffer);\n")

def write_status_body_epilogue(service: ServiceObject, func: FunctionObject, outfile):
    # dunno yet
    outfile.write("    return __status;\n")

def define_status_body(service: ServiceObject, func: FunctionObject, outfile):
    write_status_body_prologue(service, func, outfile)
    outfile.write("    __status = gracht_client_status_finalize(client, context);\n")
    write_status_body_epilogue(service, func, outfile)
    return

def define_event_body_single(service: ServiceObject, evt, outfile):
    flags = "MESSAGE_FLAG_EVENT"
    write_function_body_prologue(service, evt.get_id(), flags, evt.get_params(), True, outfile)
    outfile.write("    __status = gracht_server_send_event(client, &__buffer, 0);\n")
    write_function_body_epilogue(service, evt, outfile)
    return

def define_event_body_all(service: ServiceObject, evt, outfile):
    flags = "MESSAGE_FLAG_EVENT"
    write_function_body_prologue(service, evt.get_id(), flags, evt.get_params(), True, outfile)
    outfile.write("    __status = gracht_server_broadcast_event(&__buffer, 0);\n")
    write_function_body_epilogue(service, evt, outfile)
    return

def define_response_body(service: ServiceObject, func, flags, outfile):
    flags = "MESSAGE_FLAG_RESPONSE"
    write_function_body_prologue(service, func.get_id(), flags, func.get_response_params(), True, outfile)
    outfile.write("    __status = gracht_server_respond(message, &__buffer);\n")
    write_function_body_epilogue(service, func, outfile)
    return

def define_shared_serializers(service, outfile):
    system_types = [
        ["uint8_t", "uint8_t"],
        ["int8_t", "int8_t"], 
        ["uint16_t", "uint16_t"],
        ["int16_t", "int16_t"],
        ["uint32_t", "uint32_t"],
        ["int32_t", "int32_t"],
        ["uint64_t", "uint64_t"],
        ["int64_t", "int64_t"],
        ["uint", "unsigned int"],
        ["int", "int"],
        ["float", "float"],
        ["double", "double"]
    ]
    
    outfile.write("#ifndef GRMIN\n")
    outfile.write("#define GRMIN(a, b) (((a)<(b))?(a):(b))\n")
    outfile.write("#endif\n\n")

    outfile.write("#define SERIALIZE_VALUE(name, type) static inline void serialize_##name(gracht_buffer_t* buffer, type value) { \\\n")
    outfile.write("                                  *((type*)&buffer->data[buffer->index]) = value; buffer->index += sizeof(type); \\\n")
    outfile.write("                              }\n\n")
    outfile.write("#define DESERIALIZE_VALUE(name, type) static inline type deserialize_##name(gracht_buffer_t* buffer) { \\\n")
    outfile.write("                                  type value = *((type*)&buffer->data[buffer->index]); \\\n")
    outfile.write("                                  buffer->index += sizeof(type); \\\n")
    outfile.write("                                  return value; \\\n")
    outfile.write("                              }\n\n")

    for system_type in system_types:
        outfile.write(f"SERIALIZE_VALUE({system_type[0]}, {system_type[1]})\n")
        outfile.write(f"DESERIALIZE_VALUE({system_type[0]}, {system_type[1]})\n")

    outfile.write("\nstatic inline void serialize_string(gracht_buffer_t* buffer, const char* string) { \n")
    outfile.write("    size_t length = strlen(string);\n")
    outfile.write("    memcpy(&buffer->data[buffer->index], string, length);\n")
    outfile.write("    buffer->data[buffer->index + length] = 0;\n")
    outfile.write("    buffer->index += (length + 1);\n")
    outfile.write("}\n\n")
    outfile.write("static inline void deserialize_string_copy(gracht_buffer_t* buffer, char* out, size_t maxLength) { \n")
    outfile.write("    size_t length = strlen(&buffer->data[buffer->index]) + 1;\n")
    outfile.write("    size_t clampedLength = GRMIN(length, maxLength - 1);\n")
    outfile.write("    memcpy(out, &buffer->data[buffer->index], clampedLength);\n")
    outfile.write("    out[clampedLength] = 0;\n")
    outfile.write("    buffer->index += length;\n")
    outfile.write("}\n\n")
    outfile.write("static inline char* deserialize_string_nocopy(gracht_buffer_t* buffer) { \n")
    outfile.write("    size_t length = strlen(&buffer->data[buffer->index]) + 1;\n")
    outfile.write("    char*  string = &buffer->data[buffer->index];\n")
    outfile.write("    buffer->index += length;\n")
    outfile.write("    return string;\n")
    outfile.write("}\n\n")

    for struct in service.get_structs():
        outfile.write(f"static void serialize_{struct.get_name()}(gracht_buffer_t* buffer, struct {struct.get_name()}* in);\n")
        outfile.write(f"static void deserialize_{struct.get_name()}(gracht_buffer_t* buffer, struct {struct.get_name()}* out);\n")
    outfile.write("\n\n")

def define_struct_serializers(service, outfile):
    for struct in service.get_structs():
        has_variables = False
        for member in struct.get_members():
            if member.get_is_variable():
                has_variables = True
                break

        outfile.write(f"static void serialize_{struct.get_name()}(gracht_buffer_t* buffer, struct {struct.get_name()}* in) ")
        outfile.write("{\n")
        if has_variables:
            outfile.write("    int i;\n\n")

        for member in struct.get_members():
            if member.get_is_variable():
                outfile.write(f"    for (i = 0; i < in->{member.get_name()}_count; i++) ")
                outfile.write("    {\n")
                outfile.write(f"        serialize_{member.get_typename()}(buffer, &in->{member.get_name()}[i])\n")
                outfile.write("    }\n")
            elif service.typename_is_struct(member.get_typename()):
                outfile.write(f"    serialize_{member.get_typename()}(buffer, &in->{member.get_name()});\n")
            else:
                outfile.write(f"    serialize_{member.get_typename()}(buffer, in->{member.get_name()});\n")
        outfile.write("}\n\n")

        outfile.write(f"static void deserialize_{struct.get_name()}(gracht_buffer_t* buffer, struct {struct.get_name()}* out) ")
        outfile.write("{\n")
        if has_variables:
            outfile.write("    int i;\n\n")
        
        for member in struct.get_members():
            if member.get_is_variable():
                outfile.write(f"    out->{member.get_name()}_count = deserialize_uint32_t(buffer);\n")
                outfile.write(f"    if (out->{member.get_name()}_count) ")
                outfile.write("{\n")
                outfile.write(f"        {struct.get_name()}_{member.get_name()}_add(out, out->{member.get_name()}_count);\n")
                outfile.write(f"        for (i = 0; i < out->{member.get_name()}_count; i++) ")
                outfile.write("        {\n")
                if service.typename_is_struct(member.get_typename()):
                    outfile.write(f"            deserialize_{member.get_typename()}(buffer, &out->{member.get_name()}[i]);\n")
                else:
                    outfile.write(f"            out->{member.get_name()}[i] = deserialize_{member.get_typename()}(buffer);\n")
                outfile.write("        }\n")
                outfile.write("    }\n")
                outfile.write("    else {\n")
                outfile.write(f"        out->{member.get_name()} = NULL;\n")
                outfile.write("    }\n")
            elif member.get_typename().lower() == "string":
                outfile.write(f"    out->{member.get_name()} = malloc(strlen(&buffer->data[buffer->index]) + 1);\n")
                outfile.write(f"    deserialize_string(buffer, &out->{member.get_name()}[0], 0);\n")
            elif service.typename_is_struct(member.get_typename()):
                outfile.write(f"    deserialize_{member.get_typename()}(buffer, &out->{member.get_name()});\n")
            else:
                outfile.write(f"    out->{member.get_name()} = deserialize_{member.get_typename()}(buffer);\n")
        outfile.write("}\n\n")

def write_enum(enum_name, values, outfile):
    outfile.write("enum " + enum_name + " {\n")
    for value in values:
        if value.get_value() is None:
            outfile.write("    " + value.get_name() + ",\n")
        else:
            outfile.write("    " + value.get_name() + " = " + str(value.get_value()) + ",\n")
    outfile.write("};\n")
    return

def define_enums(service, outfile):
    if len(service.get_enums()) > 0:
        for enum in service.get_enums():
            if len(enum.get_values()):
                enum_name = get_enum_name(service, enum)
                outfile.write("#ifndef __" + enum_name.upper() + "_DEFINED__\n")
                outfile.write("#define __" + enum_name.upper() + "_DEFINED__\n")
                write_enum(enum_name, enum.get_values(), outfile)
                outfile.write("#endif //!__" + enum_name.upper() + "_DEFINED__\n\n")
        outfile.write("\n")
    return

def write_structure(service, struct_name, params, case, outfile):
    outfile.write(f"struct {struct_name} ")
    outfile.write("{\n")
    for param in params:
        outfile.write("    " + get_param_typename(service, param, case, False) + ";\n")
    outfile.write("});\n")
    return

def write_structure_functionality(service, struct, outfile):
    # write constructor
    outfile.write(f"static void {struct.get_name()}_init(struct {struct.get_name()}* in) ")
    outfile.write("{\n")
    outfile.write(f"    memset(in, 0, sizeof(struct {struct.get_name()}));\n")
    outfile.write("}\n\n")

    # write member allocators and indexors
    for member in struct.get_members():
        if member.get_is_variable():
            # write allocator
            outfile.write(f"static void {struct.get_name()}_{member.get_name()}_add(struct {struct.get_name()}* in, uint32_t count) ")
            outfile.write("{\n")
            outfile.write(f"    if (in->{member.get_name()}) ")
            outfile.write("{\n")
            outfile.write(f"        in->{member.get_name()} = realloc(in->{member.get_name()}, sizeof(struct {member.get_typename()}) * (in->{member.get_name()}_count + count));\n")
            outfile.write("    }\n")
            outfile.write("    else {\n")
            outfile.write(f"        in->{member.get_name()} = malloc(sizeof(struct {member.get_typename()}) * count);\n")
            outfile.write("    }\n\n")
            outfile.write("    for (uint32_t i = 0; i < count; i++) {\n")
            outfile.write(f"        {member.get_typename()}_init(&in->{member.get_name()}[in->{member.get_name()}_count + i]);\n")
            outfile.write("    }\n")
            outfile.write(f"    in->{member.get_name()}_count += count;\n")
            outfile.write("}\n\n")

            # write indexor
            outfile.write(f"static struct {member.get_typename()}* {struct.get_name()}_{member.get_name()}_get(struct {struct.get_name()}* in, uint32_t index) ")
            outfile.write("{\n")
            outfile.write(f"    if (index >= in->{member.get_name()}_count) ")
            outfile.write("{\n")
            outfile.write(f"        {struct.get_name()}_{member.get_name()}_add(in, (index - in->{member.get_name()}_count) + 1)\n")
            outfile.write("    }\n")
            outfile.write(f"    return &in->{member.get_name()}[index];\n")
            outfile.write("}\n\n")


    # write destructor
    outfile.write(f"static void {struct.get_name()}_destroy(struct {struct.get_name()}* in) ")
    outfile.write("{\n")
    for member in struct.get_members():
        if member.get_is_variable():
            outfile.write(f"    if (in->{member.get_name()}) ")
            outfile.write("{\n")
            outfile.write(f"        for (int i = 0; i < in->{member.get_name()}_count; i++)")
            outfile.write("{\n")
            outfile.write(f"            {member.get_typename()}_destroy(&in->{member.get_name()}[i]);\n")
            outfile.write("        }\n")
            outfile.write(f"        free(in->{member.get_name()});\n")
            outfile.write("    }\n")
    outfile.write("}\n\n")

def define_structures(service, outfile):
    for struct in service.get_structs():
        write_structure(service, get_struct_name(service.get_namespace(), struct), struct.get_members(), CONST.TYPENAME_CASE_MEMBER, outfile)
        outfile.write("\n")
        write_structure_functionality(service, struct, outfile)

def write_client_api(service, outfile):
    outfile.write("extern int gracht_client_get_buffer(gracht_client_t*, gracht_buffer_t*);\n")
    outfile.write("extern int gracht_client_get_status_buffer(gracht_client_t*, struct gracht_message_context*, gracht_buffer_t*);\n")
    outfile.write("extern int gracht_client_status_finalize(gracht_client_t*, struct gracht_message_context*);\n")
    outfile.write("extern int gracht_client_invoke(gracht_client_t*, struct gracht_message_context*, gracht_buffer_t*);\n\n")
    outfile.write("\n")

def write_server_api(service, outfile):
    outfile.write("extern int gracht_server_get_buffer(gracht_buffer_t*);\n")
    outfile.write("extern int gracht_server_respond(struct gracht_recv_message*, gracht_buffer_t*);\n")
    outfile.write("extern int gracht_server_send_event(gracht_conn_t client, gracht_buffer_t*, unsigned int flags);\n")
    outfile.write("extern int gracht_server_broadcast_event(gracht_buffer_t*, unsigned int flags);\n")
    outfile.write("\n")

# Define the client callback array - this is the one that will be registered with the client
# and handles the delegation of deserializing of incoming events.
def write_client_callback_array(service: ServiceObject, outfile):
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
    outfile.write(f"void {get_service_internal_callback_name(service, evt)}(gracht_client_t* __client, gracht_buffer_t* __buffer)")

def write_client_deserializer_body(service: ServiceObject, evt: EventObject, outfile):
    outfile.write("{\n")

    # write definitions
    for param in evt.get_params():
        if param.get_typename().lower() == "string":
            outfile.write(f"    char* __{param.get_name()};\n")
        else:
            outfile.write(f"    {param.get_typename()} __{param.get_name()};\n")

    # write deserializer calls
    for param in evt.get_params():
        if param.get_typename().lower() == "string":
            outfile.write(f"    __{param.get_name()} = deserialize_{param.get_typename()}_nocopy(__buffer);\n")
        else:
            outfile.write(f"    __{param.get_name()} = deserialize_{param.get_typename()}(__buffer);\n")

    # write invocation line
    outfile.write(f"    {get_service_callback_name(service, evt)}(__client")
    for index, param in enumerate(evt.get_params()):
        if index == 0:
            outfile.write(", ")
        outfile.write(f"__{param.get_name()}")
        if index < (len(evt.get_params()) - 1):
            outfile.write(", ")
    outfile.write(");\n")
    outfile.write("}\n\n")

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
    outfile.write(f"void {get_service_internal_callback_name(service, func)}(struct gracht_recv_message* __message, gracht_buffer_t* __buffer)")

def write_server_deserializer_body(service: ServiceObject, func: FunctionObject, outfile):
    outfile.write("{\n")

    # write definitions
    for param in func.get_request_params():
        if param.get_typename().lower() == "string":
            outfile.write(f"    char* __{param.get_name()};\n")
        else:
            outfile.write(f"    {param.get_typename()} __{param.get_name()};\n")

    # write deserializer calls
    for param in func.get_request_params():
        if param.get_typename().lower() == "string":
            outfile.write(f"    __{param.get_name()} = deserialize_{param.get_typename()}_nocopy(__buffer);\n")
        else:
            outfile.write(f"    __{param.get_name()} = deserialize_{param.get_typename()}(__buffer);\n")

    # write invocation line
    outfile.write(f"    {get_service_callback_name(service, func)}(__message")
    for index, param in enumerate(func.get_request_params()):
        if index == 0:
            outfile.write(", ")
        outfile.write(f"__{param.get_name()}")
        if index < (len(func.get_request_params()) - 1):
            outfile.write(", ")
    outfile.write(");\n")
    outfile.write("}\n\n")


class CGenerator:
    def get_server_callback_prototype(self, service, func):
        function_prototype = "void " + get_service_callback_name(service, func) + "("
        function_message_param = get_param_typename(service, VariableObject("struct gracht_recv_message*", "message", False),
                                                    CONST.TYPENAME_CASE_FUNCTION_CALL, False)
        parameter_string = function_message_param
        if len(func.get_request_params()) > 0:
            parameter_string = parameter_string + ", " + get_parameter_string(service, func.get_request_params(), 
                CONST.TYPENAME_CASE_FUNCTION_CALL, False)
        return function_prototype + parameter_string + ")"
    
    def get_response_prototype(self, service, func, case):
        function_prototype = "int " + get_server_service_response_name(service, func) + "("
        function_message_param = get_param_typename(service, VariableObject("struct gracht_recv_message*", "message", False),
                                                    case, False)
        parameter_string = function_message_param + ", "
        parameter_string = parameter_string + get_parameter_string(service, func.get_response_params(), case, True)
        return function_prototype + parameter_string + ")"

    def get_function_prototype(self, service, func, case):
        function_prototype = "int " + service.get_namespace().lower() + "_" \
                             + service.get_name().lower() + "_" + func.get_name()
        function_client_param = get_param_typename(service, VariableObject("gracht_client_t*", "client", False), case, False)
        function_context_param = get_param_typename(service,
                                                    VariableObject("struct gracht_message_context*", "context", False), case, False)
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
        outfile.write(f"    extern gracht_protocol_t {service.get_namespace()}_{service.get_name()}_client_protocol;\n\n")

    def define_server_service_extern(self, service, outfile):
        if len(service.get_functions()) == 0:
            return
        outfile.write(f"    extern gracht_protocol_t {service.get_namespace()}_{service.get_name()}_server_protocol;\n\n")

    def define_client_subscribe_prototype(self, service, outfile):
        subscribe_arg = VariableObject("uint8_t", "service", False, "1", str(service.get_id()))
        subscribe_fn = FunctionObject("subscribe", 0, [subscribe_arg], [])
        control_service = ServiceObject(service.get_namespace(), 0, service.get_name(), [], [], [], [], [])
        outfile.write("    " +
                      self.get_function_prototype(control_service,
                                                  subscribe_fn, CONST.TYPENAME_CASE_FUNCTION_CALL) + ";\n")
        return

    def define_client_subscribe(self, service, outfile):
        subscribe_arg = VariableObject("uint8_t", "service", False, "1", str(service.get_id()))
        subscribe_fn = FunctionObject("subscribe", 0, [subscribe_arg], [])
        control_service = ServiceObject(service.get_namespace(), 0, service.get_name(), [], [], [], [], [])
        outfile.write(
            self.get_function_prototype(control_service, subscribe_fn, CONST.TYPENAME_CASE_FUNCTION_CALL) + "\n")
        outfile.write("{\n")
        define_function_body(control_service, subscribe_fn, outfile)
        outfile.write("}\n\n")
        return

    def define_client_unsubscribe_prototype(self, service, outfile):
        unsubscribe_arg = VariableObject("uint8_t", "service", False, "1", str(service.get_id()))
        unsubscribe_fn = FunctionObject("unsubscribe", 1, [unsubscribe_arg], [])
        control_service = ServiceObject(service.get_namespace(), 0, service.get_name(), [], [], [], [], [])
        outfile.write("    " +
                      self.get_function_prototype(control_service,
                                                  unsubscribe_fn, CONST.TYPENAME_CASE_FUNCTION_CALL) + ";\n")
        return

    def define_client_unsubscribe(self, service, outfile):
        unsubscribe_arg = VariableObject("uint8_t", "service", False, "1", str(service.get_id()))
        unsubscribe_fn = FunctionObject("unsubscribe", 1, [unsubscribe_arg], [])
        control_service = ServiceObject(service.get_namespace(), 0, service.get_name(), [], [], [], [], [])
        outfile.write(
            self.get_function_prototype(control_service, unsubscribe_fn, CONST.TYPENAME_CASE_FUNCTION_CALL) + "\n")
        outfile.write("{\n")
        define_function_body(control_service, unsubscribe_fn, outfile)
        outfile.write("}\n\n")
        return

    def define_client_functions(self, service, outfile):
        # This actually defines the client functions implementations, to support the subscribe/unsubscribe we must
        # generate two additional functions that have special ids
        self.define_client_subscribe(service, outfile)
        self.define_client_unsubscribe(service, outfile)

        for func in service.get_functions():
            outfile.write(self.get_function_prototype(service, func, CONST.TYPENAME_CASE_FUNCTION_CALL) + "\n")
            outfile.write("{\n")
            define_function_body(service, func, outfile)
            outfile.write("}\n\n")

            if len(func.get_response_params()) > 0:
                outfile.write(
                    self.get_function_status_prototype(service, func) + "\n")
                outfile.write("{\n")
                define_status_body(service, func, outfile)
                outfile.write("}\n\n")
        return

    def define_server_responses(self, service, outfile):
        for func in service.get_functions():
            if len(func.get_response_params()) > 0:
                outfile.write(self.get_response_prototype(service, func, CONST.TYPENAME_CASE_FUNCTION_RESPONSE) + "\n")
                outfile.write("{\n")
                define_response_body(service, func, "MESSAGE_FLAG_RESPONSE", outfile)
                outfile.write("}\n\n")

    def define_events(self, service, outfile):
        for evt in service.get_events():
            outfile.write(
                get_event_prototype_name_single(service, evt, CONST.TYPENAME_CASE_FUNCTION_CALL) + "\n")
            outfile.write("{\n")
            define_event_body_single(service, evt, outfile)
            outfile.write("}\n\n")

            outfile.write(
                get_event_prototype_name_all(service, evt, CONST.TYPENAME_CASE_FUNCTION_CALL) + "\n")
            outfile.write("{\n")
            define_event_body_all(service, evt, outfile)
            outfile.write("}\n\n")
        return

    def write_server_callback(self, service, func, outfile):
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
            outfile.write("     * Response functions that can be invoked from invocations. These will format a response\n")
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
        if len(service.get_functions()) > 0:
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
            write_header(f)
            write_header_guard_start(file_name, f)
            define_headers(["<gracht/types.h>", "<string.h>", "<stdint.h>"], f)
            define_service_headers(service, f)
            define_shared_ids(service, f)
            define_shared_serializers(service, f)
            define_enums(service, f)
            define_structures(service, f)
            define_struct_serializers(service, f)
            write_header_guard_end(file_name, f)
        return

    def generate_client_header(self, service, directory):
        file_name = service.get_namespace() + "_" + service.get_name() + "_service_client.h"
        file_path = os.path.join(directory, file_name)
        with open(file_path, 'w') as f:
            write_header(f)
            write_header_guard_start(file_name, f)
            define_headers(["<gracht/client.h>"], f)
            include_shared_header(service, f)
            write_c_guard_start(f)
            self.write_callback_prototypes(service, service.get_events(), f)
            self.define_prototypes(service, f)
            self.define_client_service_extern(service, f)
            write_c_guard_end(f)
            write_header_guard_end(file_name, f)
        return

    def generate_client_impl(self, service, directory):
        file_name = service.get_namespace() + "_" + service.get_name() + "_service_client.c"
        file_path = os.path.join(directory, file_name)
        with open(file_path, 'w') as f:
            write_header(f)
            define_headers([
                "\"" + service.get_namespace() + "_" + service.get_name() + "_service_client.h\"",
                "<string.h>"], f)
            write_client_api(service, f)
            write_client_callback_array(service, f)
            write_client_deserializers(service, f)
            self.define_client_functions(service, f)
        return

    def generate_server_header(self, service, directory):
        file_name = service.get_namespace() + "_" + service.get_name() + "_service_server.h"
        file_path = os.path.join(directory, file_name)
        with open(file_path, 'w') as f:
            write_header(f)
            write_header_guard_start(file_name, f)
            define_headers(["<gracht/server.h>"], f)
            include_shared_header(service, f)
            write_c_guard_start(f)
            self.write_callback_prototypes(service, service.get_functions(), f)
            self.write_server_response_prototypes(service, f)
            self.write_server_event_prototypes(service, f)
            self.define_server_service_extern(service, f)
            write_c_guard_end(f)
            write_header_guard_end(file_name, f)
        return

    def generate_server_impl(self, service, directory):
        file_name = service.get_namespace() + "_" + service.get_name() + "_service_server.c"
        file_path = os.path.join(directory, file_name)
        with open(file_path, 'w') as f:
            write_header(f)
            define_headers([
                "\"" + service.get_namespace() + "_" + service.get_name() + "_service_server.h\"",
                "<string.h>"], f)
            write_server_api(service, f)
            write_server_callback_array(service, f)
            write_server_deserializers(service, f)
            self.define_server_responses(service, f)
            self.define_events(service, f)
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

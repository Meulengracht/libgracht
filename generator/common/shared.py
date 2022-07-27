BUILTIN_TYPES = [
        "uint8", "int8",
        "uint16", "int16",
        "uint32", "int32",
        "uint64", "int64",
        "uint", "int",
        "long", "ulong",
        "bool",
        "string",
        "float",
        "double"
]

class ValueDefinition:
    def __init__(self, name, value):
        self.name = name
        self.value = value

    def get_name(self):
        return self.name

    def get_value(self):
        return self.value


class VariableObject:
    def __init__(self, typename, name, is_variable, count=1, default_value=None, fixed=False):
        self.name = name
        self.typename = typename
        self.is_variable = is_variable
        self.count = count
        self.default_value = default_value
        self.fixed = fixed

    def get_name(self):
        return self.name

    def get_typename(self):
        return self.typename

    def get_count(self):
        return self.count

    def get_default_value(self):
        return self.default_value

    def get_is_variable(self):
        return self.is_variable

    def get_fixed(self):
        return self.fixed

class VariableVariantObject:
    def __init__(self, name, entries):
        self.name = name
        self.entries = entries

    def get_name(self):
        return self.name

    def get_entries(self):
        return self.entries

class TypeDefinition:
    def __init__(self, source, namespace, type_name, type_source):
        self.source = source
        self.namespace = namespace
        self.type_name = type_name
        self.type_source = type_source

    def get_source(self):
        return self.source

    def get_namespace(self):
        return self.namespace

    def get_type_name(self):
        return self.type_name

    def get_type_source(self):
        return self.type_source


class EnumObject:
    def __init__(self, source, namespace, name, values):
        self.source = source
        self.namespace = namespace
        self.name = name
        self.values = values

    def get_source(self):
        return self.source

    def get_namespace(self):
        return self.namespace

    def get_name(self):
        return self.name

    def get_values(self):
        return self.values


class StructureObject:
    def __init__(self, source, namespace, name, members):
        self.source = source
        self.namespace = namespace
        self.name = name
        self.members = members

    def set_members(self, members):
        self.members = members

    def get_source(self):
        return self.source

    def get_namespace(self):
        return self.namespace

    def get_name(self):
        return self.name

    def get_members(self):
        return self.members


class EventObject:
    def __init__(self, name, id, params):
        self.name = name
        self.id = id
        self.params = params

    def get_name(self):
        return self.name

    def get_id(self):
        return self.id

    def get_params(self):
        return self.params


class FunctionObject:
    def __init__(self, name, id, request_params, response_params):
        self.name = name
        self.id = id
        self.request_params = request_params
        self.response_params = response_params

    def get_name(self):
        return self.name

    def get_id(self):
        return self.id

    def get_request_params(self):
        return self.request_params

    def get_response_params(self):
        return self.response_params


class ServiceObject:
    def __init__(self, namespace, serviceId, name, types, enums, structs, functions, events):
        self.namespace = namespace
        self.name = name
        self.id = serviceId
        self.types = types
        self.enums = enums
        self.functions = functions
        self.events = events
        self.structs = structs
        self.imports = []
        
    def typename_is_enum(self, typename):
        for enum in self.enums:
            if enum.get_name().lower() == typename.lower():
                return True
        return False

    def lookup_enum(self, typename):
        for enum in self.enums:
            if enum.get_name().lower() == typename.lower():
                return enum
        return None

    def typename_is_struct(self, typename):
        for struct in self.structs:
            if struct.get_name().lower() == typename.lower():
                return True
        return False

    def lookup_struct(self, typename):
        for struct in self.structs:
            if struct.get_name().lower() == typename.lower():
                return struct
        return None

    def typename_is_builtin(self, typename):
        return typename in BUILTIN_TYPES

    def typename_is_enum(self, typename):
        for enum in self.enums:
            if enum.get_name().lower() == typename.lower():
                return True
        return False

    def typename_is_struct(self, typename):
        for struct in self.structs:
            if struct.get_name().lower() == typename.lower():
                return True
        return False

    def set_imports(self, imports):
        self.imports = imports

    def get_namespace(self):
        return self.namespace

    def get_name(self):
        return self.name

    def get_id(self):
        return self.id

    def get_types(self):
        return self.types

    def get_imports(self):
        return self.imports

    def get_enums(self):
        return self.enums

    def get_structs(self):
        return self.structs

    def get_functions(self):
        return self.functions

    def get_events(self):
        return self.events

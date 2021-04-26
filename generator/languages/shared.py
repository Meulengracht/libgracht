class TypeDefinition:
    def __init__(self, typeName, sourceName):
        self.name = typeName
        self.source = sourceName

    def get_name(self):
        return self.name

    def get_source(self):
        return self.source

class ValueDefinition:
    def __init__(self, name, value):
        self.name = name
        self.value = value

    def get_name(self):
        return self.name

    def get_value(self):
        return self.value

class EnumObject:
    def __init__(self, name, values):
        self.name = name
        self.values = values

    def get_name(self):
        return self.name

    def get_values(self):
        return self.values

class VariableObject:
    def __init__(self, typename, name, is_variable, count=1, default_value=None):
        self.name = name
        self.typename = typename
        self.is_variable = is_variable
        self.count = count
        self.default_value = default_value

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

class StructureObject:
    def __init__(self, name, members):
        self.name = name
        self.members = members

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
    def __init__(self, namespace, id, name, types, enums, structs, functions, events):
        self.namespace = namespace
        self.name = name
        self.id = id
        self.types = types
        self.enums = enums
        self.functions = functions
        self.events = events
        self.structs = structs
        self.imports = []
        self.resolve_all_types()

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

    def resolve_type(self, param):
        value_type = [x for x in self.types if x.get_name().lower() == param.get_typename().lower()]
        if len(value_type):
            self.imports.append(value_type[0].get_source())

    def resolve_all_types(self):
        for func in self.functions:
            for param in func.get_request_params():
                self.resolve_type(param)
            for param in func.get_response_params():
                self.resolve_type(param)
        for evt in self.events:
            for param in evt.get_params():
                self.resolve_type(param)
        for struct in self.structs:
            for member in struct.get_members():
                self.resolve_type(member)

        unique_imports = set(self.imports)
        self.imports = list(unique_imports)
        return

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

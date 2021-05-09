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

    def validate(self):
        # validet the id of the protocol itself
        if self.id < 1 or self.id > 255:
            raise ValueError(f"The id of service {self.name} must be in range of 1..255")

        # we want to validate the ids of our functions and events that
        # they are in range of 1-255 and that there are no conflicts
        ids_parsed = []
        for func in self.functions:
            if func.get_id() in ids_parsed:
                raise ValueError(f"The id of function {func.get_name()} ({func.get_id()}) is already in use")
            if func.get_id() < 1 or func.get_id() > 255:
                raise ValueError(f"The id of function {func.get_name()} must be in range of 1..255")
            ids_parsed.append(func.get_id())
        for evt in self.events:
            if evt.get_id() in ids_parsed:
                raise ValueError(f"The id of event {evt.get_name()} ({evt.get_id()}) is already in use")
            if evt.get_id() < 1 or evt.get_id() > 255:
                raise ValueError(f"The id of event {evt.get_name()} must be in range of 1..255")
            ids_parsed.append(evt.get_id())

        # also resolve all external types to gather a list of imports or usings
        self.resolve_all_types()

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

    def resolve_type(self, param):
        value_type = [x for x in self.types if x.get_type_name().lower() == param.get_typename().lower()]
        if len(value_type):
            self.imports.append(value_type[0].get_type_source())

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

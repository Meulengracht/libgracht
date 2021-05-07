#!/usr/bin/env python
import argparse
import copy
import os
import sys

from languages.langc import CGenerator
from languages.shared import *

trace_enabled = 0

class TOKENS(object):
    __slots__ = ()
    IDENTIFIER = 0
    DIGIT = 1
    LBRACKET = 2
    RBRACKET = 3
    LPARENTHESIS = 4
    RPARENTHESIS = 5
    LINDEX = 6
    RINDEX = 7
    EQUAL = 8
    COMMA = 9
    COLON = 10
    SEMICOLON = 11
    STRUCT = 12
    ENUM = 13
    SERVICE = 14
    FUNC = 15
    DEFINE = 16
    NAMESPACE = 17
    EVENT = 18
    IMPORT = 19
    QUOTE = 20
    FROM = 21

class Token:
    def __init__(self, scanner, tokenType, value = None):
        trace(f"found {str(tokenType)} at line {scanner.line_no()}:{scanner.line_index()} == {value}")
        self.lineNo = scanner.line_no()
        self.lineIndex = scanner.line_index()
        self.tokenType = tokenType
        self.tokenValue = value

    def token_type(self):
        return self.tokenType

    def value(self):
        return self.tokenValue

    def line_no(self):
        return self.lineNo

    def line_index(self):
        return self.lineIndex

    def __str__(self):
        return str(self.tokenType)
    def __repr__(self):
        return str(self.tokenType)

class ScanContext:
    def __init__(self):
        self.findex = 0
        self.lineIndex = 0
        self.lineNo = 0
    
    def consume(self, cnt = 1):
        self.findex += cnt
        self.lineIndex += cnt
        return self.index
    
    def nextline(self):
        self.lineNo += 1
        self.lineIndex = 0
        self.findex += 1

    def index(self):
        return self.findex

    def line_index(self):
        return self.lineIndex

    def line_no(self):
        return self.lineNo

class ParseContext:
    def __init__(self, root_file):
        self.cwd = os.path.dirname(os.path.realpath(root_file))
        self.namespace = ""
        self.types = []
        self.services = []
        self.enums = []
        self.structs = []

        # per service arrays
        self.funcs = []
        self.events = []

        # temporary arrays
        self.members = []
        self.values = []

        # things that are per-file contexts
        self.stored_cwds = []
    
    def set_namespace(self, namespace):
        self.namespace = namespace

    def create_service(self, id, name):
        self.services.append(ServiceObject(self.namespace, id, name, self.types.copy(), 
            self.enums.copy(), self.structs.copy(), self.funcs.copy(), self.events.copy()))
        self.funcs = []
        self.events = []
        return

    def create_function(self, id, name, request_params, response_params):
        self.funcs.append(FunctionObject(name, id, request_params, response_params))
        return

    def create_event(self, id, name, params):
        self.events.append(EventObject(name, id, params))
        return

    def create_member(self, typeName, name, is_variable, count=1):
        self.members.append(VariableObject(typeName, name, is_variable, count))
        return

    def create_define(self, typeName, sourceName):
        self.types.append(TypeDefinition(typeName, sourceName))
        return

    def create_value(self, name, value):
        self.values.append(ValueDefinition(name, value))
        return

    def create_enum(self, name):
        self.enums.append(EnumObject(name, self.values.copy()))
        self.values = []
        return

    def create_struct(self, name, members):
        self.structs.append(StructureObject(name, members))
        return

    def finish_members(self):
        values = self.members
        self.members = []
        return values

    def push_cwd(self, sub_file):
        self.stored_cwds.append(self.cwd)
        self.cwd = os.path.dirname(os.path.realpath(os.path.join(self.cwd, sub_file)))

    def pop_cwd(self):
        self.cwd = self.stored_cwds.pop()

    def get_services(self):
        return self.services

def get_global_scope_syntax():
    syntax = {
        # import "<file>"
        ("import", handle_import): [TOKENS.IMPORT, TOKENS.QUOTE],

        # namespace <identifier>;
        ("namespace", handle_namespace): [TOKENS.NAMESPACE, TOKENS.IDENTIFIER],

        # define <identifier> from "<using>"
        ("define", handle_define): [TOKENS.DEFINE, TOKENS.IDENTIFIER, TOKENS.FROM, TOKENS.QUOTE],

        # enum <identifier> { EXPRESSION }
        ("enum", handle_enum): [TOKENS.ENUM, TOKENS.IDENTIFIER, TOKENS.LBRACKET, -1, TOKENS.RBRACKET],

        # struct <identifier> { EXPRESSION }
        ("struct", handle_struct): [TOKENS.STRUCT, TOKENS.IDENTIFIER, TOKENS.LBRACKET, -1, TOKENS.RBRACKET],

        # service <identifer> (<digit>) {
        ("service", handle_service): [TOKENS.SERVICE, TOKENS.IDENTIFIER, TOKENS.LPARENTHESIS, TOKENS.DIGIT, TOKENS.RPARENTHESIS, TOKENS.LBRACKET]
    }
    return syntax

def get_service_scope_syntax():
    syntax = {
        # func <identifier>(EXPRESSION) : (EXPRESSION) = <DIGIT>;
        ("func", handle_func): [TOKENS.FUNC, TOKENS.IDENTIFIER, TOKENS.LPARENTHESIS, -1, TOKENS.RPARENTHESIS, 
                                TOKENS.COLON, TOKENS.LPARENTHESIS, -1, TOKENS.RPARENTHESIS,
                                TOKENS.EQUAL, TOKENS.DIGIT, TOKENS.SEMICOLON],

        # event <identifier> : (EXPRESSION) = <DIGIT>;
        ("vevent", handle_event): [TOKENS.EVENT, TOKENS.IDENTIFIER, TOKENS.COLON, TOKENS.LPARENTHESIS, -1, 
                                   TOKENS.RPARENTHESIS, TOKENS.EQUAL, TOKENS.DIGIT, TOKENS.SEMICOLON],
        
        # event <identifier> : <identifier> = <DIGIT>;
        ("event", handle_event): [TOKENS.EVENT, TOKENS.IDENTIFIER, TOKENS.COLON, TOKENS.IDENTIFIER,
                                  TOKENS.EQUAL, TOKENS.DIGIT, TOKENS.SEMICOLON],
    }
    return syntax

def get_enum_scope_syntax():
    syntax = {
        # <identifier> = identifier|digit,
        ("enum_mem0", handle_enum_member_with_value): [TOKENS.IDENTIFIER, TOKENS.EQUAL, [TOKENS.IDENTIFIER, TOKENS.DIGIT], [TOKENS.COMMA, TOKENS.RBRACKET]],
        
        # <identifier> }|,
        ("enum_mem1", handle_enum_member): [TOKENS.IDENTIFIER, [TOKENS.COMMA, TOKENS.RBRACKET]]
    }
    return syntax

def get_struct_scope_syntax():
    syntax = {
        # <identifier> <identifier>;
        ("struct_member0", handle_struct_member): [TOKENS.IDENTIFIER, TOKENS.IDENTIFIER, TOKENS.SEMICOLON],
        
        # <identifier>[] <identifier>;
        ("struct_member1", handle_struct_member): [TOKENS.IDENTIFIER, TOKENS.LINDEX, TOKENS.RINDEX, TOKENS.IDENTIFIER, TOKENS.SEMICOLON]
    }
    return syntax

def get_param_scope_syntax():
    syntax = {
        # <identifier> <identifier> )|,
        ("param", handle_param): [TOKENS.IDENTIFIER, TOKENS.IDENTIFIER, [TOKENS.RPARENTHESIS, TOKENS.COMMA]],
        
        # <identifier>[] <identifier> )|,
        ("vparam", handle_param): [TOKENS.IDENTIFIER, TOKENS.LINDEX, TOKENS.RINDEX, TOKENS.IDENTIFIER, [TOKENS.RPARENTHESIS, TOKENS.COMMA]]
    }
    return syntax

def get_keywords():
    keywords = {
        "import": TOKENS.IMPORT,
        "namespace": TOKENS.NAMESPACE,
        "service": TOKENS.SERVICE,
        "enum": TOKENS.ENUM,
        "define": TOKENS.DEFINE,
        "struct": TOKENS.STRUCT,
        "func": TOKENS.FUNC,
        "event": TOKENS.EVENT,
        "from": TOKENS.FROM
    }
    return keywords

def get_system_types():
    types = [
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
    return types


def error(text):
    print(f"ENCOUNTERED PARSE ERROR: {text}")
    sys.exit(-1)


def trace(text):
    if trace_enabled != 0:
        print(text)

def str2int(v):
    try:
        i = int(v, 0)
        return i
    except ValueError:
        return None
    except Exception:
        return None

def get_dir_or_default(path):
    if not path or not os.path.isdir(path):
        return os.getcwd()
    return path

def next_n_char(data, index):
    if len(data) > index:
        return data[index]
    return '\0'

def element_is_comment_line(data, index):
    return data[index] == '/' and next_n_char(data, index + 1) == '/'
    
def element_is_comment_block(data, index):
    return data[index] == '/' and next_n_char(data, index + 1) == '*'

def skip_line(scanner, data):
    while scanner.index() < len(data):
        if data[scanner.index()] == '\n':
            scanner.nextline()
            break
        scanner.consume()

def skip_until(scanner, data, marker):
    while scanner.index() < len(data):
        if data[scanner.index()] == '\n':
            scanner.nextline()
            continue

        if data[scanner.index()] == marker[0]:
            found = True
            for j, el in enumerate(marker):
                if next_n_char(data, scanner.index() + j) != el:
                    found = False
                    break
            if found:
                scanner.consume(len(marker))
                return
        scanner.consume()

def handle_import(context, tokens):
    fileToImport = tokens[1].value()
    if ".gr" not in fileToImport:
        fileToImport += ".gr"

    trace(f"importing {fileToImport}")
    context.push_cwd(fileToImport)
    parse_file(context, os.path.join(context.cwd, fileToImport))
    context.pop_cwd()

    tokens.pop(0) # consume IMPORT
    tokens.pop(0) # consume QUOTE

def handle_namespace(context, tokens):
    namespace = tokens[1].value()
    trace(f"using namespace {namespace}")
    tokens.pop(0) # consume NAMESPACE
    tokens.pop(0) # consume IDENTIFIER
    context.set_namespace(namespace)

def handle_define(context, tokens):
    typeName = tokens[1].value()
    sourceName = tokens[3].value()
    trace(f"defining {typeName} from {sourceName}")
    context.create_define(typeName, sourceName)
    tokens.pop(0) # consume DEFINE
    tokens.pop(0) # consume IDENTIFIER
    tokens.pop(0) # consume FROM
    tokens.pop(0) # consume QUOTE

def handle_service(context, tokens):
    name = tokens[1].value()
    service_id = tokens[3].value()
    trace(f"parsing service {name} ({service_id})")

    tokens.pop(0) # consume SERVICE
    tokens.pop(0) # consume IDENTIFIER
    tokens.pop(0) # consume LPARENTHESIS
    tokens.pop(0) # consume DIGIT
    tokens.pop(0) # consume RPARENTHESIS
    tokens.pop(0) # consume LBRACKET

    if tokens[0].token_type() != TOKENS.RBRACKET:
        # create sublist
        rbracket = next((x for x in tokens if x.token_type() == TOKENS.RBRACKET), None)
        if rbracket is None:
            error("expected '}' at end of struct")
        endOfStruct = tokens.index(rbracket)
        parse_scope(context, tokens[:endOfStruct], get_service_scope_syntax())
        del tokens[:endOfStruct]
    
    tokens.pop(0) # consume RBRACKET
    context.create_service(service_id, name)

def handle_param(context, tokens):
    # TOKENS.IDENTIFIER, TOKENS.LINDEX, TOKENS.RINDEX, TOKENS.IDENTIFIER, TOKENS.COMMA
    typeName = tokens[0].value()
    isVariable = False

    tokens.pop(0) # consume IDENTIFIER
    if tokens[0].token_type() == TOKENS.LINDEX:
        tokens.pop(0) # consume LINDEX
        tokens.pop(0) # consume RINDEX
        isVariable = True
    name = tokens[0].value()
    tokens.pop(0) # consume IDENTIFIER
    tokens.pop(0) # consume COMMA/RPARENTHESIS

    context.create_member(typeName, name, isVariable)

def handle_func(context, tokens):
    requestMembers = []
    responseMembers = []

    name = tokens[1].value()
    trace(f"parsing function {name}")
    
    tokens.pop(0) # consume FUNC
    tokens.pop(0) # consume IDENTIFIER
    tokens.pop(0) # consume LPARENTHESIS

    trace("parsing function parameters")
    if tokens[0].token_type() != TOKENS.RPARENTHESIS:
        # create sublist
        rparen = next((x for x in tokens if x.token_type() == TOKENS.RPARENTHESIS), None)
        if rparen is None:
            error("expected ')' at end of parameter list")
        endOfStruct = tokens.index(rparen) + 1
        parse_scope(context, tokens[:endOfStruct], get_param_scope_syntax())
        del tokens[:endOfStruct]
    else:
        tokens.pop(0) # consume RPARENTHESIS
    requestMembers = context.finish_members()

    tokens.pop(0) # consume COLON
    tokens.pop(0) # consume LPARENTHESIS

    trace("parsing function return values")
    if tokens[0].token_type() != TOKENS.RPARENTHESIS:
        # create sublist
        rparen = next((x for x in tokens if x.token_type() == TOKENS.RPARENTHESIS), None)
        if rparen is None:
            error("expected ')' at end of parameter list")
        endOfStruct = tokens.index(rparen) + 1
        parse_scope(context, tokens[:endOfStruct], get_param_scope_syntax())
        del tokens[:endOfStruct]
    else:
        tokens.pop(0) # consume RPARENTHESIS
    
    tokens.pop(0) # consume EQUAL
    actionId = tokens[0].value()
    tokens.pop(0) # consume DIGIT
    tokens.pop(0) # consume SEMICOLON
    responseMembers = context.finish_members()

    context.create_function(actionId, name, requestMembers, responseMembers)

def handle_event(context, tokens):
    members = []
    
    name = tokens[1].value()
    trace(f"parsing event {name}")
    
    tokens.pop(0) # consume EVENT
    tokens.pop(0) # consume IDENTIFIER
    tokens.pop(0) # consume COLON

    if tokens[0].token_type() == TOKENS.IDENTIFIER:
        context.create_member(tokens[0].value(), tokens[0].value(), False)
        tokens.pop(0) # consume IDENTIFIER
    else:
        # create sublist
        tokens.pop(0) # consume LPARENTHESIS
        rparen = next((x for x in tokens if x.token_type() == TOKENS.RPARENTHESIS), None)
        if rparen is None:
            error("expected ')' at end of parameter list")
        endOfParams = tokens.index(rparen) + 1
        parse_scope(context, tokens[:endOfParams], get_param_scope_syntax())
        del tokens[:endOfParams]

    tokens.pop(0) # consume EQUAL
    actionId = tokens[0].value()
    tokens.pop(0) # consume DIGIT
    tokens.pop(0) # consume SEMICOLON
    members = context.finish_members()
    context.create_event(actionId, name, members)

def handle_enum_member_with_value(context, tokens):
    trace(f"enum member {tokens[0].value()}: {tokens[2].value()}")
    name = tokens[0].value()
    value = tokens[2].value()
    
    tokens.pop(0) # consume IDENTIFIER
    tokens.pop(0) # consume EQUAL
    tokens.pop(0) # consume IDENTIFIER|DIGIT
    if tokens[0].token_type() == TOKENS.COMMA:
        tokens.pop(0) # consume COMMA
    if tokens[0].token_type() == TOKENS.RBRACKET:
        tokens.pop(0) # consume RBRACKET
    context.create_value(name, value)
    return

def handle_enum_member(context, tokens):
    trace(f"enum member {tokens[0].value()}")
    name = tokens[0].value()
    value = None

    tokens.pop(0) # consume IDENTIFIER
    if tokens[0].token_type() == TOKENS.COMMA:
        tokens.pop(0) # consume COMMA
    if tokens[0].token_type() == TOKENS.RBRACKET:
        tokens.pop(0) # consume RBRACKET
    context.create_value(name, value)
    return

def handle_enum(context, tokens):
    name = tokens[1].value()
    trace(f"parsing enum {name}")
    
    tokens.pop(0) # consume ENUM
    tokens.pop(0) # consume IDENTIFIER
    tokens.pop(0) # consume LBRACKET

    if tokens[0].token_type() != TOKENS.RBRACKET:
        # create sublist
        rbracket = next((x for x in tokens if x.token_type() == TOKENS.RBRACKET), None)
        if rbracket is None:
            error("expected '}' at end of enum")
        endOfEnum = tokens.index(rbracket) + 1
        parse_scope(context, tokens[:endOfEnum], get_enum_scope_syntax())
        del tokens[:endOfEnum]
    else:
        tokens.pop(0) # consume RBRACKET
    context.create_enum(name)
    
def handle_struct_member(context, tokens):
    trace(f"struct member {tokens[0].value()}")
    typeName = tokens[0].value()
    isVariable = False
    count = 1

    tokens.pop(0) # consume IDENTIFIER

    if tokens[0].token_type() == TOKENS.LINDEX:
        tokens.pop(0) # consume LINDEX
        tokens.pop(0) # consume RINDEX
        isVariable = True
    name = tokens[0].value()
    tokens.pop(0) # consume IDENTIFIER
    tokens.pop(0) # consume SEMICOLON

    context.create_member(typeName, name, isVariable, count)

def handle_struct(context, tokens):
    name = tokens[1].value()
    trace(f"parsing struct {name}")
    
    tokens.pop(0) # consume ENUM
    tokens.pop(0) # consume IDENTIFIER
    tokens.pop(0) # consume LBRACKET

    if tokens[0].token_type() != TOKENS.RBRACKET:
        # create sublist
        rbracket = next((x for x in tokens if x.token_type() == TOKENS.RBRACKET), None)
        if rbracket is None:
            error("expected '}' at end of struct")
        endOfStruct = tokens.index(rbracket)
        parse_scope(context, tokens[:endOfStruct], get_struct_scope_syntax())
        del tokens[:endOfStruct]
    
    tokens.pop(0) # consume RBRACKET
    context.create_struct(name, context.finish_members())

    
def match_syntax(tokens, syntax):
    i = 0
    j = 0
    while i < len(tokens) and j < len(syntax):
        trace(f"matching {tokens[i].token_type()} == {syntax[j]} ({j}/{len(syntax)})")
        if isinstance(syntax[j], list):
            foundOption = False
            for option in syntax[j]:
                if tokens[i].token_type() == option:
                    i += 1
                    j += 1
                    foundOption = True
                    break
            if not foundOption:
                return False
        else:
            if tokens[i].token_type() == syntax[j]:
                i += 1
                j += 1
            elif syntax[j] == -1:
                # variable entries
                j += 1
                while i < len(tokens) and tokens[i].token_type() != syntax[j]:
                    i += 1
                if i == len(tokens):
                    return False
            else:
                return False
    return True

def parse_scope(context, tokens, allowed_syntax):
    while len(tokens):
        matched = False
        for syntax in allowed_syntax.items():
            matched = match_syntax(tokens, syntax[1])
            if matched:
                trace(f"found matching syntax: {syntax[0][0]}")
                syntax[0][1](context, tokens)
                break
        
        if not matched:
            error(f"unexpected token at {tokens[0].line_no()}:{tokens[0].line_index()}: {tokens[0].token_type()}")


def parse_identifier(scanner, data):
    ident = ""
    while scanner.index() < len(data):
        el = data[scanner.index()]
        if el.isdigit() or el.isalpha() or el == '_':
            ident += el
            scanner.consume()
        else:
            break
    return ident

def parse_quoted(scanner, data):
    unquoted = ""
    scanner.consume() # consume opening quote
    while scanner.index() < len(data) and data[scanner.index()] != '\"':
        unquoted += data[scanner.index()]
        scanner.consume()
    
    if scanner.index == len(data):
        error("expected closing '\"' but found end of file")

    scanner.consume() # consume closing quote
    return unquoted

def parse_digit(scanner, data):
    startIdx = scanner.index()
    if data[startIdx] == '0' and data[startIdx + 1] == 'x':
        scanner.consume(2)
        while scanner.index() < len(data):
            if data[scanner.index()] not in "0123456789ABCDEF":
                break
            scanner.consume()
        return str2int(data[startIdx : scanner.index()])
    
    while data[scanner.index()] == '-':
        scanner.consume()
    while data[scanner.index()].isdigit():
        scanner.consume()
    return str2int(data[startIdx : scanner.index()])

def create_tokens_from_text(data):
    scanner = ScanContext()
    tokens = []
    while scanner.index() < len(data):
        el = data[scanner.index()]
        # we want to catch newlines to keep track of context
        if el == '\n':
            scanner.nextline()
            continue
        
        # skip any spaces, we don't care about them
        if el.isspace():
            scanner.consume()
            continue
        
        # handle comments
        if element_is_comment_line(data, scanner.index()):
            skip_line(scanner, data)
        elif element_is_comment_block(data, scanner.index()):
            skip_until(scanner, data, "*/")
        elif el == '{':
            scanner.consume()
            tokens.append(Token(scanner, TOKENS.LBRACKET))
        elif el == '}':
            scanner.consume()
            tokens.append(Token(scanner, TOKENS.RBRACKET))
        elif el == '(':
            scanner.consume()
            tokens.append(Token(scanner, TOKENS.LPARENTHESIS))
        elif el == ')':
            scanner.consume()
            tokens.append(Token(scanner, TOKENS.RPARENTHESIS))
        elif el == '[':
            scanner.consume()
            tokens.append(Token(scanner, TOKENS.LINDEX))
        elif el == ']':
            scanner.consume()
            tokens.append(Token(scanner, TOKENS.RINDEX))
        elif el == ':':
            scanner.consume()
            tokens.append(Token(scanner, TOKENS.COLON))
        elif el == ';':
            scanner.consume()
            tokens.append(Token(scanner, TOKENS.SEMICOLON))
        elif el == ',':
            scanner.consume()
            tokens.append(Token(scanner, TOKENS.COMMA))
        elif el == '=':
            scanner.consume()
            tokens.append(Token(scanner, TOKENS.EQUAL))
        elif el == '\"':
            unquoted = parse_quoted(scanner, data)
            tokens.append(Token(scanner, TOKENS.QUOTE, unquoted))
        elif el.isdigit() or el == '-':
            digit = parse_digit(scanner, data)
            tokens.append(Token(scanner, TOKENS.DIGIT, digit))
        elif el.isalpha() or el == '_':
            ident = parse_identifier(scanner, data)
            keywords = get_keywords()
            if ident in keywords:
                tokens.append(Token(scanner, keywords[ident]))
            else:
                tokens.append(Token(scanner, TOKENS.IDENTIFIER, ident))
        else:
            error(f"unexpected character '{el}' at line {scanner.line_no()}:{scanner.line_index()}")
    return tokens


def parse_file(context, file_path):
    data = ""
    try:
        with open(file_path, 'r') as file:
            data = file.read()
    except Exception as e:
        error(f"could not load {file_path}: {str(e)}")

    tokens = create_tokens_from_text(data)
    parse_scope(context, tokens, get_global_scope_syntax())


##########################################
# Argument parser and orchestration code
##########################################
def main(args):
    global trace_enabled
    if args.trace:
        trace_enabled = 1

    context = ParseContext(args.service)
    output_dir = get_dir_or_default(args.out)
    parse_file(context, args.service)
    services = context.get_services()
    include_services = []
    generator = None

    for service in services:
        service.validate()

    if args.include:
        include_services = args.include.split(',')

    if args.lang_c:
        generator = CGenerator()

    if generator is not None:
        generator.generate_shared_files(output_dir, services, include_services)
        if args.client:
            generator.generate_client_files(output_dir, services, include_services)
        if args.server:
            generator.generate_server_files(output_dir, services, include_services)
    return


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Optional app description')
    parser.add_argument('--service', type=str, help='The service file that should be parsed')
    parser.add_argument('--include', type=str,
                        help='The services that should be generated from the file, comma-seperated list, default is all')
    parser.add_argument('--out', type=str, help='Protocol files output directory')
    parser.add_argument('--client', action='store_true', help='Generate client side files')
    parser.add_argument('--server', action='store_true', help='Generate server side files')
    parser.add_argument('--lang-c', action='store_true', help='Generate c-style headers and implementation files')
    parser.add_argument('--trace', action='store_true', help='Trace the protocol parsing process to debug')
    args = parser.parse_args()
    if not args.service or not os.path.isfile(args.service):
        parser.error("a valid service path must be specified")
    main(args)

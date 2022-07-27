
from common.shared import *

def consolidate_struct_member(service: ServiceObject, done: list, struct: StructureObject, member):
    if isinstance(member, VariableVariantObject):
        # we do not support consolidation inside a variant
        return
    
    if member.get_name() == "":
        typename = member.get_typename()
        struct_target = service.lookup_struct(typename)
        if struct_target is None:
            raise ValueError(f"Type {typename} cannot be resolved")
        if not struct_target in done:
            consolidate_struct(struct_target)
        
        mems = struct.get_members()
        index = mems.index(member)
        for member_target in struct_target.get_members():
            mems.insert(index, member_target)
            index += 1
        mems.remove(member)
        struct.set_members(mems)

def consolidate_struct(service: ServiceObject, done: list, struct: StructureObject):
    if struct in done:
        return
    
    for member in struct.get_members():
        consolidate_struct_member(service, done, struct, member)
    done.append(struct)
    return done

def pass_consolidate(service: ServiceObject):
    done = []
    for struct in service.get_structs():
        done = consolidate_struct(service, done, struct)

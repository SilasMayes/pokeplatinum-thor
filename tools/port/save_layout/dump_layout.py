# gdb -batch -x dump_layout.py BINARY (LAYOUT_TYPES=a,b,... LAYOUT_OUT=file.json): the save structures' layouts, from DWARF, as JSON
import gdb, json, os

TYPES = os.environ['LAYOUT_TYPES'].split(',')

def desc(t):
    t = t.strip_typedefs()
    code = t.code
    if code == gdb.TYPE_CODE_STRUCT:
        fields = []
        for f in t.fields():
            if f.bitsize:
                fields.append({'name': f.name, 'bitpos': f.bitpos, 'bitsize': f.bitsize, 'type': {'kind': 'bits'}})
            else:
                fields.append({'name': f.name, 'bitpos': f.bitpos, 'type': desc(f.type)})
        return {'kind': 'struct', 'size': t.sizeof, 'fields': fields}
    if code == gdb.TYPE_CODE_ARRAY:
        lo, hi = t.range()
        return {'kind': 'array', 'size': t.sizeof, 'count': hi - lo + 1, 'elem': desc(t.target())}
    return {'kind': 'leaf', 'size': t.sizeof}  # scalars, enums, unions (copied whole)

out = {name: desc(gdb.lookup_type(name)) for name in TYPES}
json.dump(out, open(os.environ['LAYOUT_OUT'], 'w'))
print('dumped', len(out))

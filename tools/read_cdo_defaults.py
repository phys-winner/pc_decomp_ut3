"""Print selected tagged default properties of Default__<Class> exports (read-only, UT3 v512).

usage: read_cdo_defaults.py Package.u Class[,Class...] Property[,Property...]
Only properties a class sets itself appear; inherited defaults show under the parent class.
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import extract_scripttext as st

package = st.ROOT / sys.argv[1]
classes = sys.argv[2].split(',')
props = set(sys.argv[3].split(','))
data, imports, exports = st.load(package)
# Names table again (load() keeps it local): re-read it cheaply.
raw = data
r = st.Reader(raw)
r.u32(); r.u32(); r.i32(); r.fstring(); r.u32()
name_count, name_offset = r.i32(), r.i32()
r.p = name_offset
names = []
for _ in range(name_count):
    names.append(r.fstring()); r.u64()

def name_at(p):
    i, n = struct.unpack_from('<ii', raw, p)
    return names[i] if 0 <= i < len(names) else None

for cls, outer, name, ssize, soff in exports:
    if not name.startswith('Default__') or name[9:] not in classes:
        continue
    body = raw[soff:soff + ssize]
    found = {}
    for p in range(0, len(body) - 24, 1):
        n = name_at(soff + p)
        if n in props:
            t = name_at(soff + p + 8)
            if t not in ('BoolProperty', 'FloatProperty', 'IntProperty', 'ByteProperty'):
                continue
            size, index = struct.unpack_from('<ii', body, p + 16)
            if t == 'BoolProperty':
                found[n] = struct.unpack_from('<I', body, p + 24)[0]
            elif t == 'FloatProperty':
                found[n] = round(struct.unpack_from('<f', body, p + 24)[0], 4)
            elif t == 'IntProperty':
                found[n] = struct.unpack_from('<i', body, p + 24)[0]
            else:
                found[n] = body[p + 24]
    print(name[9:], found)

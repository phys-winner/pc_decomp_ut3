"""Extract original UnrealScript ScriptText exports from UT3 v512 packages (read-only).

usage: extract_scripttext.py OUTDIR Package.u ClassName [ClassName...]   ('*' extracts every class)
Packages are read from the pinned original installation; nothing is written there.
"""
import struct
import sys
from pathlib import Path

ROOT = Path(r"C:\Program Files (x86)\Steam\steamapps\common\Unreal Tournament 3\UTGame\CookedPC")


def lzo1x(src, size):
    out = bytearray()
    ip = 0

    def literal(n):
        nonlocal ip
        out.extend(src[ip:ip + n]); ip += n

    def copy(pos, n):
        d = len(out) - pos
        if d >= n:
            out.extend(out[pos:pos + n])
        else:
            for k in range(n):
                out.append(out[pos + k])

    state = 'loop'
    t = 0
    if src[0] > 17:
        t = src[0] - 17; ip = 1
        if t < 4:
            state = 'match_next'
        else:
            literal(t); state = 'first'
    while True:
        if state == 'loop':
            t = src[ip]; ip += 1
            if t >= 16:
                state = 'match'; continue
            if t == 0:
                while src[ip] == 0:
                    t += 255; ip += 1
                t += 15 + src[ip]; ip += 1
            literal(t + 3)
            state = 'first'
        if state == 'first':
            t = src[ip]; ip += 1
            if t >= 16:
                state = 'match'
            else:
                pos = len(out) - (1 + 0x800) - (t >> 2) - (src[ip] << 2); ip += 1
                copy(pos, 3)
                state = 'done'
        if state == 'match':
            if t >= 64:
                pos = len(out) - 1 - ((t >> 2) & 7) - (src[ip] << 3); ip += 1
                copy(pos, (t >> 5) - 1 + 2)
            elif t >= 32:
                t &= 31
                if t == 0:
                    while src[ip] == 0:
                        t += 255; ip += 1
                    t += 31 + src[ip]; ip += 1
                pos = len(out) - 1 - ((src[ip] >> 2) + (src[ip + 1] << 6)); ip += 2
                copy(pos, t + 2)
            elif t >= 16:
                pos = len(out) - ((t & 8) << 11)
                t &= 7
                if t == 0:
                    while src[ip] == 0:
                        t += 255; ip += 1
                    t += 7 + src[ip]; ip += 1
                pos -= (src[ip] >> 2) + (src[ip + 1] << 6); ip += 2
                if pos == len(out):
                    break
                pos -= 0x4000
                copy(pos, t + 2)
            else:
                pos = len(out) - 1 - (t >> 2) - (src[ip] << 2); ip += 1
                copy(pos, 2)
            state = 'done'
        if state == 'done':
            t = src[ip - 2] & 3
            if t == 0:
                state = 'loop'; continue
            state = 'match_next'
        if state == 'match_next':
            literal(t)
            t = src[ip]; ip += 1
            state = 'match'
    if len(out) != size:
        raise ValueError(f'LZO size {len(out)} != {size}')
    return bytes(out)


class Reader:
    def __init__(self, data, pos=0):
        self.d, self.p = data, pos

    def i32(self):
        v = struct.unpack_from('<i', self.d, self.p)[0]; self.p += 4; return v

    def u32(self):
        v = struct.unpack_from('<I', self.d, self.p)[0]; self.p += 4; return v

    def u64(self):
        v = struct.unpack_from('<Q', self.d, self.p)[0]; self.p += 8; return v

    def fstring(self):
        n = self.i32()
        if n == 0:
            return ''
        if n > 0:
            s = self.d[self.p:self.p + n - 1].decode('latin-1'); self.p += n; return s
        n = -n
        s = self.d[self.p:self.p + 2 * (n - 1)].decode('utf-16-le'); self.p += 2 * n; return s


def load(path):
    raw = path.read_bytes()
    r = Reader(raw)
    assert r.u32() == 0x9E2A83C1
    version = r.u32()
    assert version & 0xFFFF == 512, version
    r.i32()  # TotalHeaderSize
    r.fstring()
    r.u32()
    name_count, name_offset = r.i32(), r.i32()
    export_count, export_offset = r.i32(), r.i32()
    import_count, import_offset = r.i32(), r.i32()
    r.i32()  # DependsOffset
    r.p += 16
    for _ in range(r.i32()):
        r.p += 12
    r.i32(); r.i32()
    flags = r.u32()
    chunks = [(r.i32(), r.i32(), r.i32(), r.i32()) for _ in range(r.i32())]
    if chunks:
        size = max(u + s for u, s, _, _ in chunks)
        data = bytearray(size)
        data[:chunks[0][0]] = raw[:chunks[0][0]]
        for uoff, usize, coff, csize in chunks:
            c = Reader(raw, coff)
            assert c.u32() == 0x9E2A83C1
            block = c.i32()
            c.i32(); total = c.i32()
            blocks = []
            remaining = total
            while remaining > 0:
                bc, bu = c.i32(), c.i32(); blocks.append((bc, bu)); remaining -= bu
            pos, out = c.p, uoff
            for bc, bu in blocks:
                assert flags & 2, flags  # LZO
                data[out:out + bu] = lzo1x(raw[pos:pos + bc], bu)
                pos += bc; out += bu
        data = bytes(data)
    else:
        data = raw
    r = Reader(data, name_offset)
    names = []
    for _ in range(name_count):
        names.append(r.fstring()); r.u64()
    r.p = import_offset
    imports = []
    for _ in range(import_count):
        r.i32(); r.i32(); cls = r.i32(); r.i32(); outer = r.i32(); name = r.i32(); r.i32()
        imports.append((names[cls], outer, names[name]))
    r.p = export_offset
    exports = []
    for _ in range(export_count):
        cls, sup, outer = r.i32(), r.i32(), r.i32()
        name = r.i32(); r.i32()
        r.i32(); r.u64()
        ssize = r.i32(); soff = r.i32()
        for _ in range(r.i32()):
            r.p += 12
        r.u32()
        for _ in range(r.i32()):
            r.p += 4
        r.p += 16
        r.u32()
        exports.append((cls, outer, names[name], ssize, soff))
    return data, imports, exports


def class_name(imports, exports, index):
    if index < 0:
        return imports[-index - 1][2]
    if index > 0:
        return exports[index - 1][2]
    return 'Class'


def main():
    out = Path(sys.argv[1]); out.mkdir(parents=True, exist_ok=True)
    package = ROOT / sys.argv[2]
    wanted = set(sys.argv[3:])
    data, imports, exports = load(package)
    found = []
    for cls, outer, name, ssize, soff in exports:
        if name != 'ScriptText' or class_name(imports, exports, cls) != 'TextBuffer' or outer <= 0:
            continue
        owner = exports[outer - 1][2]
        if '*' not in wanted and owner not in wanted:
            continue
        body = data[soff:soff + ssize]
        n = struct.unpack_from('<i', body, 20)[0]
        text = body[24:24 + n - 1].decode('latin-1') if n > 0 else body[24:24 - 2 * n - 2].decode('utf-16-le')
        (out / (owner + '.uc')).write_text(text, encoding='utf-8', newline='')
        found.append(owner)
    print(package.name, 'extracted', len(found), sorted(found)[:40])


if __name__ == '__main__':
    main()

"""Compile UT3 shader requests recorded on Android with the game's own D3DX compiler.

The device compiles HLSL that no cache holds with vkd3d-shader (android/host/hlsl_vkd3d.c). A
request it cannot compile is recorded by android/host/d3dx9.c: the source, the macros, every file
the game's include handler served and the cache key. This tool compiles each one on Windows with
Microsoft's d3dx9_35.dll, the D3DX version UT3 imports, and writes the bytecode (<key>.bin) or the
compiler's error (<key>.err) the way the host's cache reads them; pushed to <data>/shadercache
they win over the device's results. They are also the reference of tools/test_vkd3d.py.

  python tools/compile_shader_requests.py REQUEST_DIR OUTPUT_DIR [--dll PATH]
"""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
from pathlib import Path
import struct
import sys

MAGIC = b'UT3SHADERREQ 2\n'


def fnv(h, data):
    for byte in data:
        h = ((h ^ byte) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def parse(data):
    """A recorded request: {function, profile, flags, macros, includes:[(name, bytes)], source, key}."""
    if not data.startswith(MAGIC):
        raise ValueError('not a version 2 shader request')
    pos = len(MAGIC)

    def line():
        nonlocal pos
        end = data.index(b'\n', pos)
        text = data[pos:end].decode('latin-1')
        pos = end + 1
        return text

    def blob(size):
        nonlocal pos
        value = data[pos:pos + size]
        if len(value) != size:
            raise ValueError('truncated shader request')
        pos += size
        return value

    request = {'includes': []}
    for field in ('key', 'function', 'profile', 'flags'):
        name, _, value = line().partition('=')
        if name != field:
            raise ValueError(f'expected {field}, found {name}')
        request[field] = value
    request['flags'] = int(request['flags'])
    name, _, size = line().partition('=')
    if name != 'macros':
        raise ValueError('expected macros')
    request['macros'] = blob(int(size))
    while True:
        text = line()
        if text.startswith('include '):
            _, size, included = text.split(' ', 2)
            request['includes'].append((included, blob(int(size))))
            continue
        name, _, size = text.partition('=')
        if name != 'source':
            raise ValueError('expected source')
        request['source'] = blob(int(size))
        break
    if pos != len(data):
        raise ValueError('trailing bytes in shader request')
    return request


def key(request):
    """The host's cache key (android/host/d3dx9.c shader_key)."""
    h = 0xCBF29CE484222325
    h = fnv(h, request['source'])
    h = fnv(h, b'\0')
    h = fnv(h, request['macros'])
    h = fnv(h, request['function'].encode('latin-1') + b'\0')
    h = fnv(h, request['profile'].encode('latin-1') + b'\0')
    h = fnv(h, struct.pack('<I', request['flags']))
    for name, content in request['includes']:
        h = fnv(h, name.encode('latin-1') + b'\0')
        h = fnv(h, struct.pack('<I', len(content)))
        h = fnv(h, content)
    return f'{h:016x}'


class D3DXMACRO(ctypes.Structure):
    _fields_ = [('Name', ctypes.c_char_p), ('Definition', ctypes.c_char_p)]


OPEN = ctypes.WINFUNCTYPE(ctypes.c_long, ctypes.c_void_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_void_p,
                          ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(wintypes.UINT))
CLOSE = ctypes.WINFUNCTYPE(ctypes.c_long, ctypes.c_void_p, ctypes.c_void_p)


class IncludeVtbl(ctypes.Structure):
    _fields_ = [('Open', OPEN), ('Close', CLOSE)]


class Include(ctypes.Structure):
    _fields_ = [('vtbl', ctypes.POINTER(IncludeVtbl))]


def buffer_bytes(pointer):
    """Contents of an ID3DXBuffer, then releases it."""
    vtbl = ctypes.cast(ctypes.cast(pointer, ctypes.POINTER(ctypes.c_void_p))[0], ctypes.POINTER(ctypes.c_void_p))
    release = ctypes.WINFUNCTYPE(wintypes.ULONG, ctypes.c_void_p)(vtbl[2])
    get_pointer = ctypes.WINFUNCTYPE(ctypes.c_void_p, ctypes.c_void_p)(vtbl[3])
    get_size = ctypes.WINFUNCTYPE(ctypes.c_size_t, ctypes.c_void_p)(vtbl[4])
    data = ctypes.string_at(get_pointer(pointer), get_size(pointer))
    release(pointer)
    return data


def disassemble_request(dll, request):
    """'@disasm' requests: D3DXDisassembleShader of the bytecode (source), colour flag (flags) and comment."""
    comment = None
    for line in request['macros'].decode('latin-1').split('\n'):
        name, _, value = line.partition('=')
        if name == 'comments':
            comment = value.encode('latin-1')
    text = ctypes.c_void_p()
    hr = dll.D3DXDisassembleShader(request['source'], bool(request['flags']), comment, ctypes.byref(text)) & 0xFFFFFFFF
    if hr == 0 and text.value:
        return True, buffer_bytes(text), b''
    return False, hr, b''


def assemble_request(dll, request):
    """'@asm' requests: D3DXAssembleShader with the recorded macros and include files."""
    request = dict(request, function='', profile='')
    return compile_request(dll, request, assemble=True)


def compile_request(dll, request, assemble=False):
    files = {name.lower(): content for name, content in request['includes']}
    keep = []

    def open_include(this, kind, name, parent, data, size):
        content = files.get(name.decode('latin-1').lower())
        if content is None:
            return 0x80004005  # E_FAIL: the game's handler had no such file either
        buffer = ctypes.create_string_buffer(content, len(content))
        keep.append(buffer)
        data[0] = ctypes.cast(buffer, ctypes.c_void_p).value
        size[0] = len(content)
        return 0

    def close_include(this, data):
        return 0

    vtbl = IncludeVtbl(OPEN(open_include), CLOSE(close_include))
    include = Include(ctypes.pointer(vtbl))
    lines = [line for line in request['macros'].decode('latin-1').split('\n') if line]
    macros = (D3DXMACRO * (len(lines) + 1))()
    names = []
    for i, line in enumerate(lines):
        name, _, value = line.partition('=')
        names += [name.encode('latin-1'), value.encode('latin-1')]
        macros[i].Name, macros[i].Definition = names[-2], names[-1]
    shader, errors, constants = ctypes.c_void_p(), ctypes.c_void_p(), ctypes.c_void_p()
    source = request['source']
    if assemble:
        hr = dll.D3DXAssembleShader(source, len(source), macros, ctypes.byref(include), request['flags'],
                                    ctypes.byref(shader), ctypes.byref(errors))
    else:
        hr = dll.D3DXCompileShader(source, len(source), macros, ctypes.byref(include),
                                   request['function'].encode('latin-1'), request['profile'].encode('latin-1'),
                                   request['flags'], ctypes.byref(shader), ctypes.byref(errors), ctypes.byref(constants))
    messages = buffer_bytes(errors) if errors.value else b''
    if constants.value:
        vtbl_ptr = ctypes.cast(ctypes.cast(constants, ctypes.POINTER(ctypes.c_void_p))[0], ctypes.POINTER(ctypes.c_void_p))
        ctypes.WINFUNCTYPE(wintypes.ULONG, ctypes.c_void_p)(vtbl_ptr[2])(constants)
    hr &= 0xFFFFFFFF
    if hr == 0 and shader.value:
        return True, buffer_bytes(shader), messages
    return False, hr, messages


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('requests', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--dll', default=r'C:\Windows\System32\d3dx9_35.dll')
    args = parser.parse_args()
    dll = ctypes.WinDLL(args.dll)
    dll.D3DXCompileShader.restype = ctypes.c_long
    dll.D3DXCompileShader.argtypes = [ctypes.c_char_p, wintypes.UINT, ctypes.c_void_p, ctypes.c_void_p,
                                      ctypes.c_char_p, ctypes.c_char_p, wintypes.DWORD, ctypes.c_void_p,
                                      ctypes.c_void_p, ctypes.c_void_p]
    dll.D3DXAssembleShader.restype = ctypes.c_long
    dll.D3DXAssembleShader.argtypes = [ctypes.c_char_p, wintypes.UINT, ctypes.c_void_p, ctypes.c_void_p,
                                       wintypes.DWORD, ctypes.c_void_p, ctypes.c_void_p]
    dll.D3DXDisassembleShader.restype = ctypes.c_long
    dll.D3DXDisassembleShader.argtypes = [ctypes.c_char_p, wintypes.BOOL, ctypes.c_char_p, ctypes.c_void_p]
    args.output.mkdir(parents=True, exist_ok=True)
    report = []
    for path in sorted(args.requests.glob('*.req')):
        data = path.read_bytes()
        try:
            request = parse(data)
        except ValueError as error:
            report.append({'request': path.name, 'skipped': str(error)})
            continue
        computed = key(request)
        if computed != request['key'] or path.stem != computed:
            raise SystemExit(f'{path.name}: recorded key {request["key"]}, computed {computed}')
        if request['function'] == '@disasm':
            ok, result, messages = disassemble_request(dll, request)
        elif request['function'] == '@asm':
            ok, result, messages = assemble_request(dll, request)
        else:
            ok, result, messages = compile_request(dll, request)
        entry = {'key': computed, 'function': request['function'], 'profile': request['profile'],
                 'includes': [name for name, _ in request['includes']],
                 'request_sha256': hashlib.sha256(data).hexdigest()}
        if ok:
            (args.output / f'{computed}.bin').write_bytes(result)
            entry.update(result='bytecode', bytes=len(result), sha256=hashlib.sha256(result).hexdigest())
        else:
            text = f'hr={result:08X}\n'.encode() + messages
            (args.output / f'{computed}.err').write_bytes(text)
            entry.update(result='error', hr=f'{result:08X}', messages=messages.decode('latin-1')[:2000])
        report.append(entry)
        print(computed, entry['result'], entry.get('bytes', entry.get('hr')))
    (args.output / 'compile-report.json').write_text(json.dumps(report, indent=1) + '\n', encoding='utf-8')
    failed = [entry for entry in report if entry.get('result') == 'error']
    print(f'{len(report)} requests, {len(failed)} errors')


if __name__ == '__main__':
    sys.exit(main())

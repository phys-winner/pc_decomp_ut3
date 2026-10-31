"""Re-protect the existing owned Windows UT3 record for the target app UID.

No plaintext argument, file, console output, hash or generated product key.
The original game still performs IsKeyValid after this config-only migration.
"""
import argparse
import base64
import ctypes as ct
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import struct
import subprocess
import sys
import uuid

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = 'org.ut3.port'
EXTERNAL = '/sdcard/Android/data/org.ut3.port/files'
TARGET = EXTERNAL + '/userdata/Documents/My Games/Unreal Tournament 3/UTGame/Config/UTEngine.ini'
SECTION = 'OnlineSubsystemGameSpy.OnlineSubsystemGameSpy'
SALT = bytes.fromhex('656f4a0066617247')
stage = 'arguments'


def wipe(value):
    if isinstance(value, bytearray) and value:
        ct.memset(ct.addressof((ct.c_ubyte * len(value)).from_buffer(value)), 0, len(value))


def decode_ini(raw):
    if raw.startswith(b'\xff\xfe'): codec, prefix = 'utf-16le', b'\xff\xfe'
    elif raw.startswith(b'\xfe\xff'): codec, prefix = 'utf-16be', b'\xfe\xff'
    elif raw.startswith(b'\xef\xbb\xbf'): codec, prefix = 'utf-8', b'\xef\xbb\xbf'
    else: codec, prefix = 'utf-8', b''
    return raw[len(prefix):].decode(codec, errors='strict'), codec, prefix


def field_span(text):
    headers = list(re.finditer(r'(?m)^[ \t]*\[([^\]\r\n]+)\][^\r\n]*', text))
    found = []
    for index, header in enumerate(headers):
        if header[1].strip().casefold() != SECTION.casefold(): continue
        end = headers[index + 1].start() if index + 1 < len(headers) else len(text)
        for item in re.finditer(r'(?im)^[ \t]*EncryptedProductKey[ \t]*=[ \t]*([^\r\n]*)', text[header.end():end]):
            found.append((header.end() + item.start(1), header.end() + item.end(1)))
    if len(found) != 1: raise ValueError('Unexpected protected field count')
    return found[0]


def decimal_blob(text):
    text = text.strip()
    if len(text) >= 2 and text[0] == text[-1] == '"': text = text[1:-1]
    if not text or len(text) > 3 * (1 << 20) or len(text) % 3 or not re.fullmatch(r'[0-9]+', text):
        raise ValueError('Invalid UE3 protected-data encoding')
    values = [int(text[index:index + 3]) for index in range(0, len(text), 3)]
    if any(value > 255 for value in values): raise ValueError('Invalid UE3 byte')
    return bytes(values)


def windows_entropy():
    class Head(ct.Structure):
        _fields_ = [('Next', ct.c_void_p), ('Combo', ct.c_uint32), ('Name', ct.c_char * 260),
                    ('Description', ct.c_char * 132), ('AddressLength', ct.c_uint32), ('Address', ct.c_ubyte * 8)]
    assert Head.AddressLength.offset == (404 if ct.sizeof(ct.c_void_p) == 8 else 400)
    api = ct.WinDLL('iphlpapi.dll').GetAdaptersInfo
    api.argtypes = [ct.c_void_p, ct.POINTER(ct.c_uint32)]; api.restype = ct.c_uint32
    size = ct.c_uint32(0)
    if api(None, ct.byref(size)) != 111 or not 0 < size.value <= 1 << 20: raise OSError('Adapter query failed')
    buffer = ct.create_string_buffer(size.value); start = ct.addressof(buffer); allocated = size.value
    entropy = bytearray(8)
    try:
        if api(buffer, ct.byref(size)): raise OSError('Adapter query failed')
        pointer = start
        for _ in range(128):
            if not pointer: break
            if not start <= pointer <= start + allocated - ct.sizeof(Head): raise OSError('Invalid adapter response')
            item = ct.cast(pointer, ct.POINTER(Head)).contents
            if item.AddressLength <= 8:
                for index in range(item.AddressLength): entropy[index] = item.Address[index]
                break
            pointer = item.Next
        for index, value in enumerate(SALT): entropy[index] ^= value
        return entropy
    finally:
        ct.memset(start, 0, allocated)


def windows_unprotect(cipher, entropy):
    class Blob(ct.Structure): _fields_ = [('size', ct.c_uint32), ('data', ct.c_void_p)]
    crypt = ct.WinDLL('crypt32.dll', use_last_error=True).CryptUnprotectData
    crypt.argtypes = [ct.POINTER(Blob), ct.c_void_p, ct.POINTER(Blob), ct.c_void_p, ct.c_void_p, ct.c_uint32, ct.POINTER(Blob)]
    crypt.restype = wintypes.BOOL
    free = ct.WinDLL('kernel32.dll').LocalFree; free.argtypes = [ct.c_void_p]; free.restype = ct.c_void_p
    encoded = ct.create_string_buffer(cipher)
    entropy_storage = (ct.c_ubyte * len(entropy)).from_buffer(entropy)
    source = Blob(len(cipher), ct.addressof(encoded)); extra = Blob(len(entropy), ct.addressof(entropy_storage)); result = Blob()
    try:
        if not crypt(ct.byref(source), None, ct.byref(extra), None, None, 1, ct.byref(result)):
            raise OSError('Existing current-user DPAPI record rejected')
        if not result.data or not 0 < result.size <= 1 << 20: raise OSError('Invalid DPAPI response')
        plain = bytearray(result.size)
        ct.memmove(ct.addressof((ct.c_ubyte * len(plain)).from_buffer(plain)), result.data, result.size)
        return plain
    finally:
        if result.data:
            ct.memset(result.data, 0, result.size); free(result.data)


def migrate(args):
    global stage
    if os.name != 'nt': raise ValueError('The Windows PC holding the CD key is required')
    adb = Path(os.environ.get('ANDROID_SDK_ROOT', Path.home() / 'AppData/Local/Android/Sdk')) / 'platform-tools/adb.exe'
    base = [str(adb), '-s', args.serial]
    private = []
    temporary = None
    def device(*words, data=None):
        # exec-out does not forward stdin here. Windows adb shell stdin also
        # stops at byte 0x1a, even without a PTY. ASCII in the stdin pipe avoids
        # both defects; base64 is decoded directly into the child pipe, never
        # into an argument, file or log. Shell protocol sends the final EOF.
        encoded = None
        try:
            if data is None:
                arguments = ['exec-out', *words]
            else:
                encoded = bytearray(base64.b64encode(data))
                text = 'base64 -d | ' + ' '.join(shlex.quote(word) for word in words)
                arguments = ['shell', '-T', '-e', 'none', text]
            result = subprocess.run(base + arguments, input=encoded, stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE, timeout=45)
        finally:
            wipe(encoded)
        if result.returncode: raise RuntimeError('Device operation failed')
        return result.stdout
    def command(text, data=None):
        # This ROM permits shell access to the explicitly scoped external
        # package files but denies run-as there. Keystore still runs as app UID.
        if data is not None: return device('sh', '-c', text, data=data)
        return device('sh -c ' + shlex.quote(text))
    def read_file(path): return command('cat -- ' + shlex.quote(path))
    def running():
        result = subprocess.run(base + ['shell', 'pidof', PACKAGE], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, timeout=15)
        answer = result.stdout.strip()
        if result.returncode == 1 and not answer and not result.stderr.strip(): return False
        if result.returncode != 0 or result.stderr.strip() or not re.fullmatch(rb'[0-9]+(?:\s+[0-9]+)*', answer):
            raise RuntimeError('Process-state query failed')
        return True
    try:
        stage = 'package-verification'
        uid_text = device('run-as', PACKAGE, 'id', '-u').decode().strip()
        if not uid_text.isdecimal() or not 10000 <= int(uid_text) < 100000: raise ValueError('Wrong application UID')
        details = device('dumpsys', 'package', PACKAGE).decode(errors='replace')
        # Android 15 uses appId instead of userId in dumpsys. Resolve the
        # installed user-0 UID directly so a secondary-user appId cannot match.
        listed = device('pm', 'list', 'packages', '-U', '--user', '0', PACKAGE).decode().strip()
        if not re.search(r'(?:pkgFlags|flags)=\[[^\]]*\bDEBUGGABLE\b', details) or listed != 'package:' + PACKAGE + ' uid:' + uid_text:
            raise ValueError('Wrong package/debuggable identity')
        located = device('pm', 'path', PACKAGE).decode().strip()
        if '\n' in located or not re.fullmatch(r'package:/data/app/[A-Za-z0-9_~+/.=:-]+/base\.apk', located): raise ValueError('Unexpected package path')
        apk = located[len('package:'):]
        receipt = json.loads(args.receipt.read_text(encoding='utf-8'))
        expected = receipt['sha256']
        installed = device('run-as', PACKAGE, 'sha256sum', apk).decode().split()[0]
        if receipt['package'] != PACKAGE or installed != expected or hashlib.sha256(Path(receipt['apk']).read_bytes()).hexdigest() != expected:
            raise ValueError('Signed APK receipt mismatch')
        stage = 'stopped-target'
        if running(): raise RuntimeError('Stop the game before migration')
        stage = 'installed-crypto-canary'
        java = ['run-as', PACKAGE, 'env', 'CLASSPATH=' + apk, '/system/bin/app_process', '/', 'org.ut3.port.PortCrypto']
        if b'PASS AndroidKeyStore AES-GCM' not in device(*java, 'SELFTEST'): raise RuntimeError('Installed crypto canary rejected')
        stage = 'stopped-target'
        if running(): raise RuntimeError('Stop the game before migration')
        stage = 'private-target-identity'
        identity = bytearray(read_file(EXTERNAL + '/port-adapter-v1.bin')); private.append(identity)
        if len(identity) != 14 or identity[:8] != b'UT3ID\x00\x00\x01' or identity[8] & 3 != 2: raise ValueError('Target identity unavailable')
        entropy = bytearray(8); private.append(entropy)
        entropy[:6] = identity[8:14]
        for index, value in enumerate(SALT): entropy[index] ^= value
        stage = 'source-and-target-config'
        source_raw = args.source.read_bytes(); source_text, _, _ = decode_ini(source_raw)
        start, end = field_span(source_text); cipher = decimal_blob(source_text[start:end])
        target_raw = read_file(TARGET); target_text, codec, preamble = decode_ini(target_raw)
        target_start, target_end = field_span(target_text)
        stage = 'windows-current-user-dpapi'
        original_entropy = windows_entropy(); private.append(original_entropy)
        plain = windows_unprotect(cipher, original_entropy); private.append(plain)
        stage = 'app-uid-keystore-reprotect'
        frame = bytearray(16 + len(entropy) + len(plain)); private.append(frame)
        frame[:8] = b'UT3MIG01'; struct.pack_into('<II', frame, 8, len(entropy), len(plain))
        frame[16:16 + len(entropy)] = entropy; frame[16 + len(entropy):] = plain
        protected_hex = device(*java, 'ENCRYPT', data=frame).decode().strip()
        if not re.fullmatch(r'[0-9a-fA-F]+', protected_hex) or len(protected_hex) % 2 or len(protected_hex) > 2 * (1 << 20):
            raise ValueError('Invalid protected response')
        protected = bytes.fromhex(protected_hex)
        if not protected.startswith(b'UT3DP\x01\x00\x00'): raise ValueError('Wrong protected envelope')
        replacement = ''.join(format(value, '03d') for value in protected)
        updated_text = target_text[:target_start] + replacement + target_text[target_end:]
        updated_raw = preamble + updated_text.encode(codec, errors='strict')
        check_start, check_end = field_span(updated_text)
        if updated_text[:check_start] != target_text[:target_start] or updated_text[check_end:] != target_text[target_end:]:
            raise ValueError('Unrelated target text changed')
        stage = 'unchanged-precommit'
        if args.source.read_bytes() != source_raw or read_file(TARGET) != target_raw or running():
            raise ValueError('Source/target changed before commit')
        temporary = TARGET + '.portcrypto-' + uuid.uuid4().hex + '.tmp'
        stage = 'atomic-target-write'
        command('cat > ' + shlex.quote(temporary), data=updated_raw)
        if read_file(temporary) != updated_raw: raise ValueError('Temporary readback mismatch')
        # Toybox on this device offers sync without per-file options. Both
        # flushes run only with the game stopped, never during FPS measurements.
        device('sync')
        if read_file(TARGET) != target_raw or args.source.read_bytes() != source_raw or running():
            raise ValueError('Concurrent source/target change')
        command('mv -f -- ' + shlex.quote(temporary) + ' ' + shlex.quote(TARGET)); temporary = None
        device('sync')
        stage = 'exact-readback'
        if read_file(TARGET) != updated_raw or args.source.read_bytes() != source_raw:
            raise ValueError('Postcommit source/target mismatch')
        print('PASS Existing Windows DPAPI record reprotected by target app AndroidKeyStore; exact config readback and unchanged source/unrelated config bytes verified; original game validation remains required')
    finally:
        for value in private: wipe(value)
        if temporary:
            try: command('rm -f -- ' + shlex.quote(temporary))
            except Exception: pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--source', type=Path, default=Path.home() / 'Documents/My Games/Unreal Tournament 3/UTGame/Config/UTEngine.ini')
    parser.add_argument('--receipt', type=Path, default=ROOT / 'build/apk/ut3.json')
    args = parser.parse_args()
    try: migrate(args)
    except Exception:
        print('Product-key migration failed at ' + stage + '; no secret was printed or written as plaintext', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__': raise SystemExit(main())

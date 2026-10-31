"""Where a lifted x86-32 function resumes after one of its catch blocks.

The runtime half is runtime/native32/eh32.c, which has the whole scheme. A
guest throw that a function catches ends with the catch funclet returning an
address *inside that function* (`mov eax, offset $L; ret`), and execution
continues there with the frame's ebp and saved esp. Lifted, that address must
be a label the function's local dispatch can reach, and the function needs a
landing pad for the runtime to longjmp to. This module finds both from the
same data MSVC's __CxxFrameHandler reads:

    push ebp; mov ebp, esp; push -1; push offset handler
    mov eax, fs:[0]; push eax; mov fs:[0], esp       <- installs the frame
    handler:  mov eax, offset FuncInfo; jmp __CxxFrameHandler
    FuncInfo: magic 0x1993052x, maxState, pUnwindMap, nTryBlocks, pTryBlockMap
    TryBlockMapEntry: tryLow, tryHigh, catchHigh, nCatches, pHandlerArray
    HandlerType: adjectives, pType, dispCatchObj, addressOfHandler

The runtime finds a frame's ebp as record + 12 (entry esp - 4). Inline
frames and the exact VC8 __EH_prolog3 helper variants satisfy that contract.
Catch continuations additionally require a verified saved ESP at ebp - 16;
SEH4 and helpers without that slot never receive a speculative landing pad.
"""
import struct
from capstone.x86 import X86_OP_IMM, X86_OP_REG, X86_REG_EAX

EH_MAGICS = (0x19930520, 0x19930521, 0x19930522)
PROLOGUE_WINDOW = 12     # instructions: the frame is installed right at entry
FUNCLET_WINDOW = 0x800   # bytes of a catch funclet scanned for its returns
GS_HANDLER_PREFIX = bytes.fromhex('8b542408')
PROLOG3_PREFIX = bytes.fromhex('5064ff35000000008d44240c2b64240c53565789288be8a1')
PROLOG3_SUFFIX = bytes.fromhex('ff75fcc745fcffffffff8d45f464a300000000c3')


def _edx_memory_end(raw, offset, opcode, register):
    if len(raw) < offset + 2 or raw[offset] != opcode or raw[offset + 1] & 0x3F != register:
        return None
    size = (2, 3, 6, 0)[raw[offset + 1] >> 6]
    return offset + size if size and len(raw) >= offset + size else None


def funcinfo(read, handler):
    """FuncInfo VA a C++ EH handler stub names, or None."""
    # Local UT3 extension: VC8 checks the frame's GS cookie before its metadata stub.
    h = read(handler, 50) or b''
    if h.startswith(GS_HANDLER_PREFIX):
        offset = _edx_memory_end(h, 4, 0x8D, 0x02)  # lea eax, [edx + frame displacement]
        for index in range(2):
            offset = _edx_memory_end(h, offset, 0x8B, 0x0A) if offset is not None else None
            if offset is None or len(h) < offset + 7 or h[offset:offset + 3] != b'\x33\xC8\xE8':
                return None
            check = (handler + offset + 7 + struct.unpack_from('<i', h, offset + 3)[0]) & 0xFFFFFFFF
            cookie = read(check, 15) or b''
            if (len(cookie) != 15 or cookie[:2] != b'\x3B\x0D'
                    or cookie[6:11] != b'\x75\x02\xF3\xC3\xE9'):
                return None
            offset += 7
            if h[offset:offset + 1] == b'\xB8':
                h = h[offset:]
                break
            if index == 1:
                return None
            if h[offset:offset + 2] == b'\x83\xC0':
                if len(h) < offset + 3 or h[offset + 2] not in (4, 8, 12, 16):
                    return None
                offset += 3
    if (len(h) < 10 or h[0] != 0xB8
            or not (h[5] == 0xE9 or len(h) >= 11 and h[5:7] == b'\xFF\x25')):
        return None
    fi = struct.unpack_from('<I', h, 1)[0]
    m = read(fi, 20) or b''
    return fi if len(m) == 20 and struct.unpack_from('<I', m)[0] in EH_MAGICS else None


def catch_funclets(read, fi):
    """Every catch funclet VA a FuncInfo names."""
    ntry, tries = struct.unpack('<II', read(fi + 12, 8))
    out = []
    for t in range(min(ntry, 4096)):
        _lo, _hi, _chi, ncatch, handlers = struct.unpack('<5I', read(tries + 20 * t, 20))
        for c in range(min(ncatch, 256)):
            out.append(struct.unpack('<I', read(handlers + 16 * c + 12, 4))[0])
    return out


def funclet_returns(md, read, va):
    """Addresses a catch funclet returns (`mov eax, imm32` just before `ret`)."""
    code = read(va, FUNCLET_WINDOW) or b''
    out, prev = set(), None
    for ins in md.disasm(code, va):
        if ins.mnemonic == 'ret' and prev is not None:
            out.add(prev)
        prev = None
        if ins.mnemonic == 'mov' and ins.op_str.startswith('eax, 0x'):
            prev = int(ins.op_str[5:], 16)
        if ins.mnemonic == 'int3':
            break
    return out


def frame_handler(read, insns):
    """Return (handler, saved ESP available) for verified inline or VC8 helper frames."""
    head = insns[:PROLOGUE_WINDOW]
    if len(head) >= 3:
        push, mov, call = head[:3]
        if (push.mnemonic == 'push' and push.op_str != 'ebp'
                and len(push.operands) == 1 and push.operands[0].type == X86_OP_IMM
                and 0 <= push.operands[0].imm <= 0x100000 and push.operands[0].imm % 4 == 0
                and mov.mnemonic == 'mov' and len(mov.operands) == 2
                and mov.operands[0].type == X86_OP_REG and mov.operands[0].reg == X86_REG_EAX
                and mov.operands[1].type == X86_OP_IMM and call.mnemonic == 'call'
                and len(call.operands) == 1 and call.operands[0].type == X86_OP_IMM
                and push.address + push.size == mov.address and mov.address + mov.size == call.address):
            helper = read(call.operands[0].imm & 0xFFFFFFFF, 64) or b''
            offset = len(PROLOG3_PREFIX)
            if helper.startswith(PROLOG3_PREFIX) and helper[offset + 4:offset + 7] == b'\x33\xC5\x50':
                cookie = struct.unpack_from('<I', helper, offset)[0]
                if len(read(cookie, 4) or b'') != 4:
                    return None
                tail = helper[offset + 7:]
                for extra, saved in ((b'', False), (b'\x89\x65\xF0', True),
                                     (b'\x89\x45\xF0', False), (b'\x89\x45\xEC\x89\x65\xF0', True)):
                    if tail.startswith(extra + PROLOG3_SUFFIX):
                        handler = mov.operands[1].imm & 0xFFFFFFFF
                        return (handler, saved) if funcinfo(read, handler) is not None else None
    if (len(head) < 2 or head[0].mnemonic != 'push' or head[0].op_str != 'ebp'
            or head[1].mnemonic != 'mov' or head[1].op_str != 'ebp, esp'):
        return None
    handler = None
    for ins in head:
        if ins.mnemonic == 'push' and ins.op_str.startswith('0x'):
            handler = int(ins.op_str, 16)
        elif ins.mnemonic == 'mov' and ins.op_str.endswith('dword ptr fs:[0]') and handler is not None:
            return (handler, True) if funcinfo(read, handler) is not None else None
    return None


def resume_points(md, read, insns):
    """None if the function installs no C++ EH frame of its own; else the set
    of addresses its catch funclets resume at (possibly empty: a frame with
    only unwind actions still needs no landing pad, but is reported)."""
    frame = frame_handler(read, insns)
    if frame is None:
        return None
    handler, saved_esp = frame
    out = set()
    for f in catch_funclets(read, funcinfo(read, handler)):
        out |= funclet_returns(md, read, f)
    if out and not saved_esp:
        raise ValueError('C++ catch continuation requires a verified saved ESP')
    return out


def _selftest():
    from capstone import Cs, CS_ARCH_X86, CS_MODE_32
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    mem = {}

    def put(va, raw):
        mem.update({va + i: b for i, b in enumerate(raw)})

    def read(va, n):
        return bytes(mem.get(va + i, 0xCC) for i in range(n))
    # An MSVC6 guarded function at 0x1000: handler stub at 0x2000, FuncInfo at
    # 0x3000, one try block with one catch(...) whose funclet at 0x1100 resumes
    # at 0x1050.
    put(0x1000, bytes.fromhex('558bec6aff6800200000' '64a100000000' '50' '64892500000000'))
    put(0x2000, bytes.fromhex('b800300000' 'e900000000'))
    put(0x3000, struct.pack('<5I', 0x19930520, 1, 0x3100, 1, 0x3200))
    put(0x3200, struct.pack('<5I', 0, 0, 1, 1, 0x3300))
    put(0x3300, struct.pack('<4I', 0, 0, 0, 0x1100))
    put(0x1100, bytes.fromhex('b850100000' 'c3'))
    insns = list(md.disasm(read(0x1000, 24), 0x1000))
    assert resume_points(md, read, insns) == {0x1050}, resume_points(md, read, insns)
    # Without push ebp; mov ebp, esp first, no landing pad.
    assert resume_points(md, read, insns[1:]) is None
    # A plain function.
    assert resume_points(md, read, list(md.disasm(bytes.fromhex('558bec33c05dc3'), 0x4000))) is None
    print('eh32 selftest: ok')


if __name__ == '__main__':
    _selftest()

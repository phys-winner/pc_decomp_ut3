"""Apply the whole-program call-boundary summary (tools/call_protocol.py) to generated bodies:
masked register publication at calls, tails and exits, and flags only where a reader exists.
See android/runtime/recomp_types.h, "Masked call protocol".

UT3 copy of homm5/tools/protocol_transform.py (the Heroes V pass this port follows), with three
UT3 rules on top:
  - `full` names every body the host replaces or intercepts (linker --wrap targets and
    void_dispatch_add overrides): calls into them publish and reload everything, a constant tail
    to them stays a dispatched RECOMP_ITAIL, and their own exits keep the full protocol;
  - an indirect call or tail through an import slot whose exporting body is not MSVC-ABI
    (callee-saved registers read or changed, entry flags read) keeps the full protocol, e.g.
    UT3.exe's `call [__imp__setjmp3]`, which stores the caller's ebx/esi/edi/ebp;
  - indirect tails through an unknown target publish the flags, since the target is unknown;
  - clean exits: a masked exit leaves a callee-saved register unpublished only when the body never
    wrote it to the globals itself. A nested call site that publishes EBX/ESI/EDI/EBP, a call kept on
    the full protocol, a cooperative yield (RECOMP_BACKEDGE publishes everything) or an EH frame leaves
    the global copy holding an intermediate value, which callers that read the globals after the call
    (host code, native kernels, full-protocol sites) would take for the preserved one: such an exit
    republishes the body's own (restored) value. With this, every exit leaves all eight globals as
    the full protocol did, and only lifted callers skip the reloads;
  - bodies in `warm` (a census of what a WAR-Torlan match runs) resolve their indirect calls through
    per-site caches (RECOMP_RESOLVE switched to RECOMP_SITE_RESOLVE around the body), everything else
    through the shared void_resolve cache, so the site-cache code stays out of cold bodies.
The summary is produced from the canonical generated sources; this pass only rewrites statements
whose shape it recognises and leaves anything else on the full protocol.
"""
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
GENERATED = ROOT / 'generated'
SUMMARY = ROOT / 'build/call-protocol/summary.json'

ABI4 = 0x01 | 0x02 | 0x04 | 0x10        # eax ecx edx esp
CS = 0x08 | 0x20 | 0x40 | 0x80          # ebx ebp esi edi
ALL = 0xFF
# Tails are exits: what they publish is already the body's final state, so they are not listed.
FULL_MARKERS = (b'RECOMP_BACKEDGE', b'RECOMP_EH_ENTER', b'RECOMP_CALL(', b'RECOMP_ICALL(', b'CPUID(')

FUNCTION_START = re.compile(rb'^void (sub_[0-9A-F]{8})(?:_lifted)?\(void\) \{\r?$', re.M)
FUNCTION_END = re.compile(rb'\n\}\r?\n')
CALL = re.compile(rb'RECOMP_CALL\(((?:__wrap_)?sub_[0-9A-F]{8})\);')
SITE = re.compile(rb'[^\r\n]*?/\* 0x([0-9A-F]{8}): ')
ICALL_OPEN = b'RECOMP_ICALL('
ICALL_SITE = re.compile(rb'\); (?:RECOMP_FLAGS_IN\(\); goto L_[0-9A-F]{8}; )?/\* 0x([0-9A-F]{8}): ')
IAT_OPERAND = re.compile(rb'^MEM32\(0x([0-9A-F]{1,8})\)$')
RET_FLAGS = re.compile(rb'RECOMP_FLAGS_OUT\(\); (esp \+= \d+; \{ (?:RECOMP_EH_LEAVE\(\); )?RECOMP_REGS_OUT\(\); return; \})')
TAIL_CONST = re.compile(rb'RECOMP_ITAIL\(0x([0-9A-F]{8})u\); \{ ((?:RECOMP_EH_LEAVE\(\); )?)RECOMP_REGS_OUT\(\); return; \}')
TAIL_SWITCH = re.compile(rb'RECOMP_ITAIL\(_itail_tgt\); \{ ((?:RECOMP_EH_LEAVE\(\); )?)RECOMP_REGS_OUT\(\); return; \}')
ITAIL_SLOT = re.compile(rb'_itail_tgt = \(uint32_t\)\(MEM32\(0x([0-9A-F]{1,8})\)\)')
EXIT = re.compile(rb'RECOMP_REGS_OUT\(\);( return;|\r?\n\}\r?\n$)')


def import_slots(summary):
    """IAT slot VA -> exporting function name, for slots whose exporter is lifted."""
    import pefile
    specs = {row['file'].lower(): row for row in json.loads((ROOT / 'config/modules.json').read_text())['modules']}
    exports = {}
    for spec in specs.values():
        path = GENERATED / 'input' / spec['file']
        if not path.is_file():
            continue
        pe = pefile.PE(str(path), fast_load=True)
        pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXPORT']])
        base = pe.OPTIONAL_HEADER.ImageBase
        table = {}
        for symbol in getattr(getattr(pe, 'DIRECTORY_ENTRY_EXPORT', None), 'symbols', []) or []:
            if symbol.address and not symbol.forwarder:
                name = 'sub_%08X' % (base + symbol.address)
                if symbol.name:
                    table[symbol.name.decode(errors='replace')] = name
                table['#%d' % symbol.ordinal] = name
        exports[spec['file'].lower()] = table
        pe.close()
    slots = {}
    for spec in specs.values():
        path = GENERATED / 'input' / spec['file']
        if not path.is_file():
            continue
        pe = pefile.PE(str(path), fast_load=True)
        pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT']])
        for entry in getattr(pe, 'DIRECTORY_ENTRY_IMPORT', []) or []:
            table = exports.get(entry.dll.decode(errors='replace').lower())
            if table is None:
                continue
            for imp in entry.imports:
                key = imp.name.decode(errors='replace') if imp.name else '#%d' % imp.ordinal
                if key in table:
                    slots[imp.address] = table[key]
        pe.close()
    return slots


class Protocol:
    def __init__(self, path=SUMMARY, full=(), warm=(), masked_native=()):
        data = json.loads(Path(path).read_text())
        if data.get('edition') != 'ut3':
            raise ValueError('Call protocol summary does not belong to UT3')
        self.flags_in = set(data['flags_in'])
        self.flags_out = set(data['flags_out'])
        self.regs = {name: tuple(masks) for name, masks in data['regs'].items()}
        self.problem_sites = {int(p['address'], 16) for p in data['problems']}
        self.known = set(data['names'])
        # Bodies the host replaces keep the full protocol on both sides.
        self.full = set(full)
        self.masked_native = set(masked_native)
        self.warm = set(warm)
        self.slots = import_slots(data)
        self.full_slots = {slot for slot, name in self.slots.items()
                           if name in self.full or name in self.flags_in or any(self.regs.get(name, (0, 0)))}
        self._declared = {}

    def declared(self, module):
        """Names the module's own header declares (direct tails stay in-module)."""
        if module not in self._declared:
            header = GENERATED / module / 'functions.h'
            self._declared[module] = set(re.findall(r'^void (sub_[0-9A-F]{8})\(void\);', header.read_text(), re.M))
        return self._declared[module]

    def cs(self, name):
        return self.regs.get(name, (0, 0))

    def call_masks(self, callee):
        if callee.startswith('__wrap_'):
            callee = callee.removeprefix('__wrap_')
            if callee not in self.masked_native:
                return ALL, ALL, 1
        if callee in self.full or callee not in self.known:
            return ALL, ALL, 1
        cs_in, cs_out = self.cs(callee)
        return ABI4 | cs_in | cs_out, ABI4 | cs_out, int(callee in self.flags_in)

    def exit_mask(self, name):
        if name in self.full or name not in self.known:
            return ALL
        return ABI4 | self.cs(name)[1]

    def slot_full(self, expr):
        slot = IAT_OPERAND.match(expr.strip())
        return bool(slot) and int(slot[1], 16) in self.full_slots


def rewrite_icalls(body, proto):
    out, pos = [], 0
    while True:
        at = body.find(ICALL_OPEN, pos)
        if at < 0:
            break
        depth, i = 1, at + len(ICALL_OPEN)
        while i < len(body) and depth:
            c = body[i]
            if c == 0x28:
                depth += 1
            elif c == 0x29:
                depth -= 1
            elif c == 0x0A:
                break
            i += 1
        if depth:
            out.append(body[pos:at + len(ICALL_OPEN)])
            pos = at + len(ICALL_OPEN)
            continue
        expr = body[at + len(ICALL_OPEN):i - 1]
        site = ICALL_SITE.match(body, i - 1)
        if not site or int(site[1], 16) in proto.problem_sites or proto.slot_full(expr):
            out.append(body[pos:i])
        else:
            out += [body[pos:at], b'RECOMP_ICALL_P(%s, 0x%02Xu, 0x%02Xu, 0, 0x%su)' % (expr, ABI4, ABI4, site[1])]
        pos = i
    out.append(body[pos:])
    return b''.join(out)


def transform_body(name, body, proto, module):
    """Rewrite one function body (the text after its opening line)."""
    exit_mask = proto.exit_mask(name)
    full_body = exit_mask == ALL
    keep_flags = full_body or name in proto.flags_out
    # A body that tails through a non-ABI import slot (a `jmp [__imp_x]` thunk) is that import.
    if any(int(m[1], 16) in proto.full_slots for m in ITAIL_SLOT.finditer(body)):
        return body

    dirty = 0

    def call(match):
        nonlocal dirty
        pub, rel, fl = proto.call_masks(match[1].decode())
        dirty |= pub & CS
        # Unproven same-unit native calls retain their existing full-protocol source.
        if match[1].startswith(b'__wrap_') and (pub, rel, fl) == (ALL, ALL, 1):
            return match[0]
        site = SITE.match(body, match.end())
        return b'RECOMP_CALL_P(%s, 0x%02Xu, 0x%02Xu, %d, 0x%su);' % (
            match[1], pub, rel, fl, site[1] if site else b'0')

    def tail_const(match):
        target = 'sub_' + match[1].decode()
        if target not in proto.known or target in proto.full or target not in proto.declared(module):
            return match[0]
        pub, _, fl = proto.call_masks(target)
        return b'RECOMP_TAIL_P(%s, 0x%02Xu, %d); { %sreturn; }' % (
            target.encode(), pub | exit_mask, fl, match[2])

    def tail_switch(match):
        if keep_flags:
            return match[0]
        # The target is unknown: its entry may read flags, so they are published.
        return b'RECOMP_ITAIL_P(_itail_tgt, 0x%02Xu, 1); { %sreturn; }' % (ABI4 | exit_mask, match[1])

    body = CALL.sub(call, body)
    body = rewrite_icalls(body, proto)
    # Sites left on the full protocol, yields and EH landings publish every register.
    if any(marker in body for marker in FULL_MARKERS):
        dirty |= CS
    exit_mask |= dirty
    full_body = exit_mask == ALL
    if not keep_flags:
        body = RET_FLAGS.sub(lambda m: m[1], body)
    if not full_body:
        body = TAIL_CONST.sub(tail_const, body)
        body = TAIL_SWITCH.sub(tail_switch, body)
        body = EXIT.sub(lambda m: b'RECOMP_EXIT_P(0x%02Xu);%s' % (exit_mask, m[1]), body)
    return body


SITE_ON = b'#undef RECOMP_RESOLVE\n#define RECOMP_RESOLVE(va) RECOMP_SITE_RESOLVE(va)\n'
SITE_OFF = b'#undef RECOMP_RESOLVE\n#define RECOMP_RESOLVE(va) RECOMP_RESOLVE_PLAIN(va)\n'


def apply_protocol(text, proto, module):
    parts, pos = [], 0
    for match in FUNCTION_START.finditer(text):
        end = FUNCTION_END.search(text, match.end())
        if not end or match.start() < pos:
            continue
        name = match[1].decode()
        body = transform_body(name, text[match.end():end.end()], proto, module)
        if name in proto.warm and (b'RECOMP_ICALL' in body or b'RECOMP_ITAIL' in body):
            parts += [text[pos:match.start()], SITE_ON, text[match.start():match.end()], body, SITE_OFF]
        else:
            parts += [text[pos:match.end()], body]
        pos = end.end()
    return b''.join(parts + [text[pos:]]) if parts else text

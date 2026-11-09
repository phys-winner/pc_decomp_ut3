"""Which volatile registers (eax, ecx, edx) and flags a caller may read after calling each function.

The MSVC convention makes eax/ecx/edx and the flags dead after a call except for a return value,
but link-time code generation lets callers rely on private contracts (a callee known not to change
edx, for instance). A host replacement of a lifted body has to produce exactly what some caller
reads. This pass computes, over the whole program, vol_out[f]: the volatile registers live after
any direct call to f, propagated through returns and tails (a value live after a call to g that
reaches g's `ret` unchanged is live after calls to every function that tails into g, and a caller
that returns right after calling f hands its own callers' needs to f). Indirect callers follow the
ABI (eax and edx may carry a result; ecx does not survive), and so do the exported or
address-taken entries the host or other modules call.

Callees are treated as preserving every register they are not known to define (conservative for
liveness). Uses the call_protocol.py bodies and decode cache.

    python tools/volatile_liveness.py [--summary build/call-protocol/summary.json] [--out build/call-protocol/volatile.json]
"""
import argparse
import json
from pathlib import Path
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
import call_protocol as cp  # noqa: E402

EAX, ECX, EDX = cp.BIT['eax'], cp.BIT['ecx'], cp.BIT['edx']
VOL = EAX | ECX | EDX
ABI_RESULT = EAX | EDX


def body_liveness(b, vol_out, vol_in):
    """Backward liveness of the volatile registers; returns live_in per instruction."""
    n = len(b.insns)
    live = [0] * n
    name = b.f.name
    dirty = True
    while dirty:
        dirty = False
        for i in range(n - 1, -1, -1):
            out = 0
            for s in b.succ[i]:
                out |= live[s]
            e = b.exit[i]
            if e is not None:
                if e[0] == 'ret':
                    out |= vol_out.get(name, 0)
                elif e[0] == 'tail':
                    out |= vol_out.get(name, 0)
                    out |= vol_in.get(e[1], VOL) if e[1] else VOL
            c = b.call[i]
            d = b.insns[i]
            if c is not None:
                if c[0] == 'direct' and c[1]:
                    value = out | vol_in.get(c[1], VOL)
                else:
                    value = out | VOL   # unknown callee: it may read any argument register
                if d is not None:
                    value |= d.reads & VOL
            elif d is None:
                value = 0
            else:
                value = (out & ~(d.writes & ~d.partial & VOL)) | (d.reads & VOL)
            if value != live[i]:
                live[i] = value
                dirty = True
    return live


def xmm_live_after(bodies, functions, targets):
    """For direct calls to `targets`: XMM registers some path reads after the call before writing.

    Decodes each calling body again with capstone (operand details), runs a backward liveness of
    xmm0-7 over its CFG; calls clobber every XMM register (MSVC: all are volatile) and `ret` keeps
    xmm0 live (a __m128 result). Returns {target: {"sites": n, "live": mask, "examples": [...]}}."""
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    images = {}
    result = {t: {'sites': 0, 'live': 0, 'examples': []} for t in targets}
    for name, b in bodies.items():
        sites = [i for i, c in enumerate(b.call) if c and c[0] == 'direct' and c[1] in targets]
        if not sites:
            continue
        module = b.f.module
        image = images.get(module['name']) or images.setdefault(module['name'], cp.Image(cp.GENERATED / 'input' / module['file']))
        uses, defs = [], []
        for d in b.insns:
            u = w = 0
            if d is not None:
                insn = next(md.disasm(image.code(d.address, 16), d.address, 1), None)
                if insn is not None:
                    try:
                        read, written = insn.regs_access()
                    except capstone.CsError:
                        read, written = [], []
                    for reg in read:
                        rn = insn.reg_name(reg)
                        if rn.startswith(('xmm', 'ymm')):
                            u |= 1 << int(rn[3:])
                    for reg in written:
                        rn = insn.reg_name(reg)
                        if rn.startswith(('xmm', 'ymm')):
                            w |= 1 << int(rn[3:])
                    # Scalar writes (movss/addss...) keep the upper lanes: a partial write reads too.
                    if w and insn.mnemonic.endswith(('ss', 'sd')) and not insn.mnemonic.startswith(('movs', 'cvt')):
                        u |= w
            uses.append(u)
            defs.append(w)
        n = len(b.insns)
        live = [0] * n
        dirty = True
        while dirty:
            dirty = False
            for i in range(n - 1, -1, -1):
                out = 0
                for s in b.succ[i]:
                    out |= live[s]
                e = b.exit[i]
                if e is not None and e[0] in ('ret', 'tail'):
                    out |= 1          # xmm0 may carry a vector result
                if b.call[i] is not None:
                    value = 0         # a call may define every XMM register and reads none
                else:
                    value = (out & ~defs[i]) | uses[i]
                if value != live[i]:
                    live[i] = value
                    dirty = True
        for i in sites:
            after = 0
            for s in b.succ[i]:
                after |= live[s]
            r = result[b.call[i][1]]
            r['sites'] += 1
            r['live'] |= after
            if after and len(r['examples']) < 8:
                r['examples'].append(f'{name}@{b.f.addresses[i]:08X}:{after:02X}')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--summary', type=Path, default=cp.ROOT / 'build/call-protocol/summary.json')
    parser.add_argument('--out', type=Path, default=cp.ROOT / 'build/call-protocol/volatile.json')
    parser.add_argument('--xmm', nargs='*', default=[], help='also report XMM registers live after direct calls to these')
    parser.add_argument('names', nargs='*', help='functions to report (default: all with a non-ABI result)')
    args = parser.parse_args()
    start = time.time()
    functions, bodies, helpers = cp.prepare(args.out.parent)
    summary = json.loads(args.summary.read_text())
    entries = cp.module_entries()
    vas = {f.va for f in functions.values()}
    taken = cp.address_taken(functions, vas) | entries
    # Bodies entered from outside (vtables, callbacks, exports, host calls) start with the ABI result.
    vol_out = {name: ABI_RESULT for name, f in functions.items() if f.va in taken}
    vol_in = {}
    rounds = 0
    indirect_ecx = []
    rounds_final = False
    while True:
        rounds += 1
        changed = False
        for name, b in bodies.items():
            if not b.insns:
                continue
            live = body_liveness(b, vol_out, vol_in)
            entry = b.f.addresses.index(b.f.va) if b.f.va in b.f.addresses else 0
            if live[entry] != vol_in.get(name, -1):
                vol_in[name] = live[entry]
                changed = True
            for i, c in enumerate(b.call):
                if c is not None and c[0] != 'direct' and rounds_final:
                    after = 0
                    for s in b.succ[i]:
                        after |= live[s]
                    if after & ECX:
                        indirect_ecx.append(f'{name}@{b.f.addresses[i]:08X}')
                if c is None or c[0] != 'direct' or not c[1]:
                    continue
                after = 0
                for s in b.succ[i]:
                    after |= live[s]
                if not b.succ[i] and b.exit[i] is None:
                    after = 0
                if after & ~vol_out.get(c[1], 0) & VOL:
                    vol_out[c[1]] = vol_out.get(c[1], 0) | (after & VOL)
                    changed = True
            for e in b.exit:
                if e is not None and e[0] == 'tail' and e[1] and vol_out.get(name, 0) & ~vol_out.get(e[1], 0):
                    vol_out[e[1]] = vol_out.get(e[1], 0) | vol_out.get(name, 0)
                    changed = True
        print(f'round {rounds}: non-ABI results {sum(1 for v in vol_out.values() if v & ~ABI_RESULT)} '
              f'({time.time() - start:.0f}s)', flush=True)
        if rounds_final:
            break
        if not changed:
            rounds_final = True   # one more pass records the indirect sites with the final summaries
    names = args.names or sorted(n for n, v in vol_out.items() if v & ~ABI_RESULT)
    report = {'summary_sha256': cp.hashlib.sha256(args.summary.read_bytes()).hexdigest(),
              'functions': {n: {'vol_out': vol_out.get(n, 0), 'vol_in': vol_in.get(n, VOL),
                                'flags_out': n in summary['flags_out'], 'flags_in': n in summary['flags_in'],
                                'cs': summary['regs'].get(n, [0, 0])} for n in names},
              'bits': {'eax': EAX, 'ecx': ECX, 'edx': EDX},
              'xmm_after_direct_calls': xmm_live_after(bodies, functions, set(args.xmm)) if args.xmm else {},
              'indirect_ecx_live_sites': indirect_ecx}
    args.out.write_text(json.dumps(report, indent=1) + '\n')
    print(f'{len(names)} functions -> {args.out} ({time.time() - start:.0f}s)', flush=True)


if __name__ == '__main__':
    main()

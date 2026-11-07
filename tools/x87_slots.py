"""x87 stack slots as C locals in lifted functions (a build step of tools/build_android.py).

The lifted code keeps the x87 stack in a global window (fp_push, fp_pop, _st[i], fp_xch: see
android/runtime/recomp_types.h, RECOMP_X87_WINDOW): every push and pop moves the window, writes a
value and an integer shadow, and every _st[i] is a load through it, so x87 arithmetic becomes a chain
of memory operations (and fp_window_recentre calls). For a function whose x87 stack is empty at entry,
at every call and at every return, and whose depth is the same on every path into each line, this
rewrites each slot into a C local (double s0..s7): the same double arithmetic in the same order, so the
results are bit-identical; only the storage changes. transform_file() rewrites every such function of
a lifted source in place and keeps the others as they are.

The depths come from the control flow of the lifted C itself: gotos, the jump table under `_ljump:`
(entered by `{ _itail_tgt = ...; goto _ljump; }`) and returns. Anything the analysis does not model
keeps the function on the window: an exception frame (_setjmp resumes), a computed goto, an x87
statement it cannot reach, a depth that differs between paths, a value taken from or left for a
caller or callee (float results, the CRT's x87 helpers), the integer shadow forms (fild/fistp qword).

The rewritten code is derived from the game's lifted code: it is generated into the build tree, never
into the repository.

  python tools/x87_slots.py generated/physxcore 1E23C400 [...]   -> prints the rewritten functions
"""
import re
import sys
from pathlib import Path

LABEL = re.compile(r'^(L_[0-9A-F]{8}):')
GOTO = re.compile(r'goto (L_[0-9A-F]{8});')
CASE = re.compile(r'^\s*case 0x[0-9A-F]+u: goto (L_[0-9A-F]{8});\s*$')
FSTP_ST = re.compile(r'\{ _st\[(\d)\] = _st\[0\]; g_st_i64\[\1\] = g_st_i64\[0\]; fp_pop\(\); \}')
CALL = re.compile(r'\bRECOMP_I?(?:CALL|TAIL)\w*\s*\(|\bsub_[0-9A-F]{8}\s*\(|\bvoid_call\w*\s*\(')
X87 = re.compile(r'\b_st\[|fp_push|fp_pop|fp_xch|g_st_i64')
COMMENT = re.compile(r'/\*.*?\*/')
CALL_COMMENT = re.compile(r'/\* 0x[0-9A-F]{8}: call ')


class X87Error(ValueError):
    pass


def find_function(folder, address):
    """The text of `void sub_<address>(void) { ... }` from a lifted module folder."""
    head = f'void sub_{address}(void) {{'
    for path in sorted(Path(folder).glob('functions_*.c')):
        text = path.read_text(encoding='utf-8', errors='strict')
        start = text.find('\n' + head)
        if start < 0:
            continue
        start += 1
        end = text.find('\n}\n', start)
        if end < 0:
            raise X87Error(f'sub_{address}: no end')
        return text[start:end + 2]
    raise X87Error(f'sub_{address} not found in {folder}')


def matching_paren(text, open_index):
    depth = 0
    for i in range(open_index, len(text)):
        if text[i] == '(':
            depth += 1
        elif text[i] == ')':
            depth -= 1
            if depth == 0:
                return i
    raise X87Error('unbalanced parentheses: ' + text)


def slot(depth, i):
    index = depth - 1 - i
    if index < 0 or index > 7:
        raise X87Error(f'x87 slot st({i}) at depth {depth}')
    return f's{index}'


def rewrite_operands(text, depth):
    """_st[i] inside an expression, with no push or pop in it."""
    if 'fp_push' in text or 'fp_pop' in text or 'g_st_i64' in text or 'fp_xch' in text:
        raise X87Error('nested x87 stack operation: ' + text)
    return re.sub(r'_st\[(\d)\]', lambda m: slot(depth, int(m[1])), text)


def rewrite_line(line, depth):
    """Returns the rewritten line and the depth after it."""
    out = []
    i = 0
    while i < len(line):
        m = FSTP_ST.match(line, i)
        if m:
            k = int(m[1])
            if depth < 1:
                raise X87Error('fstp st(0) on an empty stack (a value from the caller)')
            if k:
                out.append(f'{{ {slot(depth, k)} = {slot(depth, 0)}; }}')
            depth -= 1
            i = m.end()
            continue
        if line.startswith('fp_push(', i):
            close = matching_paren(line, i + 7)
            argument = rewrite_operands(line[i + 8:close], depth)
            if depth < 0 or depth >= 8:
                raise X87Error(f'x87 push at depth {depth}')
            out.append(f's{depth} = (double)({argument})')
            depth += 1
            i = close + 1
            continue
        if line.startswith('fp_pop()', i):
            out.append(slot(depth, 0))
            depth -= 1
            i += len('fp_pop()')
            continue
        if line.startswith('fp_xch(', i):
            close = matching_paren(line, i + 6)
            k = int(line[i + 7:close])
            a, b = slot(depth, 0), slot(depth, k)
            out.append(f'do {{ double _x = {a}; {a} = {b}; {b} = _x; }} while (0)')
            i = close + 1
            continue
        if line.startswith('_st[', i):
            m = re.match(r'_st\[(\d)\]', line[i:])
            out.append(slot(depth, int(m[1])))
            i += m.end()
            continue
        if line.startswith('g_st_i64', i) or line.startswith('fp_push_i64', i) or line.startswith('_fp_top', i):
            raise X87Error('unsupported x87 form: ' + line.strip())
        out.append(line[i])
        i += 1
    if depth < 0:
        raise X87Error('x87 depth below the entry: the function takes values from its caller')
    return ''.join(out), depth


def transform(function_text, new_name):
    lines = function_text.split('\n')
    header = lines[0]
    if not re.match(r'void sub_[0-9A-F]{8}\(void\) \{', header):
        raise X87Error('unexpected function header: ' + header)
    body = lines[1:]
    labels = {}
    for index, line in enumerate(body):
        m = LABEL.match(line.strip())
        if m:
            labels[m[1]] = index
    text_body = function_text.split('\n', 1)[1]
    # An exception frame resumes at a catch block through _setjmp, with whatever x87 stack the throw
    # left; a computed goto has no visible targets. Such functions keep the window.
    if 'RECOMP_EH_ENTER' in text_body or 'goto *' in text_body:
        raise X87Error('the function has an exception frame or a computed goto')
    # A jump table is `{ _itail_tgt = ...; goto _ljump; }`: the switch under `_ljump:` goes to any of
    # its cases, or through its default to an indirect tail call (a call, so the stack must be empty).
    jump_table = []
    ljump = next((i for i, line in enumerate(body) if line.strip() == '_ljump:'), None)
    if ljump is not None:
        jump_table = [m[1] for m in map(CASE.match, body[ljump + 1:]) if m]
    if '_itail_tgt =' in text_body.replace('uint32_t _itail_tgt = 0;', '') and ljump is None:
        raise X87Error('an indirect jump without a jump table')
    # Depth at every line by a worklist over the control flow (the entry stack is empty).
    depth_at = [None] * len(body)
    work = [(0, 0)]
    while work:
        start, depth = work.pop()
        index = start
        while index < len(body):
            line = body[index]
            if depth_at[index] is not None:
                if depth_at[index] != depth:
                    raise X87Error(f'inconsistent x87 depth at line {index}: {depth_at[index]} and {depth}')
                break
            depth_at[index] = depth
            # The disassembly comment may sit mid-line (rep stos: the code goes on after it).
            code = COMMENT.sub('', line)
            stripped = code.strip()
            _, after = rewrite_line(line, depth)
            # MSVC calls with an empty x87 stack; a callee that takes or returns x87 values (the CRT
            # helpers, a float result) shows as a depth error at the call or at the next st(0).
            if (CALL.search(code) or CALL_COMMENT.search(line)) and (depth or after):
                raise X87Error(f'call with x87 depth {depth} at line {index}')
            for target in GOTO.findall(stripped):
                if target not in labels:
                    raise X87Error('goto to an unknown label ' + target)
                work.append((labels[target], after))
            if 'goto _ljump;' in code:
                if after != 0:
                    raise X87Error(f'jump table with x87 depth {after}')
                for target in jump_table:
                    if target not in labels:
                        raise X87Error('jump table to an unknown label ' + target)
                    work.append((labels[target], 0))
                if stripped.startswith('{ _itail_tgt = ') or stripped.startswith('goto _ljump;'):
                    break
            if 'return;' in code:
                if after != 0:
                    raise X87Error(f'return with x87 depth {after}')
                if not stripped.startswith(('RECOMP_FLAGS_OUT();', '{ RECOMP_', 'L_', 'default:')):
                    raise X87Error('a return the analysis cannot place: ' + stripped)
                break
            if stripped.startswith('goto ') or re.match(r'(RECOMP_BACKEDGE\(\);\s*)?goto L_', stripped):
                break
            depth = after
            index += 1
    out = [f'void {new_name}(void) {{', '    double s0 = 0, s1 = 0, s2 = 0, s3 = 0, s4 = 0, s5 = 0, s6 = 0, s7 = 0;']
    for index, line in enumerate(body):
        if depth_at[index] is None:
            # Code the analysis does not reach keeps its integer statements as they are; an x87
            # statement there means a control transfer the analysis does not know, so the function
            # keeps the window rather than guess a depth.
            if X87.search(line):
                raise X87Error(f'x87 statement at line {index} that the analysis does not reach')
            out.append(line)
            continue
        rewritten, _ = rewrite_line(line, depth_at[index])
        out.append(rewritten)
    text = '\n'.join(out)
    if X87.search(text):
        raise X87Error('x87 window access left after the rewrite')
    return text


FUNCTION = re.compile(r'^void (sub_[0-9A-F]{8})\(void\) \{$', re.M)


def transform_file(text, module):
    """Every function of a lifted source whose x87 stack qualifies, rewritten in place; the others
    are kept. Returns the new text and the counts (rewritten, kept)."""
    out, position, rewritten, kept = [], 0, 0, 0
    for match in FUNCTION.finditer(text):
        end = text.find('\n}\n', match.start())
        if end < 0:
            raise X87Error(f'{match[1]}: no end')
        end += 2
        body = text[match.start():end]
        out.append(text[position:match.start()])
        if re.search(r'fp_push|fp_pop|_st\[', body):
            try:
                body = transform(body, match[1])
                rewritten += 1
            except X87Error:
                kept += 1
        out.append(body)
        position = end
    out.append(text[position:])
    new = ''.join(out).replace('#include "functions.h"', f'#include "{module}/functions.h"', 1)
    return new, rewritten, kept


def main():
    folder = sys.argv[1]
    for address in sys.argv[2:]:
        print(transform(find_function(folder, address), f'__x87_sub_{address}'))


if __name__ == '__main__':
    main()

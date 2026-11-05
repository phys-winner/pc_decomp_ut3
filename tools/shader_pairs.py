"""Regenerate android/host/shader_pairs.c (the GLES renderer's shipped prelink manifest) from one or
more userdata/ShaderCache/pairs.txt files recorded on devices. A line: vertex bytecode hash, pixel
bytecode hash (FNV-1a 64, hex), D3DCOLOR input mask (hex), alpha-test variant (decimal). Hashes only."""
import argparse
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "android/host/shader_pairs.c"
LINE = re.compile(r"^([0-9a-f]{16}) ([0-9a-f]{16}) ([0-9a-f]{1,4}) (\d{1,2})$")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pairs", nargs="+", type=Path)
    args = parser.parse_args()
    pairs = set()
    for path in args.pairs:
        for line in path.read_text(encoding="ascii").splitlines():
            m = LINE.match(line.strip())
            if not m:
                if line.strip():
                    raise ValueError(f"{path}: unexpected line {line!r}")
                continue
            bgra, alpha = int(m[3], 16), int(m[4])
            if bgra > 0xFFFF or alpha > 9:
                raise ValueError(f"{path}: variant out of range {line!r}")
            pairs.add((int(m[1], 16), int(m[2], 16), (bgra << 8) | alpha))
    rows = sorted(pairs)
    head = OUT.read_text(encoding="utf-8").split("const uint64_t d3d_shipped_pairs", 1)[0]
    body = ["const uint64_t d3d_shipped_pairs[][3] = {"]
    body += [f"    {{0x{v:016x}ull, 0x{p:016x}ull, 0x{x:x}}}," for v, p, x in rows] or ["    {0, 0, 0},"]
    body += ["};", f"const uint32_t d3d_shipped_pair_count = {len(rows)};", ""]
    OUT.write_text(head + "\n".join(body), encoding="utf-8", newline="\n")
    print(f"{OUT.relative_to(ROOT)}: {len(rows)} pairs")


if __name__ == "__main__":
    main()

"""Lift pinned UT3 PE images to C; original Steam files are read-only inputs."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile

import pefile

from lift_game import (collect_functions, emit_dispatch, emit_functions, analyze_pe,
                       build_iat_map, BoundedDisassembler)
from pe_rebase import rebase_image

ROOT = Path(__file__).resolve().parents[1]


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)


def code_address(pe, address):
    section = pe.get_section_by_rva(address - pe.OPTIONAL_HEADER.ImageBase)
    return section is not None and bool(section.Characteristics & 0x20000000)


def module_jump_tables(spec):
    tables = {}
    for row in spec.get("jump_tables", []):
        address = int(row["address"], 0)
        if address in tables:
            raise ValueError(f"Duplicate declared jump table {address:#x}")
        tables[address] = (row["first_index"], row["count"])
    return tables


def metadata_roots(pe):
    """Relocated pointers distinguish vtables from UTF-16 words resembling VAs."""
    base = pe.OPTIONAL_HEADER.ImageBase
    roots = {base + e.address for e in getattr(
        getattr(pe, "DIRECTORY_ENTRY_EXPORT", None), "symbols", [])
        if not e.forwarder and code_address(pe, base + e.address)}
    for block in getattr(pe, "DIRECTORY_ENTRY_BASERELOC", []):
        for relocation in block.entries:
            if relocation.type != 3:
                continue
            section = pe.get_section_by_rva(relocation.rva)
            if section is None or section.Characteristics & 0x20000000:
                continue
            raw = pe.get_data(relocation.rva, 4)
            if len(raw) != 4:
                raise ValueError("Truncated relocation pointer")
            pointer = struct.unpack("<I", raw)[0]
            if code_address(pe, pointer):
                roots.add(pointer)
    load_config = getattr(pe, "DIRECTORY_ENTRY_LOAD_CONFIG", None)
    if load_config:
        table = getattr(load_config.struct, "SEHandlerTable", 0)
        count = getattr(load_config.struct, "SEHandlerCount", 0)
        if count > 1000000:
            raise ValueError("Unreasonable SafeSEH table length")
        if count:
            raw = pe.get_data(table - base, count * 4)
            if len(raw) != count * 4:
                raise ValueError("Truncated SafeSEH table")
            for (rva,) in struct.iter_unpack("<I", raw):
                if not code_address(pe, base + rva):
                    raise ValueError(f"SafeSEH handler is not executable: {base + rva:#x}")
                roots.add(base + rva)
    tls = getattr(pe, "DIRECTORY_ENTRY_TLS", None)
    if tls and tls.struct.AddressOfCallBacks:
        table = tls.struct.AddressOfCallBacks - base
        for index in range(1024):
            raw = pe.get_data(table + index * 4, 4)
            if len(raw) != 4:
                raise ValueError("Truncated TLS callback table")
            address = struct.unpack("<I", raw)[0]
            if not address:
                break
            if not code_address(pe, address):
                raise ValueError("TLS callback outside code")
            roots.add(address)
        else:
            raise ValueError("Unterminated TLS callback table")
    return roots


def prepare(spec, source_root):
    source = Path(spec["source"]) if "source" in spec else source_root / spec["path"]
    raw = source.read_bytes()
    if digest(raw) != spec["sha256"]:
        raise ValueError(f"Original SHA-256 mismatch: {source}")
    pe = pefile.PE(data=raw, fast_load=True)
    if pe.FILE_HEADER.Machine != 0x14C or pe.OPTIONAL_HEADER.Magic != 0x10B:
        raise ValueError(f"Expected PE32 i386: {source}")
    base = int(spec["base"], 0)
    old_base = pe.OPTIONAL_HEADER.ImageBase
    if old_base != base:
        raw, relocation = rebase_image(raw, base)
        if rebase_image(raw, old_base)[0] != source.read_bytes():
            raise ValueError(f"Reverse relocation failed: {source}")
    if spec.get("jump_tables"):
        rebased = pefile.PE(data=raw, fast_load=True)
        try:
            for row in spec["jump_tables"]:
                rva = int(row["address"], 0) - base + 4 * row["first_index"]
                size = 4 * row["count"]
                section = rebased.get_section_by_rva(rva)
                if (size <= 0 or section is None or
                        rva + size > section.VirtualAddress + section.SizeOfRawData):
                    raise ValueError("Declared jump table is outside file-backed PE data")
                if digest(rebased.get_data(rva, size)) != row["sha256"]:
                    raise ValueError(f"Declared jump table SHA-256 mismatch: {row['address']}")
        finally:
            rebased.close()
    target = ROOT / "generated/input" / spec["file"]
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_bytes(raw)
    return target, digest(raw)


def merge_dispatch(specs):
    entries, headers, module_lines = [], [], []
    for spec in specs:
        directory = ROOT / "generated" / spec["name"]
        report_path = directory / "report.json"
        if not report_path.exists():
            continue
        report = json.loads(report_path.read_text())
        if report.get("status") != "GENERATED":
            continue
        if digest((ROOT / "generated/input" / spec["file"]).read_bytes()) != report["image_sha256"]:
            raise ValueError(f"Stale generated image: {spec['file']}")
        entries.extend(int(a, 0) for a in report["dispatch_entries"])
        headers.append(f'{spec["name"]}/functions.h')
        module_lines.append('{"%s", "%s", %su, %d, 0},' % (
            spec["file"], spec["file"], spec["base"], int(spec.get("exe", False))))
    if not entries:
        raise ValueError("No generated modules")
    emit_dispatch(ROOT / "generated/dispatch.c", entries, headers)
    (ROOT / "generated/modules.inc").write_text("\n".join(module_lines) + "\n")


def regenerate_function(spec, address, source_root):
    """Replace a verified body through the production emitter, preserving its VA."""
    directory = ROOT / "generated" / spec["name"]
    report_path = directory / "report.json"
    report = json.loads(report_path.read_text())
    if report.get("status") != "GENERATED":
        raise ValueError("A complete module is required for targeted regeneration")
    addresses = [int(a, 0) for a in report["dispatch_entries"]]
    if address not in addresses:
        raise ValueError(f"Entry {address:#x} is not in the module dispatch")
    binary, image_hash = prepare(spec, source_root)
    if image_hash != report["image_sha256"]:
        raise ValueError("Prepared PE differs from the generated module")
    parsed = analyze_pe(str(binary))
    imports = build_iat_map(parsed)
    pe = pefile.PE(str(binary))
    for module in getattr(pe, "DIRECTORY_ENTRY_DELAY_IMPORT", []):
        for symbol in module.imports:
            imports[symbol.address] = (module.dll.decode(),
                symbol.name.decode() if symbol.name else f"ordinal_{symbol.ordinal}")
    pe.close()
    decoder = BoundedDisassembler(binary.read_bytes(), parsed.image_base, parsed.sections,
                                  jump_tables=module_jump_tables(spec))
    function = decoder.disassemble_function(address, imports)
    if function is None or not function.blocks:
        raise ValueError(f"Cannot decode verified entry {address:#x}")
    source = directory / report["sources"][addresses.index(address) // 100]
    text = source.read_text(encoding="utf-8")
    symbol = f"void sub_{address:08X}(void) {{"
    if text.count(symbol) != 1:
        raise ValueError("Generated body does not match dispatch order")
    start = text.index(symbol)
    end = text.find("\nvoid sub_", start + len(symbol))
    if end < 0:
        end = len(text)
    with tempfile.TemporaryDirectory(prefix="ut3-function-") as temporary:
        output = Path(temporary)
        emitted = emit_functions(output, imports, decoder, {address: function},
                                 additional_entries=addresses)
        replacement = (output / emitted["sources"][0]).read_text(encoding="utf-8")
        replacement = replacement[replacement.index(symbol):].rstrip() + "\n"
    updated = text[:start] + replacement + text[end:]
    temporary_source = source.with_suffix(".c.tmp")
    temporary_source.write_text(updated, encoding="utf-8")
    temporary_source.replace(source)
    traps = set(report["functions_containing_traps"])
    traps.discard(hex(address))
    if "void_lift_trap(" in replacement:
        traps.add(hex(address))
    report["functions_containing_traps"] = sorted(traps, key=lambda a: int(a, 0))
    report.setdefault("targeted_regenerations", {})[hex(address)] = {
        "source": source.name, "source_sha256": digest(source.read_bytes()),
        "instructions_with_overlap": emitted["emitted_instructions_with_overlap"],
        "eh_owners": emitted["eh_owners"]}
    write_json(report_path, report)
    print(f"Regenerated {spec['name']} {address:#x} in {source.name}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--module", action="append", help="Module name from config/modules.json")
    parser.add_argument("--function", action="append", type=lambda a: int(a, 0),
                        help="Regenerate an existing VA; requires exactly one --module")
    args = parser.parse_args()
    inputs = json.loads((ROOT / "config/inputs.json").read_text())
    specs = json.loads((ROOT / "config/modules.json").read_text())["modules"]
    known = {m["name"] for m in specs}
    if args.module and set(args.module) - known:
        parser.error("Unknown module: " + ", ".join(sorted(set(args.module) - known)))
    if args.function:
        if not args.module or len(args.module) != 1:
            parser.error("--function requires exactly one --module")
        spec = next(m for m in specs if m["name"] == args.module[0])
        for address in args.function:
            regenerate_function(spec, address, Path(inputs["source_root"]))
        return
    for spec in specs:
        if args.module and spec["name"] not in args.module:
            continue
        directory = ROOT / "generated" / spec["name"]
        directory.mkdir(parents=True, exist_ok=True)
        write_json(directory / "report.json", {"status": "GENERATING"})
        binary, image_hash = prepare(spec, Path(inputs["source_root"]))
        pe = pefile.PE(str(binary))
        roots = metadata_roots(pe)
        catalog = ROOT / "config" / f'{spec["name"]}.functions.json'
        rows = json.loads(catalog.read_text())["functions"] if catalog.exists() else []
        print(f'{spec["name"]}: {len(roots)} metadata roots, {len(rows)} catalog entries', flush=True)
        info, imports, decoder, functions, rejected = collect_functions(
            binary, rows, roots, label=spec["file"], compact=True, pointer_entries=(),
            jump_tables=module_jump_tables(spec))
        # Include delay-IAT names in generation; binding is performed by the host loader.
        for module in getattr(pe, "DIRECTORY_ENTRY_DELAY_IMPORT", []):
            for symbol in module.imports:
                imports[symbol.address] = (module.dll.decode(),
                    symbol.name.decode() if symbol.name else f"#{symbol.ordinal}")
        report = emit_functions(directory, imports, decoder, functions,
                                label=spec["file"], header_name="functions.h", chunk_prefix="functions")
        report.update(status="GENERATED", module=spec["file"],
                      original_sha256=spec["sha256"], image_sha256=image_hash,
                      metadata_roots=len(roots), rejected_entries=rejected,
                      dispatch_entries=[hex(a) for a in sorted(functions)])
        write_json(directory / "report.json", report)
        pe.close()
    merge_dispatch(specs)


if __name__ == "__main__":
    main()

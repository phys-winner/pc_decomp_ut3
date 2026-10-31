"""Read-only catalog of this pinned UT3 installation and UE3 version-512 packages."""

import hashlib
import json
import re
import struct
from collections import Counter
from pathlib import Path

import pefile
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ESP


ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def decode_text(path):
    raw = path.read_bytes()
    return raw.decode("utf-16" if raw.startswith((b"\xff\xfe", b"\xfe\xff")) else "utf-8-sig")


def sections(path):
    result, current = [], None
    for line, text in enumerate(decode_text(path).splitlines(), 1):
        clean = text.strip()
        if clean.startswith("[") and clean.endswith("]"):
            current = {"section": clean[1:-1], "line": line, "values": []}
            result.append(current)
        elif current and clean and not clean.startswith(";"):
            current["values"].append({"line": line, "text": clean})
    return result


class OriginalLzo:
    """Use the already pinned original decoder, with no downloaded dependency."""

    def __init__(self, source, expected_hash):
        exe = source / "Binaries/UT3.exe"
        assert sha(exe) == expected_hash
        pe = pefile.PE(str(exe), fast_load=True)
        self.machine = Uc(UC_ARCH_X86, UC_MODE_32)
        self.machine.mem_map(0x01570000, 0x20000)
        self.machine.mem_write(0x01570000, pe.get_data(0x01570000 - pe.OPTIONAL_HEADER.ImageBase, 0x20000))
        for address, size in ((0x30000000, 0x40000), (0x31000000, 0x40000),
                              (0x32000000, 0x1000), (0x300000, 0x10000), (0x100000, 0x1000)):
            self.machine.mem_map(address, size)

    def decompress(self, data, size):
        assert 0 < len(data) < 0x40000 and 0 < size <= 0x20000
        machine = self.machine
        machine.mem_write(0x30000000, data)
        machine.mem_write(0x32000000, struct.pack("<I", size))
        machine.mem_write(0x308000, struct.pack("<6I", 0x100000, 0x30000000, len(data),
                                               0x31000000, 0x32000000, 0))
        machine.reg_write(UC_X86_REG_ESP, 0x308000)
        machine.emu_start(0x01579230, 0x100000, count=20000000)
        assert machine.reg_read(UC_X86_REG_EAX) == 0
        assert struct.unpack("<I", machine.mem_read(0x32000000, 4))[0] == size
        return bytes(machine.mem_read(0x31000000, size))


def package(path, lzo, exports=True):
    raw = path.read_bytes()
    assert struct.unpack_from("<II", raw) == (0x9E2A83C1, 512), path
    folder_length = struct.unpack_from("<i", raw, 12)[0]
    assert folder_length == 5 and raw[16:21] == b"None\0", path
    flags, name_count, name_offset, export_count, export_offset, import_count, import_offset, depends = struct.unpack_from("<8I", raw, 21)
    assert 0 < name_count < 1000000 and 0 <= import_count < 1000000 and 0 < export_count < 1000000
    generations = struct.unpack_from("<I", raw, 69)[0]
    engine, cooker, compression, chunks = struct.unpack_from("<4I", raw, 73 + 12 * generations)
    wanted = depends if exports else export_offset
    if chunks:
        assert compression == 2
        rows = [struct.unpack_from("<4I", raw, 89 + 12 * generations + 16 * i) for i in range(chunks)]
        logical = bytearray(raw[:rows[0][0]])
        for start, length, offset, compressed_length in rows:
            if start >= wanted:
                break
            assert len(logical) == start
            magic, block_size, compressed_total, total = struct.unpack_from("<4I", raw, offset)
            assert magic == 0x9E2A83C1 and block_size == 0x20000 and total == length
            count = (total + block_size - 1) // block_size
            blocks = [struct.unpack_from("<2I", raw, offset + 16 + 8 * i) for i in range(count)]
            assert sum(x[0] for x in blocks) == compressed_total and sum(x[1] for x in blocks) == total
            pos = offset + 16 + 8 * count
            assert pos + compressed_total == offset + compressed_length
            for compressed, uncompressed in blocks:
                if len(logical) >= wanted:
                    break
                logical += lzo.decompress(raw[pos:pos + compressed], uncompressed)
                pos += compressed
    else:
        logical = raw
    names, pos = [], name_offset
    for _ in range(name_count):
        length = struct.unpack_from("<i", logical, pos)[0]
        pos += 4
        assert 0 < abs(length) < 65536
        size = abs(length) * (1 if length > 0 else 2)
        value = logical[pos:pos + size]
        assert len(value) == size and value.endswith(b"\0" if length > 0 else b"\0\0")
        names.append(value.decode("latin1" if length > 0 else "utf-16-le").rstrip("\0"))
        pos += size + 8
    assert pos == import_offset
    imports = []
    for index in range(import_count):
        class_package, _, cls, _, outer, name, _ = struct.unpack_from("<4Ii2I", logical, import_offset + index * 28)
        imports.append({"name": names[name], "outer": outer, "class": names[cls], "package": names[class_package]})
    assert import_offset + import_count * 28 == export_offset
    objects, pos = [], export_offset
    if exports:
        for _ in range(export_count):
            cls, parent, outer, name, _, archetype, flag_low, flag_high, size, offset, components = struct.unpack_from("<3i2Ii5I", logical, pos)
            assert components < 10000
            pos += 44 + components * 12
            export_flags, generation_count = struct.unpack_from("<2I", logical, pos)
            assert generation_count < 1000
            pos += 28 + generation_count * 4
            objects.append({"name": names[name], "outer": outer, "class": cls, "parent": parent,
                            "serial_size": size, "serial_offset": offset})
        assert pos == depends

    def resolve(index, chain=()):
        if index == 0:
            return ""
        assert index not in chain and len(chain) < 100
        obj = objects[index - 1] if index > 0 else imports[-index - 1]
        parent = resolve(obj["outer"], (*chain, index))
        return (parent + "." if parent else "") + obj["name"]

    for index, obj in enumerate(objects):
        obj["path"] = resolve(index + 1)
        obj["class_path"] = resolve(obj["class"])
        obj["parent_path"] = resolve(obj["parent"])
    return {"names": names, "imports": [resolve(-i - 1) for i in range(import_count)],
            "objects": objects, "name_count": name_count, "export_count": export_count,
            "compression": compression, "package_flags": hex(flags), "logical": logical}


def read_package_range(raw, lzo, begin, size):
    """Read a serialized object without materializing a whole compressed package."""
    generations = struct.unpack_from("<I", raw, 69)[0]
    compression, count = struct.unpack_from("<2I", raw, 81 + 12 * generations)
    if not count:
        result = raw[begin:begin + size]
        assert len(result) == size
        return result
    assert compression == 2 and 0 < size < 0x1000000
    output, copied = bytearray(size), 0
    for index in range(count):
        start, length, offset, _ = struct.unpack_from("<4I", raw, 89 + 12 * generations + 16 * index)
        if start >= begin + size or start + length <= begin:
            continue
        magic, block_size, _, total = struct.unpack_from("<4I", raw, offset)
        assert magic == 0x9E2A83C1 and block_size == 0x20000 and total == length
        blocks = (total + block_size - 1) // block_size
        position, logical = offset + 16 + 8 * blocks, start
        for block in range(blocks):
            compressed, uncompressed = struct.unpack_from("<2I", raw, offset + 16 + 8 * block)
            if logical < begin + size and logical + uncompressed > begin:
                value = lzo.decompress(raw[position:position + compressed], uncompressed)
                left, right = max(begin, logical), min(begin + size, logical + uncompressed)
                output[left - begin:right - begin] = value[left - logical:right - logical]
                copied += right - left
            position += compressed
            logical += uncompressed
    assert copied == size
    return bytes(output)


def main():
    manifest = json.loads((ROOT / "config/inputs.json").read_text())
    source = Path(manifest["source_root"])
    expected = next(row["sha256"] for row in manifest["modules"] if row["path"] == "Binaries/UT3.exe")
    lzo = OriginalLzo(source, expected)
    cooked = source / "UTGame/CookedPC"
    all_files = sorted(p for p in source.rglob("*") if p.is_file())
    records = [{"path": p.relative_to(source).as_posix(), "size": p.stat().st_size} for p in all_files]
    result = {"source_root": str(source), "ut3_exe_sha256": expected,
              "date": "2026-10-28", "method": "read-only source/config inventory and validated version-512 package tables; original pinned x86 LZO decoder in Unicorn",
              "files": records, "maps": [], "scripts": [], "config_sections": {}, "languages": [], "characters": []}
    for path in sorted((source / "UTGame/Config").glob("*.ini")):
        result["config_sections"][path.name] = {"sha256": sha(path), "sections": sections(path)}
    for path in sorted(cooked.rglob("*.ut3")):
        parsed = package(path, lzo)
        refs = [s for s in parsed["imports"] if re.search(r"UTVehicle|UTVWeap|UTOnslaught|UTCTF|UTGreed|UTMutator", s, re.I)]
        result["maps"].append({"name": path.stem, "path": path.relative_to(source).as_posix(),
                               "size": path.stat().st_size, "sha256": sha(path),
                               "name_count": parsed["name_count"], "export_count": parsed["export_count"],
                               "vehicle_and_mode_imports": refs,
                               "vehicle_factory_instances": dict(Counter(obj["class_path"] for obj in parsed["objects"]
                                   if "UTVehicleFactory" in obj["class_path"] and not obj["name"].startswith("Default__")))})
    interest = re.compile(r"Unlock|Cheat|Vehicle|Hero|Greed|Betray|Deploy|Seat|Debug|God|Ghost|AllWeapons|AllAmmo|Summon|Slomo|Fly|Walk|Chapter|Campaign|Language|Holiday|Christmas", re.I)
    for name in ("Core.u", "Engine.u", "UTGame.u", "UTGameContent.u", "UT3Gold.u", "UT3GoldGame.u"):
        path = cooked / name
        parsed = package(path, lzo)
        selected = [obj for obj in parsed["objects"] if interest.search(obj["path"]) and
                    (obj["class"] == 0 or obj["class_path"].endswith(".Function"))]
        result["scripts"].append({"path": path.relative_to(source).as_posix(), "sha256": sha(path),
                                  "name_count": parsed["name_count"], "export_count": parsed["export_count"],
                                  "interesting_objects": selected,
                                  "interesting_names": [s for s in parsed["names"] if interest.search(s) and not s.startswith("SUICIDES_")]})
        payloads = {}
        selected_functions = {"UTVehicleBase.SwitchWeapon", "UTPlayerController.SwitchWeapon", "UTPlayerController.Use",
                              "UTUIScene.TryCharacterUnlock", "UTCheatManager.UnlockAllChars", "UTCheatManager.SummonV",
                              "UTUIFrontEnd_CharacterFaction.OnEnterUnlockCode", "UTUIFrontEnd_CharacterFaction.OnButtonBar_EnterUnlockCode"}
        for obj in parsed["objects"]:
            if obj["path"] not in selected_functions:
                continue
            value = read_package_range(path.read_bytes(), lzo, obj["serial_offset"], obj["serial_size"])
            script_size = struct.unpack_from("<I", value, 40)[0]
            assert script_size > 0 and value[44 + script_size - 1] == 0x53
            native_index, precedence, function_flags = struct.unpack_from("<HBI", value, 44 + script_size)
            assert len(value) in (44 + script_size + 15, 44 + script_size + 17)
            payloads[obj["path"]] = {"serialized_sha256": hashlib.sha256(value).hexdigest(), "serialized_hex": value.hex(),
                                     "script_size": script_size, "native_index": native_index, "function_flags": hex(function_flags),
                                     "printable_literals": [s.decode("ascii") for s in re.findall(rb"[ -~]{5,}", value)]}
        result["scripts"][-1]["function_payloads"] = payloads
        if name in ("UTGame.u", "Engine.u"):
            literals = re.findall(rb"[ -~]{5,}", parsed["logical"])
            result["scripts"][-1]["unlock_literal_candidates"] = [s.decode("ascii") for s in literals if re.search(rb"unlock|phayder|jihan|allchar|[Uu]nlock[Cc]ode", s, re.I)]
    for folder in sorted((source / "UTGame/Localization").iterdir()):
        if not folder.is_dir():
            continue
        code = folder.name.upper()
        files = list(folder.iterdir())
        localized = [row for row in records if re.search(rf"(?:_LOC_|Startup_){code}\.upk$", row["path"], re.I)]
        result["languages"].append({"code": code, "text_files": len(files), "localized_packages": len(localized),
                                    "startup": [r["path"] for r in localized if "/Startup_" in r["path"]],
                                    "utgame": [r["path"] for r in localized if "/UTGame_LOC_" in r["path"]],
                                    "text_sha256": {p.name: sha(p) for p in files if p.is_file() and p.suffix.lower() == "." + code.lower()}})
    for line, text in enumerate(decode_text(source / "UTGame/Config/DefaultCustomChar.ini").splitlines(), 1):
        if not text.startswith("Characters="):
            continue
        fields = dict(re.findall(r'(CharName|CharID|Faction|FamilyID|HeadID|bLocked)\s*=\s*"?([^",)]+)', text, re.I))
        result["characters"].append({"line": line, **fields, "default_locked": bool(re.search(r'bLocked\s*=\s*"?true', text, re.I))})
    parts = re.findall(r'^Parts=.*?ObjectName="([^"]*)"', decode_text(source / "UTGame/Config/DefaultCustomChar.ini"), re.M)
    package_paths = {Path(row["path"]).stem.lower(): row["path"] for row in records if Path(row["path"]).suffix.lower() == ".upk"}
    result["character_assets"] = {"part_records": len(parts), "packages": {name: package_paths.get(name.lower())
        for name in sorted({value.split(".")[0] for value in parts if value})}}
    assert all(result["character_assets"]["packages"].values())
    assert len(result["maps"]) == 66 and len(result["languages"]) == 9
    assert any(row["default_locked"] for row in result["characters"])
    assert any(any(obj["path"] == "UTUIFrontEnd_CharacterFaction.OnButtonBar_EnterUnlockCode" for obj in s["interesting_objects"]) for s in result["scripts"])
    target = ROOT / "research/hidden-content-evidence.json"
    target.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"report": str(target), "files": len(records), "maps": len(result["maps"]),
                      "scripts": len(result["scripts"]), "characters": len(result["characters"]),
                      "locked_characters": sum(row["default_locked"] for row in result["characters"]),
                      "languages": [row["code"] for row in result["languages"]]}, ensure_ascii=False))


if __name__ == "__main__":
    main()

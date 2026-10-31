"""Audit explicit traps in generated static C, deduplicating overlapping functions."""
from collections import Counter
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
TRAP = re.compile(r'void_lift_trap\(\s*(0x[0-9a-fA-F]+)u?\s*,\s*("(?:[^"\\]|\\.)*")')


def parse_traps(source):
    return [(int(address, 16), json.loads(reason)) for address, reason in TRAP.findall(source)]


def main():
    specs = json.loads((ROOT / "config/modules.json").read_text())["modules"]
    modules, incomplete = [], []
    for spec in specs:
        directory = ROOT / "generated" / spec["name"]
        path = directory / "report.json"
        if not path.exists():
            incomplete.append(spec["name"])
            continue
        report = json.loads(path.read_text())
        if report.get("status") != "GENERATED":
            incomplete.append(spec["name"])
            continue
        occurrences = Counter()
        for source in report["sources"]:
            occurrences.update(parse_traps((directory / source).read_text(encoding="utf-8")))
        sites = [{"address": hex(address), "instruction": reason,
                  "category": "int3" if reason.split()[0] == "int3" else "unsupported",
                  "occurrences_with_overlap": count}
                 for (address, reason), count in sorted(occurrences.items())]
        counts = Counter(site["category"] for site in sites)
        modules.append({"name": spec["name"], "functions": report["functions"],
                        "trap_functions": len(report["functions_containing_traps"]),
                        "int3_sites": counts["int3"], "unsupported_sites": counts["unsupported"],
                        "sites": sites})
    result = {"generation_complete": not incomplete, "incomplete_modules": incomplete,
              "modules": modules, "scope": "Explicit void_lift_trap sites in generated C; not a proof of ISA coverage or reachability."}
    target = ROOT / "research/static-trap-audit.json"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    rows = ["# Explicit static trap audit", "", result["scope"], "",
            f"Complete generation: {result['generation_complete']}. Incomplete: {', '.join(incomplete) or 'none'}.", "",
            "A site is a unique guest address and reason inside one module. Overlapping function copies are deduplicated. `int3` remains an intentional runtime trap; separating it does not make it safe to ignore.", "",
            "| Module | Functions | Functions with traps | int3 sites | Other unsupported sites |",
            "| --- | ---: | ---: | ---: | ---: |"]
    for module in modules:
        rows.append(f"| {module['name']} | {module['functions']} | {module['trap_functions']} | {module['int3_sites']} | {module['unsupported_sites']} |")
    reasons = Counter(site["instruction"].split()[0] for module in modules for site in module["sites"] if site["category"] == "unsupported")
    rows += ["", "Unsupported instruction/reason groups (unique sites):", ""]
    rows += [f"- `{reason}`: {count}" for reason, count in reasons.most_common()]
    target.with_suffix(".md").write_text("\n".join(rows) + "\n", encoding="utf-8")
    print(f"Audited {len(modules)}/{len(specs)} modules; {sum(m['int3_sites'] for m in modules)} int3 sites, {sum(m['unsupported_sites'] for m in modules)} other explicit traps.")


if __name__ == "__main__":
    main()

"""Check a port's app icons against docs/ICONS.md.

    python tools/icons.py check GAME/android/app/res

For every adaptive icon (mipmap-anydpi-v26/*.xml): a v33 twin with a monochrome layer, vector layers
only, an opaque full-canvas background, and foreground/monochrome geometry inside the 66 dp safe
circle (radius 33 around 54,54 on the 108 dp canvas; up to 36 is a warning, beyond is an error).
Curves and arcs of the path data are sampled. Standard library only.
"""
import argparse
import math
from pathlib import Path
import re
import sys
import xml.etree.ElementTree as ET

A = "{http://schemas.android.com/apk/res/android}"
SAFE, VISIBLE, CENTER = 33.0, 36.0, 54.0


def tokens(d):
    return re.findall(r"[MmLlHhVvCcSsQqTtAaZz]|-?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?", d)


def arc(x1, y1, rx, ry, phi, large, sweep, x2, y2):
    """SVG arc from endpoint to center parameterisation, sampled."""
    if rx == 0 or ry == 0:
        return [(x2, y2)]
    p = math.radians(phi)
    c, s = math.cos(p), math.sin(p)
    dx, dy = (x1 - x2) / 2, (y1 - y2) / 2
    x1p, y1p = c * dx + s * dy, -s * dx + c * dy
    rx, ry = abs(rx), abs(ry)
    scale = x1p ** 2 / rx ** 2 + y1p ** 2 / ry ** 2
    if scale > 1:
        rx, ry = rx * math.sqrt(scale), ry * math.sqrt(scale)
    num = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p
    den = rx * rx * y1p * y1p + ry * ry * x1p * x1p
    co = math.sqrt(max(0.0, num / den)) if den else 0.0
    if large == sweep:
        co = -co
    cxp, cyp = co * rx * y1p / ry, -co * ry * x1p / rx
    cx, cy = c * cxp - s * cyp + (x1 + x2) / 2, s * cxp + c * cyp + (y1 + y2) / 2

    def angle(ux, uy, vx, vy):
        return math.atan2(ux * vy - uy * vx, ux * vx + uy * vy)
    t1 = angle(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry)
    dt = angle((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry)
    if not sweep and dt > 0:
        dt -= 2 * math.pi
    if sweep and dt < 0:
        dt += 2 * math.pi
    return [(cx + rx * math.cos(t1 + dt * i / 64) * c - ry * math.sin(t1 + dt * i / 64) * s,
             cy + rx * math.cos(t1 + dt * i / 64) * s + ry * math.sin(t1 + dt * i / 64) * c) for i in range(1, 65)]


def points(d):
    """Points along a VectorDrawable path (M L H V C S Q T A Z, absolute and relative)."""
    t = tokens(d)
    i, x, y, sx, sy, cmd, out = 0, 0.0, 0.0, 0.0, 0.0, None, []
    last_control = None

    def num():
        nonlocal i
        i += 1
        return float(t[i - 1])
    while i < len(t):
        if re.match(r"[A-Za-z]", t[i]):
            cmd = t[i]
            i += 1
        rel, kind = cmd.islower(), cmd.upper()
        ox, oy = (x, y) if rel else (0.0, 0.0)
        if kind == "Z":
            x, y = sx, sy
            out.append((x, y))
            last_control = None
            continue
        if kind == "M":
            x, y = num() + ox, num() + oy
            sx, sy = x, y
            out.append((x, y))
            cmd = "l" if rel else "L"
            last_control = None
        elif kind == "L":
            x, y = num() + ox, num() + oy
            out.append((x, y))
            last_control = None
        elif kind == "H":
            x = num() + ox
            out.append((x, y))
            last_control = None
        elif kind == "V":
            y = num() + (y if rel else 0.0)
            out.append((x, y))
            last_control = None
        elif kind in "QCST":
            if kind in "ST":
                reflected = (2 * x - last_control[0], 2 * y - last_control[1]) if last_control else (x, y)
                ctrl = [reflected] + [(num() + ox, num() + oy) for _ in range(2 if kind == "S" else 1)]
            else:
                ctrl = [(num() + ox, num() + oy) for _ in range(2 if kind == "Q" else 3)]
            pts = [(x, y)] + ctrl
            degree = len(pts) - 1
            for k in range(1, 65):
                u = k / 64
                out.append((sum(math.comb(degree, j) * (1 - u) ** (degree - j) * u ** j * pts[j][0] for j in range(degree + 1)),
                            sum(math.comb(degree, j) * (1 - u) ** (degree - j) * u ** j * pts[j][1] for j in range(degree + 1))))
            last_control = pts[-2]
            x, y = pts[-1]
        elif kind == "A":
            rx, ry, phi, large, sweep = num(), num(), num(), int(num()), int(num())
            x2, y2 = num() + ox, num() + oy
            out += arc(x, y, rx, ry, phi, large, sweep, x2, y2)
            x, y = x2, y2
            last_control = None
        else:
            raise ValueError("unsupported path command " + kind)
    return out


def transformed(element, matrix=(1, 0, 0, 1, 0, 0)):
    """Paths with the group transforms above them (scale, rotation, translation about a pivot)."""
    for child in element:
        if child.tag == "group":
            px, py = float(child.get(A + "pivotX", 0)), float(child.get(A + "pivotY", 0))
            sx, sy = float(child.get(A + "scaleX", 1)), float(child.get(A + "scaleY", 1))
            r = math.radians(float(child.get(A + "rotation", 0)))
            tx, ty = float(child.get(A + "translateX", 0)), float(child.get(A + "translateY", 0))
            c, s = math.cos(r), math.sin(r)
            local = (sx * c, sx * s, -sy * s, sy * c, 0, 0)
            ex = px + tx - (local[0] * px + local[2] * py)
            ey = py + ty - (local[1] * px + local[3] * py)
            local = (local[0], local[1], local[2], local[3], ex, ey)
            a, b, cc, d, e, f = matrix
            combined = (a * local[0] + cc * local[1], b * local[0] + d * local[1], a * local[2] + cc * local[3],
                        b * local[2] + d * local[3], a * local[4] + cc * local[5] + e, b * local[4] + d * local[5] + f)
            yield from transformed(child, combined)
        elif child.tag == "path" and child.get(A + "pathData"):
            a, b, c, d, e, f = matrix
            yield [(a * px + c * py + e, b * px + d * py + f) for px, py in points(child.get(A + "pathData"))]


def reach(drawable):
    root = ET.parse(drawable).getroot()
    width = float(root.get(A + "viewportWidth", 108))
    scale = 108.0 / width
    worst, at = 0.0, None
    for path in transformed(root):
        for px, py in path:
            distance = math.hypot(px * scale - CENTER, py * scale - CENTER)
            if distance > worst:
                worst, at = distance, (round(px, 2), round(py, 2))
    return worst, at


def drawable(res, reference):
    if not reference or not reference.startswith("@drawable/"):
        return None
    name = reference.split("/", 1)[1]
    return res / "drawable" / (name + ".xml")


def check(res):
    errors, warnings, icons = [], [], 0
    rasters = [p for p in res.rglob("*") if p.suffix.lower() in (".png", ".webp", ".jpg") and "mipmap" in p.parent.name]
    for raster in rasters:
        warnings.append(f"{raster.relative_to(res)}: raster launcher icon (minSdk 26 needs only the adaptive one)")
    for icon in sorted((res / "mipmap-anydpi-v26").glob("*.xml")):
        icons += 1
        name = icon.relative_to(res)
        root = ET.parse(icon).getroot()
        layers = {child.tag: child.get(A + "drawable") for child in root}
        if "background" not in layers or "foreground" not in layers:
            errors.append(f"{name}: needs a background and a foreground")
            continue
        twin = res / "mipmap-anydpi-v33" / icon.name
        mono = None
        if twin.is_file():
            mono = {child.tag: child.get(A + "drawable") for child in ET.parse(twin).getroot()}.get("monochrome")
        elif "monochrome" in layers:
            warnings.append(f"{name}: keep the monochrome layer in mipmap-anydpi-v33")
            mono = layers["monochrome"]
        if not mono:
            errors.append(f"{name}: no monochrome layer (themed icons, Android 13+)")
        background = drawable(res, layers["background"])
        if background is not None:
            if not background.is_file():
                errors.append(f"{name}: {layers['background']} is not a vector drawable of res/drawable")
            else:
                full = any(re.fullmatch(r"\s*M\s*0[ ,]+0\s*h\s*108\s*v\s*108\s*h\s*-108\s*z\s*", p.get(A + "pathData", ""), re.I)
                           or "M0,0h108v108h-108z" in p.get(A + "pathData", "").replace(" ", "")
                           for p in ET.parse(background).getroot().iter("path"))
                if not full:
                    warnings.append(f"{name}: the background should cover the whole 108 dp canvas (M0,0h108v108h-108z)")
        elif not (layers["background"] or "").startswith("@color/"):
            errors.append(f"{name}: the background must be a colour or a vector drawable")
        for layer, reference in (("foreground", layers["foreground"]), ("monochrome", mono)):
            path = drawable(res, reference)
            if path is None:
                if reference:
                    errors.append(f"{name}: the {layer} must be a vector drawable")
                continue
            if not path.is_file():
                errors.append(f"{name}: {reference} is missing")
                continue
            distance, at = reach(path)
            text = f"{name} {layer}: {distance:.2f} dp from the centre at {at}"
            if distance > VISIBLE:
                errors.append(text + " (beyond the 72 dp visible area)")
            elif distance > SAFE:
                warnings.append(text + " (outside the 66 dp safe circle: masks may clip it)")
            else:
                print("ok   " + text)
    if icons == 0:
        errors.append("no adaptive icons in mipmap-anydpi-v26")
    for w in warnings:
        print("warn " + w)
    for e in errors:
        print("FAIL " + e)
    return not errors


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    c = commands.add_parser("check")
    c.add_argument("res", type=Path)
    args = parser.parse_args()
    sys.exit(0 if check(args.res) else 1)


if __name__ == "__main__":
    main()

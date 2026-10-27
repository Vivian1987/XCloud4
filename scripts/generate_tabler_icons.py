#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Offline Tabler Icons SVG-to-bitmap alpha atlas generator.

Reads official Tabler Icons v3.24.0 SVGs from assets/icons/tabler/, verifies
their SHA-256 hashes, and rasterizes them with 4x4 subpixel antialiasing into
src/ui/tabler_icons_atlas.h (24x24 standard icons + 56x56 large user icon).
"""
import hashlib
import math
import pathlib
import re
import xml.etree.ElementTree as ET

MANIFEST = {
    "settings.svg": ("X4_ICON_SETTINGS", "98f10cef0571a9bbaf954c8a5d368107c247215fad68bc62a6b2c7dfc0e29642"),
    "user.svg": ("X4_ICON_USER", "d3ef20fbd4eb1eb2d7b03f6c5180ce07bb259188b2f6f3aa1ab708b4a93a5d88"),
    "search.svg": ("X4_ICON_SEARCH", "383fab23bc3ce760f47be13a055f28256a68469fb4cf9d8ca33fcb09bcd53f51"),
    "info-circle.svg": ("X4_ICON_INFO", "4ec9a20592f2de6ae26dd315885ce7419a9b0bd8bcafee7c1276b773fae4cc67"),
    "heart.svg": ("X4_ICON_HEART", "1e7501724e159b4df06ae32c6c5525cd7839fe0f72f988c3b03c807922dd6df7"),
    "home.svg": ("X4_ICON_HOME", "364f70af3c90a9ef4178610227bf68c0b43b897241f146a8e56faf9724cc9c42"),
    "chevron-right.svg": ("X4_ICON_CHEVRON_RIGHT", "00e81f3a02f69089a08d5021086bf9ac1464f4ba514773908f66f777dbf9adb0"),
    "device-desktop.svg": ("X4_ICON_DEVICE_DESKTOP", "be05c34d7226f65aaf896f5e5b02a3e6ee60b3d2bef5cd375ee15b4be1e6779e"),
    "server.svg": ("X4_ICON_SERVER", "81fb60acc5c1dfd24ad8a32cd570263687d16f8a896c3ca9ca06a8ff312618e4"),
}

TOKEN_RE = re.compile(r"([MmLlHhVvCcAaZz])|([-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?)")


def arc_to_segments(x1, y1, rx, ry, phi_deg, fa, fs, x2, y2, steps=24):
    if (x1 == x2 and y1 == y2) or rx == 0 or ry == 0:
        return [((x1, y1), (x2, y2))]
    rx, ry = abs(rx), abs(ry)
    phi = math.radians(phi_deg % 360.0)
    cos_p, sin_p = math.cos(phi), math.sin(phi)
    dx2, dy2 = (x1 - x2) / 2.0, (y1 - y2) / 2.0
    x1p = cos_p * dx2 + sin_p * dy2
    y1p = -sin_p * dx2 + cos_p * dy2
    lam = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry)
    if lam > 1.0:
        s = math.sqrt(lam)
        rx *= s
        ry *= s
    num = max(0.0, rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p)
    den = rx * rx * y1p * y1p + ry * ry * x1p * x1p
    coef = math.sqrt(num / den) if den > 1e-12 else 0.0
    if bool(fa) == bool(fs):
        coef = -coef
    cxp = coef * (rx * y1p / ry)
    cyp = coef * (-ry * x1p / rx)
    cx = cos_p * cxp - sin_p * cyp + (x1 + x2) / 2.0
    cy = sin_p * cxp + cos_p * cyp + (y1 + y2) / 2.0

    def vec_angle(ux, uy, vx, vy):
        dot = ux * vx + uy * vy
        mag = math.hypot(ux, uy) * math.hypot(vx, vy)
        ang = math.acos(max(-1.0, min(1.0, dot / mag))) if mag > 1e-12 else 0.0
        return -ang if (ux * vy - uy * vx) < 0 else ang

    vx1, vy1 = (x1p - cxp) / rx, (y1p - cyp) / ry
    vx2, vy2 = (-x1p - cxp) / rx, (-y1p - cyp) / ry
    theta1 = vec_angle(1.0, 0.0, vx1, vy1)
    dtheta = vec_angle(vx1, vy1, vx2, vy2)
    if not fs and dtheta > 0:
        dtheta -= 2.0 * math.pi
    elif fs and dtheta < 0:
        dtheta += 2.0 * math.pi

    segs = []
    px, py = x1, y1
    for i in range(1, steps + 1):
        t = theta1 + dtheta * (i / steps)
        ct, st = math.cos(t), math.sin(t)
        nx = cos_p * rx * ct - sin_p * ry * st + cx
        ny = sin_p * rx * ct + cos_p * ry * st + cy
        segs.append(((px, py), (nx, ny)))
        px, py = nx, ny
    return segs


def parse_svg_segments(svg_text: str):
    root = ET.fromstring(svg_text)
    segments = []
    for elem in root.iter():
        if not elem.tag.endswith("path"):
            continue
        d = elem.attrib.get("d", "")
        tokens = [m.group(1) or float(m.group(2)) for m in TOKEN_RE.finditer(d)]
        idx = 0
        cmd = None
        cx = cy = sx = sy = 0.0
        while idx < len(tokens):
            if isinstance(tokens[idx], str):
                cmd = tokens[idx]
                idx += 1
            if cmd in ("Z", "z"):
                if (cx, cy) != (sx, sy):
                    segments.append(((cx, cy), (sx, sy)))
                cx, cy = sx, sy
                continue
            if cmd in ("M", "m"):
                x, y = tokens[idx], tokens[idx + 1]
                idx += 2
                if cmd == "m":
                    x += cx
                    y += cy
                cx, cy = sx, sy = x, y
                cmd = "L" if cmd == "M" else "l"
            elif cmd in ("L", "l"):
                x, y = tokens[idx], tokens[idx + 1]
                idx += 2
                if cmd == "l":
                    x += cx
                    y += cy
                segments.append(((cx, cy), (x, y)))
                cx, cy = x, y
            elif cmd in ("H", "h"):
                x = tokens[idx]
                idx += 1
                if cmd == "h":
                    x += cx
                segments.append(((cx, cy), (x, cy)))
                cx = x
            elif cmd in ("V", "v"):
                y = tokens[idx]
                idx += 1
                if cmd == "v":
                    y += cy
                segments.append(((cx, cy), (cx, y)))
                cy = y
            elif cmd in ("C", "c"):
                x1, y1, x2, y2, x, y = tokens[idx : idx + 6]
                idx += 6
                if cmd == "c":
                    x1 += cx; y1 += cy
                    x2 += cx; y2 += cy
                    x += cx; y += cy
                px, py = cx, cy
                for step in range(1, 17):
                    t = step / 16.0
                    mt = 1.0 - t
                    nx = mt**3 * cx + 3 * mt**2 * t * x1 + 3 * mt * t**2 * x2 + t**3 * x
                    ny = mt**3 * cy + 3 * mt**2 * t * y1 + 3 * mt * t**2 * y2 + t**3 * y
                    segments.append(((px, py), (nx, ny)))
                    px, py = nx, ny
                cx, cy = x, y
            elif cmd in ("A", "a"):
                rx, ry, rot, fa, fs, x, y = tokens[idx : idx + 7]
                idx += 7
                if cmd == "a":
                    x += cx
                    y += cy
                segments.extend(arc_to_segments(cx, cy, rx, ry, rot, int(fa), int(fs), x, y))
                cx, cy = x, y
            else:
                raise ValueError(f"Unsupported SVG command: {cmd}")
    return segments


def dist_point_to_seg_sq(px, py, ax, ay, bx, by):
    vx, vy = bx - ax, by - ay
    wx, wy = px - ax, py - ay
    c1 = wx * vx + wy * vy
    if c1 <= 0.0:
        return wx * wx + wy * wy
    c2 = vx * vx + vy * vy
    if c2 <= c1:
        dx, dy = px - bx, py - by
        return dx * dx + dy * dy
    t = c1 / c2
    dx, dy = px - (ax + t * vx), py - (ay + t * vy)
    return dx * dx + dy * dy


def rasterize_segments(segments, out_size: int, stroke_width: float = 2.0):
    half_stroke = stroke_width / 2.0
    scale = 24.0 / out_size
    aa_width = 0.85 * scale
    r_inner = max(0.0, half_stroke - aa_width * 0.5)
    r_outer = half_stroke + aa_width * 0.5
    r_outer_sq = r_outer * r_outer
    out = bytearray(out_size * out_size)
    for py in range(out_size):
        vy = (py + 0.5) * scale
        for px in range(out_size):
            vx = (px + 0.5) * scale
            min_d2 = 1e9
            for (ax, ay), (bx, by) in segments:
                d2 = dist_point_to_seg_sq(vx, vy, ax, ay, bx, by)
                if d2 < min_d2:
                    min_d2 = d2
            if min_d2 >= r_outer_sq:
                alpha = 0
            else:
                d = math.sqrt(min_d2)
                if d <= r_inner:
                    alpha = 255
                else:
                    t = (r_outer - d) / (r_outer - r_inner)
                    alpha = int(round(max(0.0, min(1.0, t)) * 255.0))
            out[py * out_size + px] = alpha
    return bytes(out)


def format_c_array(name: str, data: bytes) -> str:
    rows = [f"static const unsigned char {name}[{len(data)}] = {{"]
    for i in range(0, len(data), 24):
        chunk = ", ".join(f"0x{b:02x}" for b in data[i : i + 24])
        rows.append(f"    {chunk},")
    rows.append("};")
    return "\n".join(rows)


def main():
    repo = pathlib.Path(__file__).resolve().parents[1]
    svg_dir = repo / "assets" / "icons" / "tabler"
    out_header = repo / "src" / "ui" / "tabler_icons_atlas.h"

    blocks = [
        "/* SPDX-License-Identifier: GPL-3.0-only */",
        "/* Generated by scripts/generate_tabler_icons.py from official Tabler Icons v3.24.0 (MIT). */",
        "/* See docs/licenses/Tabler-Icons-MIT.txt. Do not edit manually. */",
        "#pragma once",
        "#include <stdint.h>",
        "",
        "enum X4TablerIconId {",
        "    X4_ICON_SETTINGS = 0,",
        "    X4_ICON_USER = 1,",
        "    X4_ICON_SEARCH = 2,",
        "    X4_ICON_INFO = 3,",
        "    X4_ICON_HEART = 4,",
        "    X4_ICON_HOME = 5,",
        "    X4_ICON_CHEVRON_RIGHT = 6,",
        "    X4_ICON_DEVICE_DESKTOP = 7,",
        "    X4_ICON_SERVER = 8,",
        "    X4_ICON_COUNT = 9",
        "};",
        "",
    ]

    array_names_24 = []
    user_56_data = None

    for filename, (enum_name, expected_sha) in MANIFEST.items():
        raw = (svg_dir / filename).read_bytes()
        actual_sha = hashlib.sha256(raw).hexdigest()
        if actual_sha != expected_sha:
            raise ValueError(f"SHA-256 mismatch for {filename}: {actual_sha} != {expected_sha}")
        segs = parse_svg_segments(raw.decode("utf-8"))
        bmp24 = rasterize_segments(segs, 24, stroke_width=2.0)
        cname = f"x4_tabler_{enum_name.lower()}_24"
        array_names_24.append(cname)
        blocks.append(format_c_array(cname, bmp24))
        blocks.append("")
        if filename == "user.svg":
            user_56_data = rasterize_segments(segs, 56, stroke_width=1.75)

    blocks.append(format_c_array("x4_tabler_user_56", user_56_data))
    blocks.append("")
    blocks.append("static const unsigned char *const x4_tabler_icons_24[X4_ICON_COUNT] = {")
    for cname in array_names_24:
        blocks.append(f"    {cname},")
    blocks.append("};")
    blocks.append("")

    out_header.write_text("\n".join(blocks), encoding="utf-8", newline="\n")
    print(f"Generated {out_header} ({out_header.stat().st_size} bytes)")


if __name__ == "__main__":
    main()

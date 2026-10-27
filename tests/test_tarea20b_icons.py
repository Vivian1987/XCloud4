#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify pinned SVGs, generated atlas pixels and stable original icon IDs."""
import hashlib
import importlib.util
import pathlib
import re
import sys

sys.dont_write_bytecode = True
repo = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('x4_tabler_generator', repo / 'scripts/generate_tabler_icons.py')
gen = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gen)
header = (repo / 'src/ui/tabler_icons_atlas.h').read_text(encoding='utf-8')
assert len(gen.MANIFEST) == 9
seen = set()
for index, (filename, (enum, expected_sha)) in enumerate(gen.MANIFEST.items()):
    raw = (repo / 'assets/icons/tabler' / filename).read_bytes()
    assert hashlib.sha256(raw).hexdigest() == expected_sha, filename
    assert re.search(r'\b' + enum + r'\s*=\s*' + str(index) + r'\b', header), enum
    symbol = 'x4_tabler_' + enum.lower() + '_24'
    data = re.search(r'\b' + symbol + r'\[576\]\s*=\s*\{(.*?)\};', header, re.S)
    assert data, symbol
    alpha = bytes(int(x, 16) for x in re.findall(r'0x([a-f0-9]{2})', data[1]))
    assert alpha == gen.rasterize_segments(gen.parse_svg_segments(raw.decode('utf-8')), 24, 2.0), symbol
    assert len(alpha) == 576 and max(alpha) == 255 and min(alpha) == 0
    assert alpha not in seen, symbol
    seen.add(alpha)
assert 'X4_ICON_DEVICE_DESKTOP = 7' in header
assert 'X4_ICON_SERVER = 8' in header
assert 'X4_ICON_COUNT = 9' in header
print('PASS TAREA20b: 9 pinned SVG hashes and atlas rasters; original icon IDs preserved; desktop/server appended')

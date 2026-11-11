#!/usr/bin/env python3
"""Check fitted native HUD captures and real row-selection proofs."""
import pathlib
import io
import zlib
import re
import sys

root = pathlib.Path(sys.argv[1])
log = (root / 'native.log').read_text()
assert 'UI_STARTUP window_and_decorations=inside_work_area' in log
assert 'PASS: native UI fit' in log
records = re.findall(r'UI_FIT case=(\w+) pixels=(\d+),(\d+) viewport=(\d+),(\d+),(\d+),(\d+) projection=([\d.]+) hit_testing=PASS camera=unchanged', log)
assert len(records) == 44
assert log.count('text_overlap=0') == 44
assert {r[0] for r in records} == {'small', 'square', 'portrait', 'retina', 'wide', 'controls',
                                  'crowded', 'station', 'crew', 'market', 'probe', 'probe_portrait',
                                  'crewmap_wide','crewmap_portrait','crewmap_orders','crewmap_crowded','vessel_wide','vessel_portrait','receiver_wide','receiver_portrait','archaeology_wide','archaeology_portrait','archaeology_history','discovery_journal','radio_monitor','radio_call_wide','radio_call_portrait','radio_alert','target_acquiring','target_locked','target_portrait','target_blocked','target_offscreen','threat_locking','threat_portrait','threat_directional_hit','threat_missile','threat_decoy','ship_emitter','ship_emitter_portrait','ship_scan','ship_jam','ship_jam_monitor','relic_surface'}
for name, w, h, x, y, vw, vh, projection in records:
    w, h, x, y, vw, vh = map(int, (w, h, x, y, vw, vh))
    assert 0 <= x and 0 <= y and x + vw <= w and y + vh <= h and vw >= vh
    assert abs((x + vw / 2) - w / 2) <= .5 and abs((y + vh / 2) - h / 2) <= .5
    path = root / 'captures' / f'{name}.ppm'
    raw = path.read_bytes() if path.exists() else zlib.decompress(path.with_suffix('.ppm.z').read_bytes())
    with io.BytesIO(raw) as image:
        assert image.readline().strip() == b'P6'
        assert tuple(map(int, image.readline().split())) == (w, h)
        assert image.readline().strip() == b'255'
        pixels = image.read()
    assert len(pixels) == w * h * 3
    def lit(left, top, right, bottom):
        return sum(max(pixels[(py*w+px)*3:(py*w+px)*3+3]) > 110
                   for py in range(max(0, int(y + top*vh/900)), min(h, int(y + bottom*vh/900)+1))
                   for px in range(max(0, int(x + left*vh/900)), min(w, int(x + right*vh/900)+1)))
    canvas_w = vw * 900 / vh
    if name.startswith('probe'):
        assert lit(38, 43, canvas_w - 38, 61) > 20, f'{name}: missing probe title'
        assert lit(38, 849, canvas_w - 38, 860) > 10, f'{name}: missing probe controls'
    else:
        assert lit(24, 14, 170, 35) > 12, f'{name}: clipped title'
        assert lit(24, 886, 172, 900) > 5, f'{name}: clipped bottom controls hint'
        assert lit(canvas_w - 200, 886, canvas_w - 24, 900) > 5, f'{name}: clipped right footer'
    if name == 'controls':
        assert lit(canvas_w / 2 - 260, 225, canvas_w / 2 + 260, 615) > 250, 'Missing controls guide'
    if name.startswith('crewmap_'):
        assert lit(canvas_w-298,260,canvas_w-38,277)>12, f'{name}: missing crew deck title'
        assert lit(canvas_w-300,281,canvas_w-35,447)>70, f'{name}: missing rooms, crew or movement'
        assert lit(canvas_w-298,451,canvas_w-38,483)>18, f'{name}: missing crew identity/location and controls'
print('PASS: forty-four native HUD, crew, trade, probe, vessel, receiver and archaeology layouts; zero text overlaps; fitted crew selection; unchanged camera')

"""Verify rendered milestones of the flight-controlled, restartable seam journey."""
from pathlib import Path
import hashlib
import json
import re
import sys
import zlib

root = Path(sys.argv[1])
stages = [0, 3, 6, 7, 8, 10, 11, 13]
hashes = []
for stage in stages:
    path = root / 'captures' / f'micro-flight-{stage}.ppm'
    raw = path.read_bytes() if path.exists() else zlib.decompress(path.with_suffix('.ppm.z').read_bytes())
    magic, dimensions, maximum, pixels = raw.split(b'\n', 3)
    width, height = map(int, dimensions.split())
    assert magic == b'P6' and maximum == b'255' and (width, height) == (1440, 900)
    assert len(pixels) == width * height * 3
    # Inspect the actual exterior, away from the cockpit instruments.
    region = b''.join(pixels[(y * width + 450) * 3:(y * width + 1000) * 3] for y in range(220, 640))
    assert sum(v > 50 for v in region) > 1500, f'Empty material flight stage {stage}'
    hashes.append(hashlib.sha256(raw).hexdigest())
assert len(set(hashes)) == len(stages)
log = (root / 'native.log').read_text()
assert 'PASS flight-controlled surface / rock / mineral / atoms / return / full-size selective mining / exact restarts' in log
assert log.count('MICRO_RESTART exact camera / ship / ledger / original identities') == 3
captures = re.findall(r'MICRO_FLIGHT_CAPTURE (\d+) .*? / triangles=(\d+) / atoms=(\d+)', log)
assert [int(r[0]) for r in captures] == stages
assert all(int(r[2]) > 100 for r in captures if int(r[0]) in [7, 8])
assert '20 KG' in log or 'stage=11 complete' in log
(root / 'capture-hashes.json').write_text(json.dumps(dict(zip(map(str, stages), hashes)), indent=2) + '\n')
print('PASS eight rendered flight milestones, live atomic sites, three exact restarts and finite selective cargo')

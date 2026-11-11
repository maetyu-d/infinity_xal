#!/usr/bin/env python3
"""Check native reflection captures made from the same saved berth and frame.

Example:
  python3 tools/check_reflections.py --on on/flight.ppm --off off/flight.ppm
"""
import argparse
import json
from PIL import Image, ImageChops, ImageStat

p = argparse.ArgumentParser(__doc__)
p.add_argument("--on", required=True)
p.add_argument("--off", required=True)
p.add_argument("--report")
args = p.parse_args()
enabled = Image.open(args.on).convert("RGB")
disabled = Image.open(args.off).convert("RGB")
if enabled.size != disabled.size:
    raise SystemExit("Capture sizes differ")
w, h = enabled.size
delta = ImageChops.difference(enabled, disabled)
floor = (int(w*.18), int(h*.56), int(w*.78), int(h*.82))
title = (int(w*.015), int(h*.008), int(w*.14), int(h*.038))
floor_rms = ImageStat.Stat(delta.crop(floor)).rms
hud_rms = ImageStat.Stat(delta.crop(title)).rms
report = dict(size=[w,h], floor_rgb_rms=floor_rms, hud_rgb_rms=hud_rms,
              changed_bounds=delta.getbbox())
if max(floor_rms) < 1.0:
    raise SystemExit("Reflections did not measurably change the receiving floor")
if max(hud_rms) > .5:
    raise SystemExit("The comparison changed the HUD; use the same save and frame")
if args.report:
    from pathlib import Path
    Path(args.report).write_text(json.dumps(report,indent=2)+"\n")
print(json.dumps(report,indent=2))

#!/usr/bin/env python3
"""Compare matching native lens captures while checking that the HUD stays sharp."""
import argparse
import json
from pathlib import Path
from PIL import Image, ImageChops, ImageStat

p = argparse.ArgumentParser(__doc__)
p.add_argument("--on", required=True)
p.add_argument("--off", required=True)
p.add_argument("--report")
a = p.parse_args()
on = Image.open(a.on).convert("RGB")
off = Image.open(a.off).convert("RGB")
if on.size != off.size:
    raise SystemExit("Capture sizes differ")
w, h = on.size
world = (int(w*.20), int(h*.20), int(w*.78), int(h*.82))
title = (int(w*.015), int(h*.008), int(w*.14), int(h*.038))
delta = ImageChops.difference(on, off)
scene_rms = ImageStat.Stat(delta.crop(world)).rms
hud_rms = ImageStat.Stat(delta.crop(title)).rms
if max(scene_rms) < 2:
    raise SystemExit("The lens did not visibly change the scene")
if max(hud_rms) > .5:
    raise SystemExit("The HUD changed; use matching captures")
pixels = list(on.crop(world).getdata())
dark = sum(.2126*r+.7152*g+.0722*b < 15 for r, g, b in pixels)/len(pixels)
yellow = sum(r > 210 and g > 170 and b < 230 for r, g, b in pixels)/len(pixels)
if dark < .2 or yellow < .001:
    raise SystemExit("The capture lost the requested deep shadows or hot highlights")
result = dict(size=on.size, scene_rgb_rms=scene_rms, hud_rgb_rms=hud_rms,
              dark_fraction=dark, yellow_fraction=yellow)
if a.report:
    Path(a.report).write_text(json.dumps(result, indent=2)+"\n")
print(json.dumps(result, indent=2))

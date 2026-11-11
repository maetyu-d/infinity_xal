#!/usr/bin/env python3
"""Verify glass-space on/off captures and, optionally, a higher-contrast berth."""
import argparse
import json
from pathlib import Path
from PIL import Image, ImageChops, ImageStat

p=argparse.ArgumentParser(__doc__)
p.add_argument("--on",required=True)
p.add_argument("--off",required=True)
p.add_argument("--reference")
p.add_argument("--current")
p.add_argument("--report")
a=p.parse_args()
on=Image.open(a.on).convert("RGB");off=Image.open(a.off).convert("RGB")
if on.size!=off.size:raise SystemExit("Capture sizes differ")
w,h=on.size
roi=(int(w*.20),int(h*.20),int(w*.78),int(h*.82))
title=(int(w*.015),int(h*.008),int(w*.14),int(h*.038))
delta=ImageChops.difference(on,off)
rms=ImageStat.Stat(delta.crop(roi)).rms
hud=ImageStat.Stat(delta.crop(title)).rms
if max(rms)<5:raise SystemExit("Glass space did not visibly change the void")
if max(hud)>.5:raise SystemExit("The comparison changed the HUD; use matching captures")
def contrast(path):
 im=Image.open(path).convert("RGB")
 pixels=list(im.crop(roi).getdata())
 return dict(dark_fraction=sum(.2126*r+.7152*g+.0722*b<15 for r,g,b in pixels)/len(pixels),
             yellow_fraction=sum(r>210 and g>170 and b<230 for r,g,b in pixels)/len(pixels))
v=dict(space_rgb_rms=rms,hud_rgb_rms=hud,space_contrast=contrast(a.on))
if a.reference and a.current:
 v["previous_berth"]=contrast(a.reference);v["current_berth"]=contrast(a.current)
 if v["current_berth"]["dark_fraction"]<=v["previous_berth"]["dark_fraction"]:
  raise SystemExit("The berth shadows did not deepen")
 if v["current_berth"]["yellow_fraction"]<=v["previous_berth"]["yellow_fraction"]:
  raise SystemExit("The burning yellows did not increase")
if a.report:Path(a.report).write_text(json.dumps(v,indent=2)+"\n")
print(json.dumps(v,indent=2))

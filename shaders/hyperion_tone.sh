// Shared film curve and regional grades for the world and physical cockpit.
#include "hyperion_region_look.sh"
vec3 referenceFirePalette(float t){
 vec3 black=vec3(0.);
 vec3 soot=vec3(.075,.014,.005);       // deep umber
 vec3 charred=vec3(.220,.012,.006);    // blackened red
 vec3 crimson=vec3(.460,.025,.009);    // deep crimson
 vec3 flame=vec3(.880,.140,.012);      // scarlet
 vec3 orange=vec3(1.,.430,.018);     // burning orange
 vec3 gold=vec3(1.,.940,.075);         // yellow heat
 vec3 whiteHeat=vec3(1.,.995,.720);    // white-yellow heat
 if(t<.12)return mix(black,soot,smoothstep(0.,.12,t));
 if(t<.30)return mix(soot,charred,smoothstep(.12,.30,t));
 if(t<.48)return mix(charred,crimson,smoothstep(.30,.48,t));
 if(t<.65)return mix(crimson,flame,smoothstep(.48,.65,t));
 if(t<.80)return mix(flame,orange,smoothstep(.65,.80,t));
 if(t<.92)return mix(orange,gold,smoothstep(.80,.92,t));
 return mix(gold,whiteHeat,smoothstep(.92,1.,t));
}
vec3 charredSunset(vec3 c){
 float y=dot(c,vec3(.299,.587,.114)),peak=max(c.r,max(c.g,c.b));
 // Peak-aware brightness retains hot orange and gold in saturated emitters.
 float energy=pow(clamp(y*.58+peak*.42,0.,1.),1.08);
 // A steeper pivot separates deep wine/umber from incandescent yellow.
 float heat=clamp((energy-.45)*1.55+.45,0.,1.);
 vec3 warm=referenceFirePalette(heat);
 return clamp(mix(warm,c*vec3(1.,.60,.20),.04),vec3(0.),vec3(1.));
}
vec3 verdantPalette(float t){
 vec3 ink=vec3(.006,.019,.013);
 vec3 pine=vec3(.023,.135,.076);
 vec3 emerald=vec3(.045,.335,.170);
 vec3 fern=vec3(.19,.56,.27);
 vec3 lime=vec3(.55,.78,.25);
 vec3 paleSun=vec3(.96,.98,.57);
 if(t<.22)return mix(ink,pine,smoothstep(0.,.22,t));
 if(t<.43)return mix(pine,emerald,smoothstep(.22,.43,t));
 if(t<.63)return mix(emerald,fern,smoothstep(.43,.63,t));
 if(t<.83)return mix(fern,lime,smoothstep(.63,.83,t));
 return mix(lime,paleSun,smoothstep(.83,1.,t));
}
vec3 verdantGrade(vec3 c,float peak){
 float energy=clamp(dot(c,vec3(.24,.66,.10))*.72+max(c.r,max(c.g,c.b))*.28,0.,1.);
 vec3 grade=verdantPalette(pow(energy,.88));
 // Lamps and hot minerals retain copper and amber against the green air.
 float copper=smoothstep(.14,.58,c.r-c.g)*smoothstep(.24,.72,energy);
 grade=mix(grade,vec3(.82,.35,.08)*energy,copper*.66);
 return mix(grade,vec3(.98,1.,.64),smoothstep(2.,8.,peak)*.80);
}
vec3 tidalPalette(float t){
 vec3 ink=vec3(.005,.012,.027);
 vec3 navy=vec3(.017,.045,.095);
 vec3 cobalt=vec3(.051,.151,.27);
 vec3 blue=vec3(.12,.32,.45);
 vec3 sea=vec3(.34,.60,.71);
 vec3 foam=vec3(.87,.955,.98);
 if(t<.20)return mix(ink,navy,smoothstep(0.,.20,t));
 if(t<.40)return mix(navy,cobalt,smoothstep(.20,.40,t));
 if(t<.60)return mix(cobalt,blue,smoothstep(.40,.60,t));
 if(t<.81)return mix(blue,sea,smoothstep(.60,.81,t));
 return mix(sea,foam,smoothstep(.81,1.,t));
}
vec3 tidalGrade(vec3 c,float peak){
 float energy=clamp(dot(c,vec3(.22,.62,.16))*.59+max(c.r,max(c.g,c.b))*.41,0.,1.);
 vec3 grade=tidalPalette(pow(energy,.87));
 // Keep warm fixtures legible against the cool reflected light.
 float amber=smoothstep(.18,.68,c.r-c.b)*smoothstep(.37,.76,energy);
 grade=mix(grade,vec3(.92,.69,.32)*energy,amber*.56);
 return mix(grade,vec3(.89,.96,1.),smoothstep(2.,8.,peak)*.78);
}
vec3 acidPalette(float t){
 vec3 ink=vec3(.010,.020,.029);
 vec3 plum=vec3(.19,.029,.115);
 vec3 raspberry=vec3(.70,.072,.36);
 vec3 fuchsia=vec3(.97,.23,.60);
 vec3 rose=vec3(1.,.61,.82);
 vec3 bleach=vec3(1.,.94,.96);
 if(t<.23)return mix(ink,plum,smoothstep(0.,.23,t));
 if(t<.43)return mix(plum,raspberry,smoothstep(.23,.43,t));
 if(t<.60)return mix(raspberry,fuchsia,smoothstep(.43,.60,t));
 if(t<.81)return mix(fuchsia,rose,smoothstep(.60,.81,t));
 return mix(rose,bleach,smoothstep(.81,1.,t));
}
vec3 acidGrade(vec3 c,float peak){
 float energy=clamp(dot(c,vec3(.30,.58,.12))*.65+max(c.r,max(c.g,c.b))*.35,0.,1.);
 float t=pow(energy,.94);
 vec3 grade=acidPalette(t);
 // Slate stays cool in shadow. The stain retains pink down into the pits.
 float stain=smoothstep(.025,.32,c.r-c.g);
 float shadow=(1.-smoothstep(.22,.58,t))*(1.-stain*.85);
 vec3 slate=mix(vec3(.009,.018,.026),vec3(.24,.31,.34),smoothstep(.04,.53,t));
 grade=mix(grade,slate,shadow*.96);
 // Sulphur follows the mineral and light colour, independently of exposure.
 float sulphur=smoothstep(.045,.20,(c.g-c.b)/max(c.r,.08))*smoothstep(.22,.42,t);
 vec3 yellow=mix(vec3(.51,.47,.075),vec3(1.,.971,.27),smoothstep(.32,.76,t));
 yellow=mix(yellow,vec3(1.,.99,.68),smoothstep(.77,1.,t));
 grade=mix(grade,yellow,sulphur);
 return mix(grade,vec3(1.,.99,.76),smoothstep(2.,8.,peak)*.84);
}
vec3 toneColour(vec3 c,float built){
 float brightness=dot(c,vec3(.299,.587,.114));c=max(vec3(0.),mix(vec3(brightness),c,mix(1.28,1.06,built)));
 c=max(c*1.36,vec3(0.));
 float peak=max(c.r,max(c.g,c.b)),mappedPeak=(peak*(2.51*peak+.03))/(peak*(2.43*peak+.59)+.14);
 vec3 film=(c*(2.51*c+.03))/(c*(2.43*c+.59)+.14);
 c=clamp(mix(film,c/max(peak,.0001)*mappedPeak,.86),vec3(0.),vec3(1.));
 c=pow(c,vec3(1./2.2));
 if(built>=.999)return c;
 // Retain the small incandescent range above the ordinary surface exposure.
 vec3 grade=vec3(0.);
 if(u_regionLook[1].y>0.)
  grade+=mix(charredSunset(c),vec3(1.,.995,.64),smoothstep(2.,8.,peak)*.90)*u_regionLook[1].y;
 if(u_regionLook[0].x>0.)grade+=verdantGrade(c,peak)*u_regionLook[0].x;
 if(u_regionLook[1].x>0.)grade+=tidalGrade(c,peak)*u_regionLook[1].x;
 if(u_regionLook[1].z>0.)grade+=acidGrade(c,peak)*u_regionLook[1].z;
 return mix(grade,c,built);
}
vec3 toneColour(vec3 c){
 return toneColour(c,0.);
}

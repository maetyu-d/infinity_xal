#ifndef HYPERION_ACID
#define HYPERION_ACID

// Broad oxidation, chipped mineral crust and fine pits share the material's
// local coordinates. The filtered noise resolves each layer as it gets close.
vec3 acidEtching(vec3 p){
 float grain=strataNoise(p*11.6+vec3(6.,13.,-4.),vec3(0.));
 float chips=strataNoise(p*3.1+vec3(-5.,8.,17.),vec3(0.));
 float broad=strataNoise(p,vec3(0.));
 float reaction=broad*.60+chips*.29+grain*.11;
 float crust=smoothstep(.48,.70,reaction);
 float oxide=smoothstep(.26,.55,reaction);
 float pits=smoothstep(.46,.81,grain)*(.25+.75*chips);
 return vec3(crust,oxide,pits);
}

vec3 acidMineral(vec3 etch){
 vec3 slate=vec3(.032,.047,.058);
 vec3 oxide=vec3(.26,.038,.145);
 vec3 chalk=vec3(.79,.76,.15);
 float chipped=etch.x*smoothstep(.11,.67,etch.x);
 return mix(mix(slate,oxide,etch.y*etch.y),chalk,chipped)*(1.-etch.z*.61);
}

vec3 acidSky(vec3 ray,float phase){
 vec3 p=ray*vec3(4.1,5.7,4.1)+vec3(9.,18.,7.+phase*12.);
 float warp=strataNoiseSample(p*.67,vec3(0.));
 vec3 grainFrame=p+vec3(.63,-.92,.42)*(warp-.5);
 vec3 etch=acidEtching(grainFrame);
 float flakes=strataNoise(grainFrame*7.8+vec3(6.,-5.,3.),vec3(0.));
 float exposure=.49+dot(ray,vec3(.49,.25,-.15))*.68;
 float stain=smoothstep(.19,.67,exposure+(etch.y-.5)*.58);
 float bleach=smoothstep(.49,.86,exposure+(etch.x-.35)*.70);
 bleach*=smoothstep(.24,.72,warp*.46+flakes*.29+etch.x*.25)*.82;
 float lee=clamp(.23+.77*etch.y-.33*etch.z,0.,1.);
 vec3 sky=mix(vec3(.003,.008,.016),vec3(.61,.030,.276),stain*lee);
 sky=mix(sky,vec3(1.26,1.14,.13),bleach*(.42+flakes*.58));
 float edge=bleach*(1.-bleach)*4.*smoothstep(.39,.64,flakes);
 sky+=vec3(.26,.047,.12)*edge*stain;
 float grains=strataNoise(grainFrame*23.4+vec3(2.,3.,9.),vec3(0.));
 return sky*(.78+grains*.31-etch.z*.22);
}
#endif

#ifndef HYPERION_LAVA_PROVINCES
#define HYPERION_LAVA_PROVINCES
vec3 lavaCellHash(vec3 p){
 p=vec3(dot(p,vec3(127.1,311.7,74.7)),dot(p,vec3(269.5,183.3,246.1)),dot(p,vec3(113.5,271.9,124.6)));
 return fract(sin(p)*43758.5453);
}
float lavaPlateGap(vec3 p){
 vec3 cell=floor(p),local=fract(p);
 float nearest=10.,second=10.;
 for(int z=-1;z<=1;++z)for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){
  vec3 offset=vec3(float(x),float(y),float(z));
  vec3 delta=offset+.20+lavaCellHash(cell+offset)*.60-local;
  float distance=dot(delta,delta);
  second=min(second,max(nearest,distance));nearest=min(nearest,distance);
 }
 return sqrt(second)-sqrt(nearest);
}
vec3 lavaProvinces(vec3 lit,vec3 n,float phase,float temperature,float day){
 vec3 p=n*5.8+vec3(phase*29.,13.,4.);
 float continent=strataNoise(p,vec3(0.));
 float drift=strataNoise(p*2.19+vec3(7.,continent*2.,3.),vec3(0.));
 float crust=strataNoise(p*6.7+vec3(drift*2.,3.,continent),vec3(0.));
 float granular=strataNoise(p*21.3+crust*.6,vec3(0.));
 float province=continent*.76+drift*.24;
 float cooled=smoothstep(.39,.57,province);
 float pool=1.-smoothstep(.32,.48,province);
 float gap=lavaPlateGap(p*1.37+vec3(drift,crust,continent)*.67);
 float footprint=max(.003,fwidth(gap));
 float channel=1.-smoothstep(.009,.027+footprint,gap);
 channel*=1.-smoothstep(.10,.30,footprint);
 float margin=exp(-gap/.095);
 float fineFootprint=max(.002,fwidth(crust));
 float hair=1.-smoothstep(.004,.013+fineFootprint,abs(crust-.50+drift*.12));
 hair*=(1.-smoothstep(.04,.16,fineFootprint))*smoothstep(.41,.66,province);
 float rafts=smoothstep(.40,.69,crust)*(.50+.50*cooled);
 float flow=pool*(1.-rafts*.72);
 // Broad cooling crust has a quiet dark body; incandescent river cores
 // are narrow enough for the surrounding red cooling margins to read.
 vec3 basalt=lit*(.40+.47*cooled)*(.63+crust*.36+granular*.11);
 basalt+=vec3(.004,.0014,.0007)*cooled*day;
 float thermal=clamp(pow(max(0.,temperature)/1400.,4.),0.,4.);
 float energy=flow*(.014+.035*(1.-crust))+channel*(.020+.034*(1.-cooled))+hair*.003;
 float core=pow(channel,5.)*(.44+.56*crust)+flow*pow(1.-rafts,4.)*.49;
 vec3 red=vec3(1.48,.038,.003),orange=vec3(2.10,.36,.018),yellow=vec3(2.75,1.12,.16);
 vec3 heat=mix(red,orange,smoothstep(.12,.70,flow+channel*.6));
 heat=mix(heat,yellow,clamp(core,0.,1.));
 // Filtered pressure wrinkles live only on the hot flow, leaving the
 // large plates calm and preventing a globe-wide contour pattern.
 float ropePhase=drift*47.+crust*4.1;
 float rope=mix(.5,.5+.5*cos(ropePhase),1.-smoothstep(.5,1.8,fwidth(ropePhase)));
 energy*=.82+.27*rope;
 return basalt+heat*thermal*(.00035+energy)+red*thermal*margin*.0025;
}
#endif

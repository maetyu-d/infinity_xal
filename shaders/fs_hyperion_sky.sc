$input v_texcoord0
#include <bgfx_shader.sh>
uniform vec4 u_sky[2];
uniform mat4 u_skyRotation;
uniform vec4 u_spaceGlass;
#include "hyperion_region_look.sh"
#include "hyperion_noise.sh"
#include "hyperion_space_glass.sh"
#include "hyperion_tidal.sh"
#include "hyperion_acid.sh"

// Accumulate emission through a folded dust lane. A sample on the near side
// attenuates the deeper samples, so the lanes have dark cores and bright rims.
vec3 nebula(vec3 d, float phase, float density) {
 vec3 seed=vec3(9.1+phase*15.,31.7-phase*11.,7.8+phase*23.);
 vec3 axis=normalize(vec3(.32,.81,-.29));
 float warp=strataNoiseSample(d*3.1+seed,vec3(0.));
 float curl=strataNoiseSample(d*4.7+seed.yzx,vec3(0.));
 float laneAxis=dot(d,axis)+.46*(warp-.5)+.18*sin(d.z*4.8+d.x*3.6+phase*6.283);
 float lane=exp(-laneAxis*laneAxis*6.8);
 vec3 bend=vec3(warp-.5,curl-.5,warp-curl)*.95;
 vec3 result=vec3(0.);
 float transmission=1.;
 for(int sampleIndex=0;sampleIndex<6;++sampleIndex){
  float i=float(sampleIndex);
  vec3 p=d*(2.0+i*.81)+seed+bend*(1.0+i*.13);
  float broad=strataNoiseSample(p,vec3(0.));
  float filaments=strataNoiseSample(p*2.72+vec3(13.6,3.7,4.6),vec3(0.));
  float knots=strataNoiseSample(p*5.9+vec3(3.,6.3,17.),vec3(0.));
  float body=smoothstep(.36,.69,broad*.60+filaments*.29+knots*.11);
  float rim=4.*body*(1.-body)*smoothstep(.31,.71,filaments);
  float nearSide=strataNoiseSample(p+vec3(-.27,.22,.13),vec3(0.));
  float light=clamp(.14+.86*rim+max(0.,broad-nearSide)*1.4,0.,1.);
  float column=body*(.12+.88*lane)*density;
  float opacity=1.-exp(-column*(.41+.22*(1.-light)));
  vec3 soot=vec3(.014,.0029,.0022),ember=vec3(.46,.055,.009),ion=vec3(1.05,.33,.034);
  vec3 glow=mix(mix(soot,ember,light*light),ion,pow(light,5.)*.73);
  result+=transmission*opacity*glow;
  transmission*=1.-opacity;
 }
 return result+transmission*vec3(.0019,.00085,.0010);
}
void main(){
 vec2 ndc=v_texcoord0*2.-1.;
 vec3 ray=normalize(vec3(ndc.x*u_sky[1].x,ndc.y*u_sky[1].y*u_sky[1].z,-1.));
 vec3 d=normalize(mul(u_skyRotation,vec4(ray,0.)).xyz);
 float phase=u_sky[0].y,t=u_sky[0].w;
 float density=(.55+.45*clamp(u_sky[0].z,0.,1.))*u_sky[0].x;
 vec3 c=vec3(0.);
 if(u_regionLook[1].y>.0001)c+=nebula(d,phase,density)*u_regionLook[1].y;
 float dust=strataNoiseSample(d*6.4+phase*13.,vec3(0.));
 c+=(vec3(.0034,.010,.0055)+vec3(.010,.030,.014)*dust*density)*u_regionLook[0].x;
 if(u_regionLook[1].x>0.)
  c+=tidalSky(d,vec3(.07,.10,.16)*dust*density,phase,u_regionLook[0].y)*(.28+.72*u_sky[0].x)*u_regionLook[1].x;
 if(u_regionLook[1].z>0.)
  c+=acidSky(d,phase)*(.28+.72*u_sky[0].x)*u_regionLook[1].z;
 if(u_spaceGlass.x>0.)c+=spaceGlassGlow(d,phase,t)*u_spaceGlass.x;
 gl_FragColor=vec4(c,1.);
}

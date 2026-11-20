$input v_texcoord0
#include <bgfx_shader.sh>
uniform vec4 u_stellar[3];
#include "hyperion_star_types.sh"

void main(){
 float kind=u_starType[0].x,time=u_starType[1].w,phase=u_starType[1].x;
 bool neutron=kind>7.5&&kind<8.5;
 bool dwarf=kind>8.5&&kind<9.5;
 bool carbon=kind>9.5;
 float discSize=neutron?.153:dwarf?.236:.271;
 vec2 sky=v_texcoord0-vec2(.5,.53);
 sky.x*=u_stellar[0].x;
 vec2 p=sky/discSize;
 float r=length(p),aa=u_stellar[0].y/discSize;
 float disc=1.-smoothstep(1.-aa,1.+aa,r);
 vec3 n=vec3(p,sqrt(max(0.,1.-r*r)));
 vec3 colour=vec3(.001,.002,.004)+vec3(.002,.004,.009)*exp(-dot(sky,sky)*8.);
 vec2 radial=p/max(r,.001);
 float haze=strataNoise(vec3(radial*5.,phase*11.+r*2.),vec3(0.));
 float envelope=max(0.,r-1.);
 vec3 tint=carbon?vec3(.87,.16,.057):dwarf?vec3(.76,.31,.10):vec3(1.,.37,.10);
 if(neutron){
  vec2 axis=normalize(vec2(.43,-1.));
  float along=abs(dot(p,axis)),crosswise=abs(p.x*axis.y-p.y*axis.x);
  float jet=exp(-crosswise/(.034+along*.041))*exp(-along*.46);
  float halo=exp(-crosswise/(.19+along*.115))*exp(-along*.63);
  float threads=strataNoise(vec3(p*vec2(3.,8.),phase+time*.15),vec3(0.));
  colour+=vec3(.16,.38,1.1)*(jet*.66+halo*.18)*(.70+threads*.58);
  colour+=vec3(.25,.41,1.)*exp(-envelope*5.5)*.19;
 }else{
  float atmosphere=exp(-envelope/((carbon?.21:dwarf?.085:.12)*(.61+haze*.65)));
  float filament=strataNoise(vec3(radial*19.,r*7.+phase),vec3(0.));
  float wind=pow(max(0.,filament),3.)*exp(-envelope*(carbon?4.2:8.2));
  float rim=exp(-abs(r-1.007)/max(.014,aa*1.8))*haze;
  colour+=tint*(atmosphere*(dwarf?.032:.16)+wind*(carbon?.24:dwarf?.025:.12)+rim*(dwarf?.04:.25));
 }
 if(disc>0.){
  vec3 light=u_stellar[1].rgb;
  if(neutron)light=vec3(.49,.67,1.15);
  if(dwarf)light=vec3(.68,.29,.115); // Increase gain on the infrared-bright target.
  if(carbon)light=vec3(.90,.096,.034);
  if(!neutron&&!dwarf&&!carbon)light=mix(light,vec3(1.24,.24,.045),.84);
  vec3 surface=typedPhotosphere(n,light,n.z,time);
  if(carbon){
   float lanes=strataNoise(n*9.3+vec3(5.,phase*13.,2.),vec3(0.));
   surface*=mix(vec3(.35,.20,.19),vec3(1.32,1.03,.80),smoothstep(.28,.70,lanes));
  }
  if(dwarf){
   float circulation=sin(n.y*31.+strataNoise(n*5.+phase,vec3(0.))*4.);
   surface*=mix(vec3(.47,.38,.34),vec3(1.28,1.20,1.18),smoothstep(-.75,.85,circulation));
  }
  colour=mix(colour,surface,disc);
 }
 gl_FragColor=vec4(pow(colour/(colour+vec3(.49)),vec3(1./2.2)),1.);
}

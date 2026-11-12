$input v_color0, v_position, v_pattern, v_sphere, v_elevation
#include <bgfx_shader.sh>
uniform vec4 u_hyperion[4];
#include "hyperion_noise.sh"
void main(){
 vec2 q=v_sphere.xy*1.32;
 float r=length(q);
 bool corona=v_color0.a>1.5;
 if(r>(corona?1.32:1.018))discard;
 float t=u_hyperion[0].y;
 float a=atan2(q.y,q.x+1e-10);
 float twist=a+2.9*r-t*.44;
 vec2 swirl=r*vec2(cos(twist),sin(twist));
 vec3 flow=vec3(swirl*9.,t*.39);
 float broad=strataNoise(flow,vec3(0.));
 float sparks=strataNoise(flow*2.6+vec3(17.,1.,6.),vec3(0.));
 float edgeFlow=.79+.13*sin(a*3.+t*.53)+.07*sin(a*9.-t*.78);
 float edge=1.+(broad-.5)*.026;
 float delta=r-edge;
 float footprint=max(fwidth(r),.0025);
 vec3 plasma=v_color0.rgb;
 if(corona){
  float shoulder=exp(-abs(delta)/.056)*(.10+.62*broad*broad);
  float halo=exp(-abs(delta)/.14)*.13;
  float caustic=1.-smoothstep(.009,.023+footprint,abs(delta));
  float filament=exp(-abs(r-1.066-.015*sin(a*4.+t*.43))/.012);
  float broken=smoothstep(.40,.77,broad*.55+sparks*.45);
  vec3 core=mix(plasma,vec3(1.,.91,.67),.69);
  vec3 light=plasma*(shoulder+halo)*edgeFlow;
  light+=core*caustic*(.74+.95*broken)*edgeFlow;
  light+=plasma*filament*broken*.42;
  light*=1.-smoothstep(1.21,1.32,r);
  gl_FragColor=vec4(light,1.);
  return;
 }
 float inner=smoothstep(.28,.98,r);
 float stretch=sin(27.*r-1.6*a+4.2*broad-t*1.17);
 float crease=pow(.5+.5*stretch,8.)*(.12+.88*broad);
 float funnel=inner*inner*(.020+.10*broad+.105*crease);
 float pin=exp(-abs(r-.920+.009*sin(a*5.))/.012)*.068*edgeFlow;
 vec3 abyss=vec3(.0013,.0018,.0035)*(1.+broad*.9);
 gl_FragColor=vec4(abyss+plasma*(funnel+pin),1.);
}

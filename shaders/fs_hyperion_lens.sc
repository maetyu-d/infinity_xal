$input v_texcoord0
#include <bgfx_shader.sh>
SAMPLER2D(s_scene,0);
SAMPLER2D(s_bloom,1);
SAMPLER2D(s_opaqueDepth,2);
SAMPLER2D(s_focusSeed,3);
SAMPLER2D(s_cloudVolume,9);
uniform vec4 u_post[2];
#include "hyperion_camera_lens.sh"
#include "hyperion_display.sh"
#include "hyperion_stellar_optics.sh"
vec3 cameraBlur(vec2 uv,vec3 c,float focus){
 float z=cameraDepth(uv),coc=cameraDefocus(z,focus);
 if(coc<.025)return c;
 vec3 sum=c;float weight=1.;
 for(int tap=0;tap<8;tap++){
  float angle=float(tap)*.7853982;
  vec2 q=uv+vec2(cos(angle),sin(angle))*u_lens[1].xy*u_lens[0].z*coc;
  float donor=cameraDepth(q);
  // Do not smear a foreground silhouette across the far glass sky.
  float accept=1.-smoothstep(.025,.15,abs(donor-z)/max(max(donor,z),.00001));
  sum+=texture2D(s_scene,q).rgb*accept;weight+=accept;
 }
 return mix(c,sum/weight,coc*.80);
}
float cameraIris(vec2 q,float radius){
 float angle=atan2(q.y,q.x);
 float edge=length(q)/apertureEdge(angle);
 float aa=u_lens[1].y*2.;
 float fill=1.-smoothstep(radius-aa,radius+aa,edge);
 float rim=exp(-abs(edge-radius)/max(aa*2.,.00001));
 return fill*.22+rim*.48;
}
vec3 cameraFlare(vec2 uv){
 if(u_stellar[0].w<=0.)return vec3(0.);
 float visibility=stellarVisibility(true);
 if(visibility<.001)return vec3(0.);
 vec2 axis=vec2(.5)-u_stellar[0].xy;
 vec3 ghosts=vec3(0.);
 for(int ghost=0;ghost<5;ghost++){
  float g=float(ghost);
  vec2 centre=u_stellar[0].xy+axis*(1.20+g*.42);
  vec2 q=(uv-centre)*vec2(u_stellar[2].x,1.);
  float radius=.018+g*.010+u_stellar[0].z*.32;
  ghosts+=mix(vec3(1.,.24,.025),vec3(1.,.78,.20),g*.18)*cameraIris(q,radius)*(.035-g*.004);
 }
 return ghosts*visibility*u_stellar[2].y*u_lens[0].w*(1.-u_regionLook[0].x*.78-u_regionLook[1].x*.85-u_regionLook[1].z*.97);
}
void main(){
 vec2 uv=v_texcoord0;
 if(u_lens[0].x>.5){
  vec2 q=uv-.5;
  uv=clamp(.5+q*(1.+dot(q,q)*.018),vec2(.001),vec2(.999));
 }
 vec4 optical=texture2D(s_scene,uv);
 vec3 c=optical.rgb;
 vec3 bokeh=vec3(0.);
 if(u_lens[0].x>.5){
  float focus=texture2D(s_focusSeed,vec2(.5)).a;
  c=cameraBlur(uv,c,focus);
  vec2 edge=(uv-.5)*2.;
  float rim=clamp(dot(edge,edge)*.45,0.,1.);
  // Sub-pixel red/gold edge dispersion stays inside the firelight palette.
  vec2 dispersion=edge*u_lens[1].xy*rim*.65*(1.-u_regionLook[0].x*.80-u_regionLook[1].x*.75-u_regionLook[1].z*.88);
  vec3 dispersed=vec3(texture2D(s_scene,uv+dispersion).r,c.g,texture2D(s_scene,uv-dispersion*.65).b);
  c=mix(c,dispersed,rim*.22);
  bokeh=texture2D(s_bloom,uv).rgb;
  c+=bokeh*.34;
 }
 vec3 display=displayTransform(c,uv,clamp(optical.a,0.,1.));
 if(u_lens[0].x>.5){
  // Low-opacity red halation survives the deliberately deep shadow curve.
  float glow=max(bokeh.r,max(bokeh.g,bokeh.b));
  vec3 halo=mix(vec3(1.,.16,.012),vec3(1.,.66,.11),smoothstep(.3,2.,glow));
  vec3 mistHalo=mix(vec3(.12,.54,.29),vec3(.78,.96,.37),smoothstep(.3,2.,glow));
  vec3 waterHalo=mix(vec3(.11,.25,.45),vec3(.52,.75,.92),smoothstep(.3,2.,glow));
  vec3 acidHalo=mix(vec3(.78,.13,.45),vec3(1.,.96,.40),smoothstep(.3,2.,glow))*.59;
  halo=halo*u_regionLook[1].y+mistHalo*u_regionLook[0].x+waterHalo*u_regionLook[1].x+acidHalo*u_regionLook[1].z;
  display+=halo*pow(clamp(glow*.30,0.,1.),.9)*.105;
  display+=cameraFlare(uv);
 }
 gl_FragColor=vec4(clamp(display,vec3(0.),vec3(1.)),1.);
}

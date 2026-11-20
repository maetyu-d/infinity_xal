$input v_texcoord0
#include <bgfx_shader.sh>
SAMPLER2D(s_scene,0);
SAMPLER2D(s_bloom,1);
SAMPLER2D(s_opaqueDepth,2);
SAMPLER2D(s_focusSeed,3);
SAMPLER2D(s_ringLayer,4);
SAMPLER2D(s_shipTrail,6);
SAMPLER2D(s_shipBlast,10);
SAMPLER2D(s_cloudVolume,9);
uniform vec4 u_post[2];
uniform vec4 u_shipBlastParams;
#include "hyperion_camera_lens.sh"
#include "hyperion_vintage_lens.sh"
#include "hyperion_display.sh"
#include "hyperion_stellar_optics.sh"
vec3 cameraBlur(vec2 uv,vec3 c,float focus,float surface){
 float z=cameraDepth(uv),coc=cameraDefocus(z,focus);
 // Reveal structural details through the windows without shifting the focus of the sky.
 float logMetres=u_regionLook[0].z+log2(max(z,u_lens[1].z))*.30103;
 float closeStructure=surface*(1.-smoothstep(2.92,3.3,logMetres));
 coc*=1.-u_post[1].w*mix(.76,.97,closeStructure);
 float aspect=u_lens[1].y/u_lens[1].x;
 float field=vintageField(uv,aspect);
 float fieldBlur=field*field*(1.-u_post[1].w*.34);
 float radius=1.35*coc+6.8*fieldBlur;
 if(radius<.18)return c;
 vec2 radial=vintageRadial(uv,aspect);
 vec3 sum=c;float weight=1.;
 for(int tap=0;tap<12;tap++){
  float angle=(float(tap)+.5)*2.3999632;
  float reach=sqrt((float(tap)+.5)/12.);
  vec2 q=uv+vintagePupil(radial,field,angle,reach)*u_lens[1].xy*radius;
  float donor=cameraDepth(q);
  // Do not smear a foreground silhouette across the far glass sky.
  float accept=1.-smoothstep(.025,.15,abs(donor-z)/max(max(donor,z),u_lens[1].z));
  sum+=texture2D(s_scene,q).rgb*accept;weight+=accept;
 }
 return mix(c,sum/weight,clamp(coc*.30+field*.84,0.,.92));
}
float cameraIris(vec2 q,float radius){
 float angle=atan2(q.y,q.x);
 float edge=length(q)/apertureEdge(angle);
 float aa=u_lens[1].y*2.+radius*.24;
 float fill=1.-smoothstep(radius-aa,radius+aa,edge);
 float rim=exp(-abs(edge-radius)/max(aa*2.,.00001));
 return fill*.25+rim*.10;
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
 return ghosts*visibility*u_stellar[2].y*u_lens[0].w*.56;
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
  c=cameraBlur(uv,c,focus,optical.a);
  vec2 edge=(uv-.5)*2.;
  float rim=clamp(dot(edge,edge)*.45,0.,1.);
  // Sub-pixel red/gold edge dispersion stays inside the firelight palette.
  vec2 dispersion=edge*u_lens[1].xy*rim*.65*(1.-u_regionLook[0].x*.80-u_regionLook[1].x*.75-u_regionLook[1].z*.88);
  vec3 dispersed=vec3(texture2D(s_scene,uv+dispersion).r,c.g,texture2D(s_scene,uv-dispersion*.65).b);
  c=mix(c,dispersed,rim*.22);
  bokeh=texture2D(s_bloom,uv).rgb;
  c+=bokeh*(.025+.52*vintageField(uv,u_lens[1].y/u_lens[1].x));
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
  display+=halo*pow(clamp(glow*.30,0.,1.),.9)*.10;
  display+=vintageGlare(bokeh)*.115;
  display+=cameraFlare(uv);
 }
 // Fuel emission and released gas retain their own spectra after the regional
 // world grade. The layer shares opaque world depth and stays behind the cabin.
 if(u_shipBlastParams.x>.5){
  vec4 blast=texture2D(s_shipBlast,uv);
  float coverage=clamp(blast.a,0.,1.);
  float transmission=1.;
  if(u_post[1].z>.5)transmission*=1.-texture2D(s_ringLayer,uv).a;
  if(u_stellar[2].w>.5)transmission*=texture2D(s_cloudVolume,uv).a;
  vec3 fuel=toneColour(blast.rgb/max(.0001,coverage),1.);
  display=mix(display,fuel,coverage*transmission);
  vec2 haloPixel=u_lens[1].xy*4.;
  vec3 halo=(texture2D(s_shipBlast,uv+vec2(haloPixel.x,0.)).rgb
            +texture2D(s_shipBlast,uv-vec2(haloPixel.x,0.)).rgb
            +texture2D(s_shipBlast,uv+vec2(0.,haloPixel.y)).rgb
            +texture2D(s_shipBlast,uv-vec2(0.,haloPixel.y)).rgb)*.25;
  display+=toneColour(halo,1.)*.075*transmission*(1.-coverage*.7);
 }
 if(u_regionLook[1].w>.5){
  float wake=texture2D(s_shipTrail,uv).r;
  if(wake>0.){
   vec3 tint=vec3(.18,.91,1.)*u_regionLook[1].y     // Charred Glass
            +vec3(1.,.24,.76)*u_regionLook[0].x     // Verdant Fog
            +vec3(1.,.59,.18)*u_regionLook[1].x     // Tidal Blue
            +vec3(.29,1.,.57)*u_regionLook[1].z;    // Acid Etch
   vec3 filament=mix(tint,vec3(1.),.32*smoothstep(.25,1.,wake));
   // Keep the hue readable through the brightest parts of the sky.
   filament=mix(filament,tint*.46,smoothstep(.44,.76,luma(display)));
   float transmission=1.;
   if(u_post[1].z>.5)transmission*=1.-texture2D(s_ringLayer,uv).a;
   if(u_stellar[2].w>.5)transmission*=texture2D(s_cloudVolume,uv).a;
   display=mix(display,filament,wake*.94*transmission);
  }
 }
 if(u_lens[0].x>.5)
  display=vintageGrade(display,vintageField(uv,u_lens[1].y/u_lens[1].x));
 gl_FragColor=vec4(clamp(display,vec3(0.),vec3(1.)),1.);
}

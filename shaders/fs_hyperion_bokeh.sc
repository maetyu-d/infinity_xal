$input v_texcoord0
#include <bgfx_shader.sh>
SAMPLER2D(s_scene,0);
SAMPLER2D(s_opaqueDepth,2);
uniform vec4 u_post[2];
#include "hyperion_camera_lens.sh"
#include "hyperion_vintage_lens.sh"
void main(){
 if(u_lens[0].x<.5){gl_FragColor=vec4(0.,0.,0.,1.);return;}
 vec2 uv=v_texcoord0;
 float focus=texture2D(s_scene,vec2(.5)).a;
 float coc=cameraDefocus(cameraDepth(uv),focus)*(1.-u_post[1].w*.88);
 float aspect=u_lens[1].y/u_lens[1].x;
 float field=vintageField(uv,aspect);
 float radius=(1.0+15.*coc+32.*field*field)*u_lens[0].y;
 float sourceLod=clamp(log2(max(1.,radius/6.)),0.,2.2)*u_post[0].z;
 vec2 radial=vintageRadial(uv,aspect);
 vec3 sum=texture2DLod(s_scene,uv,sourceLod).rgb;float total=1.;
 for(int tap=0;tap<48;tap++){
  float angle=(float(tap)+.5)*2.3999632;
  float r=sqrt((float(tap)+.5)/48.)*apertureEdge(angle);
  vec2 aperture=vintagePupil(radial,field,angle,r);
  sum+=texture2DLod(s_scene,uv+aperture*radius*u_lens[1].xy,sourceLod).rgb;
  total+=1.;
 }
 // Broad low-energy scatter forms a veil around bright sources. Its fixed
 // kernel preserves energy while a light crosses the reduced-resolution seed.
 vec3 veil=vec3(0.);
 for(int tap=0;tap<12;tap++){
  float angle=(float(tap)+.5)*2.3999632;
  float r=sqrt((float(tap)+.5)/12.);
  vec2 aperture=vintagePupil(radial,field*.45,angle,r);
  veil+=texture2DLod(s_scene,uv+aperture*(radius*2.4+17.)*u_lens[1].xy,
                     min(4.,sourceLod+1.25)*u_post[0].z).rgb/12.;
 }
 gl_FragColor=vec4(sum/total*1.20+veil*.27,1.);
}

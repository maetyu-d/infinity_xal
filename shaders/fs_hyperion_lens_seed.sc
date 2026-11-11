$input v_texcoord0
#include <bgfx_shader.sh>
SAMPLER2D(s_scene,0);
SAMPLER2D(s_opaqueDepth,2);
#include "hyperion_camera_lens.sh"
void main(){
 if(u_lens[0].x<.5){gl_FragColor=vec4(0.,0.,0.,u_lens[1].w);return;}
 vec3 light=vec3(0.);float hottest=0.;
 // Filter the source footprint before the aperture gather; small stars
 // retain energy instead of blinking between low-resolution texels.
 for(int y=-1;y<=1;y++)for(int x=-1;x<=1;x++){
  vec3 c=texture2D(s_scene,v_texcoord0+vec2(float(x),float(y))*u_lens[1].xy*1.15).rgb;
  float peak=max(c.r,max(c.g,c.b)),soft=clamp(peak-.30,0.,.6);
  float amount=max(peak-.85,soft*soft*.5)/max(peak,.0001);
  light+=c*amount/9.;
  hottest=max(hottest,peak*amount);
 }
 // Restore tiny point highlights lost to the third-resolution footprint.
 // Broad emitters keep their exposure; isolated stars/glints open into iris discs.
 float average=max(light.r,max(light.g,light.b));
 float isolated=smoothstep(2.,7.,hottest/max(average,.00001));
 light*=1.+isolated*4.;
 gl_FragColor=vec4(light,cameraFocus());
}

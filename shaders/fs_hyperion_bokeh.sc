$input v_texcoord0
#include <bgfx_shader.sh>
SAMPLER2D(s_scene,0);
SAMPLER2D(s_opaqueDepth,2);
#include "hyperion_camera_lens.sh"
void main(){
 if(u_lens[0].x<.5){gl_FragColor=vec4(0.,0.,0.,1.);return;}
 vec2 uv=v_texcoord0;
 float focus=texture2D(s_scene,vec2(.5)).a;
 float coc=cameraDefocus(cameraDepth(uv),focus);
 float offaxis=clamp(length((uv-.5)*2.),0.,1.);
 float radius=(6.+20.*max(coc,offaxis*offaxis*.28))*u_lens[0].y;
 vec2 radial=normalize(uv-.5+vec2(.00001)),side=vec2(-radial.y,radial.x);
 float catEye=1.-offaxis*.32;
 vec3 sum=texture2D(s_scene,uv).rgb;float total=1.;
 for(int tap=0;tap<48;tap++){
  float angle=(float(tap)+.5)*2.3999632;
  float r=sqrt((float(tap)+.5)/48.)*apertureEdge(angle);
  vec2 aperture=(radial*cos(angle)*catEye+side*sin(angle))*r;
  sum+=texture2D(s_scene,uv+aperture*radius*u_lens[1].xy).rgb;
  total+=1.;
 }
 gl_FragColor=vec4(sum/total*1.25,1.);
}

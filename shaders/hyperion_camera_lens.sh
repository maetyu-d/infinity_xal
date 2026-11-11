uniform vec4 u_lens[2]; // enabled, aperture, blur px, flare; texel.xy, near, far
float cameraDepth(vec2 uv){
 float d=texture2D(s_opaqueDepth,uv).r;
 return u_lens[1].z*u_lens[1].w/(u_lens[1].z+d*(u_lens[1].w-u_lens[1].z));
}
float cameraFocus(){
 float focus=u_lens[1].w;
 for(int tap=0;tap<5;tap++){
  vec2 uv=vec2(.5);
  if(tap==1)uv.x+=.035;if(tap==2)uv.x-=.035;
  if(tap==3)uv.y+=.035;if(tap==4)uv.y-=.035;
  float d=texture2D(s_opaqueDepth,uv).r;
  if(d>0.)focus=min(focus,cameraDepth(uv));
 }
 return max(focus,u_lens[1].z*2.);
}
float cameraDefocus(float z,float focus){
 return smoothstep(.035,.65,abs(z-focus)/max(max(z,focus),.00001));
}
float apertureEdge(float angle){
 return .9238795/cos(mod(angle+.3926991,.7853982)-.3926991);
}

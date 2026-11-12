uniform vec4 u_wormholeOptics[5]; // camera-space centre/radius, right/frustum X, up/frustum Y, plasma/time, normal

vec3 wormholeOptics(vec2 uv,vec3 scene){
 float radius=u_wormholeOptics[0].w;
 if(radius<=0.)return scene;
 vec3 centre=u_wormholeOptics[0].xyz,right=u_wormholeOptics[1].xyz,up=u_wormholeOptics[2].xyz;
 vec3 normal=u_wormholeOptics[4].xyz;
 vec2 frustum=vec2(u_wormholeOptics[1].w,u_wormholeOptics[2].w);
 vec3 ray=vec3((uv*2.-1.)*frustum,-1.);
 float angle=dot(ray,normal);
 if(abs(angle)<1e-5)return scene;
 float depth=dot(centre,normal)/angle;
 if(depth<u_lens[1].z)return scene;
 vec3 offset=ray*depth-centre;
 vec2 q=vec2(dot(offset,right),dot(offset,up))/radius;
 float r=length(q);
 if(r<.943 || r>1.64)return scene;
 float pixel=max(fwidth(r),.002);
 if(reflectionDepth(uv)<depth*(1.-min(.008,pixel*.4)))return scene;

 float t=u_wormholeOptics[3].w;
 vec2 direction=q/r;
 float a=atan2(q.y,q.x);
 float stream=a-t*.26;
 vec2 flow=vec2(cos(stream),sin(stream));
 float turbulence=strataNoise(vec3(flow*7.4,r*8.+t*.44),vec3(0.));
 float fine=strataNoise(vec3(flow*27.,r*19.-t*.51),vec3(9.,1.,5.));
 float rim=1.+.014*(turbulence-.5);
 float d=r-rim;
 vec3 plasma=u_wormholeOptics[3].rgb;

 // Shift the background away from the throat in its own plane. Depth is
 // checked at both rays so a foreground ship or station cannot be pulled in.
 if(r>1.025){
  float band=smoothstep(1.025,1.10,r)*(1.-smoothstep(1.35,1.64,r));
  float deflection=band*(.39+.12*turbulence+.07*sin(a*4.+t*.37));
  vec3 radial=right*direction.x+up*direction.y;
  vec3 tangent=up*direction.x-right*direction.y;
  vec3 target=ray*depth+(radial+tangent*(fine-.5)*.32)*(radius*deflection);
  vec2 sampleUV=.5+.5*target.xy/(frustum*max(-target.z,1e-8));
  float border=smoothstep(.003,.036,min(min(sampleUV.x,sampleUV.y),min(1.-sampleUV.x,1.-sampleUV.y)));
  if(border>.001 && reflectionDepth(sampleUV)>=-target.z*.998){
   vec3 bent=resolveStellarDisc(sampleUV,texture2D(s_scene,sampleUV).rgb);
   scene=mix(scene,bent,band*border*.90);
  }
 }

 float strandA=abs(d-.046-.022*sin(a*5.-t*.81)-.010*sin(a*12.+t*.37));
 float strandB=abs(d-.101-.028*sin(a*6.+t*.54+.9));
 float coreDistance=d/max(.010,pixel*.82);
 float core=exp(-coreDistance*coreDistance);
 float threads=exp(-strandA/max(.008,pixel*.58))*smoothstep(.22,.69,turbulence*.6+fine*.4);
 threads+=exp(-strandB/max(.006,pixel*.50))*smoothstep(.34,.75,fine)*.78;
 float haloLine=exp(-abs(d-.174-.012*sin(a*4.+t*.28))/max(.004,pixel*.43));
 threads+=haloLine*smoothstep(.46,.77,turbulence)*.51;
 float shoulder=exp(-abs(d)/.052)*(.23+.75*turbulence*turbulence);
 float veil=exp(-abs(d)/.15)*smoothstep(.17,.71,turbulence)*.18;
 float flare=smoothstep(.58,.83,turbulence)*(.60+.40*fine);
 vec3 whiteCore=mix(plasma,vec3(1.,.96,.78),.73);
 float asymmetric=.76+.24*sin(a-.52);
 vec3 emission=plasma*(shoulder+veil+threads*1.55)+whiteCore*core*(1.5+1.9*flare)*asymmetric;
 return scene+emission*(1.-smoothstep(1.45,1.64,r))*smoothstep(.943,.976,r);
}

#ifndef HYPERION_BUILT_FINISH
#define HYPERION_BUILT_FINISH
// Coated and machined manufactured surfaces. The tangent and roughness are
// fixed to the object; no view noise, temporal accumulation or normal sparkle.
vec3 builtTangent(vec3 N,vec3 grain){
 vec3 fallback=abs(N.y)<.8?cross(N,vec3(0.,1.,0.)):cross(N,vec3(0.,0.,1.));
 return surfaceUnit(grain-N*dot(N,grain),surfaceUnit(fallback,vec3(1.,0.,0.)));
}
vec3 builtBrdf(vec3 body,vec3 N,vec3 V,vec3 L,float rough,float metal,vec3 tangent,float anisotropy){
 float nv=max(dot(N,V),.015),nl=max(dot(N,L),0.);
 vec3 H=surfaceUnit(V+L,N),T=builtTangent(N,tangent),B=cross(N,T);
 float nh=max(dot(N,H),0.),vh=max(dot(V,H),0.);
 float alpha=max(.035,rough*rough);
 float aspect=sqrt(1.-clamp(anisotropy,0.,.8)*.85);
 float ax=alpha/aspect,ay=alpha*aspect;
 vec3 h=vec3(dot(T,H)/ax,dot(B,H)/ay,nh);
 float denominator=dot(h,h);
 float distribution=1./max(3.141593*ax*ay*denominator*denominator,.00001);
 float lv=length(vec3(dot(T,V)*ax,dot(B,V)*ay,nv));
 float ll=length(vec3(dot(T,L)*ax,dot(B,L)*ay,nl));
 float visibility=.5/max(nl*lv+nv*ll,.0001);
 vec3 f0=mix(vec3(.04),body,metal);
 vec3 fresnel=f0+(1.-f0)*pow(1.-vh,5.);
 // A bounded multiple-scattering correction keeps brushed alloy bright
 // without whitening paint. The original high-contrast display curve stays.
 vec3 compensation=vec3(1.)+f0*(rough*.38);
 vec3 specular=distribution*visibility*fresnel*compensation;
 vec3 diffuse=(1.-fresnel)*(1.-metal)*body/3.141593;
 return (diffuse+specular)*nl*3.8;
}

vec3 builtCoat(vec3 N,vec3 V,vec3 L,float rough){
 vec3 H=surfaceUnit(V+L,N);
 float nv=max(dot(N,V),.02),nl=max(dot(N,L),0.),nh=max(dot(N,H),0.);
 float alpha=max(.05,rough*rough),a2=alpha*alpha;
 float d=nh*nh*(a2-1.)+1.;
 float distribution=a2/max(3.141593*d*d,.00001);
 float fresnel=.04+.96*pow(1.-max(dot(V,H),0.),5.);
 return vec3(distribution*fresnel*nl/max(4.*(nv+nl-nv*nl),.08));
}
#endif

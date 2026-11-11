#include "hyperion_space_glass.sh"
// Hyperion worldFragment/structureLight: hemispherical sky irradiance and
// broad reflected sky/film. Analytic, roughness-filtered and world-oriented;
// no screen-colour feedback or additional texture allocation.
vec3 reflectedEnvironment(vec3 R,vec3 up,float day,float phase,float rough,float air){
 float h=dot(R,up),horizon=exp(-abs(h-.015)/mix(.025,.22,rough));
 vec3 zenith=mix(vec3(.008,.003,.002),vec3(.045,.016,.009),day);
 vec3 sky=mix(zenith,vec3(3.2,.85,.12),horizon*(.40+.60*day));
 // Broad firelit cloud cards form readable reflections on smooth glass.
 float cloud=.5+.5*sin(R.x*5.+R.z*3.+phase*6.);
 sky+=vec3(.38,.055,.009)*pow(cloud,4.)*exp(-abs(h-.17)*5.)*(1.-rough*.70);
#ifdef INFINITY_MATERIAL_REFLECTIONS
 vec3 sun=normalize(u_hyperion[3].xyz+vec3(1e-15));
#else
 vec3 sun=normalize(vec3(.6,.7,-.4));
#endif
 float alignment=max(0.,dot(R,sun));
 float glint=pow(alignment,mix(1800.,14.,rough*rough));
 sky+=vec3(7.,2.8,.45)*glint*(.25+.75*day);
 sky+=spaceGlassGlow(R,phase,0.)*pow(1.-rough,3.)*.18;
 vec3 ground=vec3(.018,.006,.003)+vec3(.18,.025,.008)*horizon;
 return mix(ground,sky,smoothstep(-.12,.06,h));
}
// Rough dielectric Fresnel: unresolved rough facets do not turn an entire
// grazing landscape into a white mirror. Wet coatings retain their glints.
vec3 environmentReflectance(vec3 f0,float fresnel,float rough){
 return f0+(max(vec3(1.-rough),f0)-f0)*fresnel;
}
vec3 diffuseEnvironment(vec3 N,vec3 up,vec3 albedo,float day,float air){
 float skyFacing=clamp(dot(N,up)*.5+.5,0.,1.);
 vec3 sky=mix(vec3(.018,.028,.055),vec3(.070,.12,.21),air)*(.15+.85*day);
 vec3 bounce=mix(vec3(.025,.017,.012),albedo*.09,air)*(.15+.85*day);
 return albedo*mix(bounce,sky,skyFacing);
}
vec3 materialBump(vec3 N,vec3 position,float height,float maxSlope){
 vec3 dx=dFdx(position),dy=dFdy(position),a=cross(dy,N),b=cross(N,dx);
 float det=dot(dx,a);
 vec3 gradient=sign(det)*(dFdx(height)*a+dFdy(height)*b)/max(abs(det),1e-20);
 gradient*=min(1.,maxSlope/max(length(gradient),.001));
 return normalize(N-gradient);
}

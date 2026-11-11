#include "hyperion_pigment.sh"
#include "hyperion_weather.sh"
#include "hyperion_region_look.sh"
SAMPLER2D(s_scene,0);
SAMPLER2D(s_bloom,1);
SAMPLER2D(s_opaqueDepth,2);
uniform vec4 u_lens[2];
uniform vec4 u_post[2]; // texel.xy, strength, surface buffer available; reserved
#include "hyperion_optical_ray.sh"
// Retained atmospheric density, Rayleigh/Mie phases and stellar optical depth.
vec3 atmosphere(vec2 uv, vec2 cloudClip, out vec3 transmission) {
    transmission=vec3(1.);
    vec3 o=u_atmosphere[0].xyz,L=normalize(u_atmosphere[1].xyz);
    float H=u_atmosphere[1].w,outer=1.+H*4.444;
    vec2 ndc=uv*2.-1.;
    vec3 cameraRay=normalize(vec3(ndc.x*u_atmosphere[3].x,ndc.y*u_atmosphere[3].y*u_atmosphere[3].z,-1.));
    vec3 d=normalize(mul(u_atmosphereRotation,vec4(cameraRay,0.)).xyz);
    vec2 span=sphereSpan(o,d,outer);
    float start=max(0.,span.x),end=span.y;
    vec2 ground=sphereSpan(o,d,1.);if(ground.x>0.)end=min(end,ground.x);
    float depth=texture2D(s_opaqueDepth,uv).r;
    if(depth>0.){float near=u_atmosphere[2].y,far=u_atmosphere[2].z;float z=near*far/(near+depth*(far-near));end=min(end,z/max(-cameraRay.z,.0001)/u_atmosphere[2].x);}
    end=mix(end,min(end,cloudClip.x),smoothstep(.10,.85,cloudClip.y));
    if(end<=start || end-start<H*.00001)return vec3(0.);
    float stepSize=(end-start)/12.,optical=0.,aerosolDepth=0.;
    vec3 molecularSum=vec3(0.),aerosolSum=vec3(0.);
    float highLayer=0.;
    vec3 beta=vec3(.0058,.0135,.029)*u_atmosphere[0].w;
    vec3 hazeExtinction=vec3(.0027,.0024,.0021)*u_atmosphere[0].w;
    float mu=dot(d,L),rayleigh=.05968*(1.+mu*mu),mie=.012*pow(max(.02,1.-.78*mu),-1.5);
    for(int i=0;i<12;i++){
        vec3 p=o+d*(start+(float(i)+.5)*stepSize);
        float height=max(0.,length(p)-1.)/H,kmStep=stepSize*u_atmosphere[3].w;
        float density=exp(-height),aerosol=exp(-height*2.25);
        optical+=density*kmStep;
        aerosolDepth+=aerosol*kmStep;
        vec2 sun=sphereSpan(p,L,outer),hit=sphereSpan(p,L,1.);
        float sunlight=hit.x>0.?0.:1.,sunOptical=0.;
        for(int j=0;j<4;j++){vec3 sp=p+L*sun.y*(float(j)+.5)/4.;sunOptical+=exp(-max(0.,length(sp)-1.)/H)*sun.y*u_atmosphere[3].w*.25;}
        vec3 through=exp(-beta*(optical+sunOptical)-hazeExtinction*(aerosolDepth+sunOptical*.42));
        molecularSum+=through*density*kmStep*sunlight;
        aerosolSum+=through*aerosol*kmStep*sunlight;
        float upper=(height-2.65)*1.46;
        highLayer+=exp(-upper*upper)*through.g*kmStep*sunlight;
    }
    vec3 scattering=(molecularSum*beta*rayleigh+aerosolSum*hazeExtinction*mie)*7.;
    scattering+=vec3(.00011,.00032,.00048)*u_atmosphere[0].w*highLayer;
    // Soot-red aerial scattering; the real light direction still defines dusk.
    float horizon=exp(-abs(dot(d,normalize(o)))*7.);
    float dusk=1.-smoothstep(.03,.32,abs(dot(normalize(o),L)));
    scattering*=mix(vec3(.70,.30,.11),vec3(1.85,.62,.065),horizon*.45+dusk*.45);
    scattering=mix(scattering,scattering.g*mix(vec3(.38,1.25,.70),vec3(.90,1.48,.30),horizon),u_regionLook[0].x);
    scattering=mix(scattering,scattering.g*vec3(.62,.96,1.25),u_regionLook[1].x);
    vec3 acidAir=mix(vec3(1.28,.37,.77),vec3(1.35,1.29,.30),horizon*.62+dusk*.38);
    scattering=mix(scattering,scattering.g*acidAir,u_regionLook[1].z);
    transmission=exp(-beta*optical-hazeExtinction*aerosolDepth);
    return scattering;
}
#include "hyperion_reflections.sh"
#include "hyperion_space_refraction.sh"
#include "hyperion_display.sh"
#define INFINITY_POLAR_RESOLVE 1
#include "hyperion_polar.sh"
#include "hyperion_cloud_resolve.sh"
#include "hyperion_stellar_optics.sh"
#include "hyperion_region_fog.sh"
#if INFINITY_WEATHER_VISIBILITY
#include "hyperion_weather_visibility.sh"
#endif
void main() {
    vec2 uv=v_texcoord0;
    vec4 surface=u_post[0].w>.5?texture2D(s_reflectionSurface,uv):vec4(0.);
    float built=surface.w<0.&&texture2D(s_opaqueDepth,uv).r>0.?1.:0.;
    vec3 c=refractSpace(uv,sceneReflection(uv,resolveScene(uv),surface));
    c=resolveStellarDisc(uv,c);
    c+=texture2D(s_bloom,uv).rgb*(u_lens[0].x>.5?.30:.50);
    if(u_atmosphere[0].w>0.){
        vec2 cloudClip;vec4 clouds=resolveClouds(uv,cloudClip);c=(c+resolvePolar(uv))*clouds.a+clouds.rgb;
        vec3 transmission;vec3 air=atmosphere(uv,cloudClip,transmission);c=c*transmission+air;
        built*=clouds.a*transmission.g;
    }
#if INFINITY_WEATHER_VISIBILITY
    c=weatherVisibility(c,uv,built);
#endif
    c=regionalFog(uv,c,built);
    gl_FragColor=vec4(c,built);
}

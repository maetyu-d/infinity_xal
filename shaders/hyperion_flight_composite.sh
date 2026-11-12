#include "hyperion_pigment.sh"
#include "hyperion_weather.sh"
#include "hyperion_region_look.sh"
SAMPLER2D(s_scene,0);
SAMPLER2D(s_bloom,1);
SAMPLER2D(s_opaqueDepth,2);
uniform vec4 u_lens[2];
uniform vec4 u_post[2]; // texel.xy, strength, surface buffer available; reserved
#include "hyperion_optical_ray.sh"
// Exponential atmosphere along a solar ray. The fit tends to H/cos(theta)
// overhead and to the half Gaussian column of a curved limb at grazing angles.
float solarColumn(float radius,float mu,float heightScale,float density) {
    float outward=abs(mu);
    float curvature=max(0.,1.-outward*outward)*2.54648*heightScale/radius;
    float tail=density*2.*heightScale/max(1e-5,outward+sqrt(outward*outward+curvature));
    if(mu>=0.)return tail;
    float tangent=max(1.,radius*sqrt(max(0.,1.-mu*mu)));
    float whole=sqrt(6.2831853*tangent*heightScale)*exp(-(tangent-1.)/heightScale);
    return max(tail,whole-tail);
}

vec3 atmosphere(vec2 uv, vec2 cloudClip, out vec3 transmission) {
    transmission=vec3(1.);
    vec3 o=u_atmosphere[0].xyz,L=normalize(u_atmosphere[1].xyz);
    float H=u_atmosphere[1].w,outer=1.+H*5.6;
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
    float stepSize=(end-start)/14.,optical=0.,aerosolDepth=0.;
    float kmStep=stepSize*u_atmosphere[3].w;
    vec3 direct=vec3(0.),diffuse=vec3(0.);
    float airglow=0.;
    vec3 beta=vec3(.0058,.0135,.029)*u_atmosphere[0].w;
    vec3 hazeExtinction=vec3(.0040,.0030,.0024)*u_atmosphere[0].w;
    float mu=dot(d,L),rayleigh=.05968*(1.+mu*mu),mie=.012*pow(max(.02,1.-.84*mu),-1.5);
    for(int i=0;i<14;i++){
        vec3 p=o+d*(start+(float(i)+.5)*stepSize);
        float radius=max(1.,length(p)),height=(radius-1.)/H;
        float density=exp(-height),aerosol=exp(-height*2.4);
        float molecularStep=density*kmStep,aerosolStep=aerosol*kmStep;
        vec3 viewTransmission=exp(-beta*(optical+.5*molecularStep)-hazeExtinction*(aerosolDepth+.5*aerosolStep));
        optical+=molecularStep;
        aerosolDepth+=aerosolStep;
        float zenith=dot(p,L)/radius;
        float localHorizon=-sqrt(max(0.,1.-1./(radius*radius)));
        float sunlight=smoothstep(-.006,.006,zenith-localHorizon);
        if(sunlight>.001){
            float sunMolecules=solarColumn(radius,zenith,H,density)*u_atmosphere[3].w;
            float sunAerosol=solarColumn(radius,zenith,H/2.4,aerosol)*u_atmosphere[3].w;
            vec3 sunTransmission=exp(-beta*sunMolecules-hazeExtinction*sunAerosol);
            direct+=viewTransmission*sunTransmission*sunlight*
                    (beta*(molecularStep*rayleigh)+hazeExtinction*(aerosolStep*mie));
        }
        float twilight=smoothstep(-.20,.13,zenith);
        diffuse+=viewTransmission*beta*molecularStep*(.017+.045*twilight)*twilight;
        float upper=(height-3.85)*1.70;
        airglow+=viewTransmission.g*exp(-upper*upper)*kmStep*(.45+.55*twilight);
    }
    vec3 scattering=direct*7.8+diffuse*3.4;
    // Soot-red aerial scattering; the real light direction still defines dusk.
    float horizon=exp(-abs(dot(d,normalize(o)))*7.);
    float dusk=1.-smoothstep(.03,.32,abs(dot(normalize(o),L)));
    scattering*=mix(vec3(.70,.30,.11),vec3(1.85,.62,.065),horizon*.45+dusk*.45);
    scattering=mix(scattering,scattering.g*mix(vec3(.38,1.25,.70),vec3(.90,1.48,.30),horizon),u_regionLook[0].x);
    scattering=mix(scattering,scattering.g*vec3(.62,.96,1.25),u_regionLook[1].x);
    vec3 acidAir=mix(vec3(1.28,.37,.77),vec3(1.35,1.29,.30),horizon*.62+dusk*.38);
    scattering=mix(scattering,scattering.g*acidAir,u_regionLook[1].z);
    vec3 upperTint=mix(vec3(.65,.15,.052),vec3(.21,.78,.28),u_regionLook[0].x);
    upperTint=mix(upperTint,vec3(.14,.54,.88),u_regionLook[1].x);
    upperTint=mix(upperTint,vec3(.74,.26,.47),u_regionLook[1].z);
    scattering+=upperTint*(1.-exp(-airglow*u_atmosphere[0].w*.012))*.23;
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
#include "hyperion_wormhole_optics.sh"
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
    c+=stellarProminences(uv);
    c=regionalFog(uv,c,built);
    c=wormholeOptics(uv,c);
    c+=texture2D(s_bloom,uv).rgb*(u_lens[0].x>.5?.30:.50);
    if(u_atmosphere[0].w>0.){
        vec2 cloudClip;vec4 clouds=resolveClouds(uv,cloudClip);c=(c+resolvePolar(uv))*clouds.a+clouds.rgb;
        vec3 transmission;vec3 air=atmosphere(uv,cloudClip,transmission);c=c*transmission+air;
        built*=clouds.a*transmission.g;
    }
#if INFINITY_WEATHER_VISIBILITY
    c=weatherVisibility(c,uv,built);
#endif
    gl_FragColor=vec4(c,built);
}

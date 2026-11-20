$input v_texcoord0
#include <bgfx_shader.sh>
SAMPLER2D(s_scene,0);
SAMPLER2D(s_bloom,1);
SAMPLER2D(s_opaqueDepth,2);
SAMPLER2D(s_reflectionSurface,3); // Existing opaque cabin depth, standard near-to-far.
SAMPLER2D(s_ringLayer,4); // Cabin-local view normal, reflectance and roughness.
uniform vec4 u_post[2];
uniform vec4 u_lens[2];
#include "hyperion_vintage_lens.sh"
#include "hyperion_region_look.sh"
#include "hyperion_surface_frame.sh"
#include "hyperion_noise.sh"
#include "hyperion_cabin.sh"
uniform mat4 u_cabinFromEye;

float cabinDepth(vec2 uv){
    float depth=texture2D(s_reflectionSurface,uv).r;
    return u_post[1].x*u_post[1].y/max(1e-8,u_post[1].y-depth*(u_post[1].y-u_post[1].x));
}

vec3 cabinPosition(vec2 uv,float depth){
    return vec3((uv*2.-1.)*u_post[0].zw,-1.)*depth;
}

float cabinContact(vec2 uv,vec3 colour){
    if(u_post[1].w<.001)return 1.;
    float z=cabinDepth(uv),farClip=u_post[1].y;
    if(z>=farClip*.98)return 1.;
    vec3 p=cabinPosition(uv,z);
    vec2 pixel=u_post[0].xy;
    vec2 xStep=vec2(pixel.x,0.),yStep=vec2(0.,pixel.y);
    vec3 left=p-cabinPosition(uv-xStep,cabinDepth(uv-xStep));
    vec3 right=cabinPosition(uv+xStep,cabinDepth(uv+xStep))-p;
    vec3 below=p-cabinPosition(uv-yStep,cabinDepth(uv-yStep));
    vec3 above=cabinPosition(uv+yStep,cabinDepth(uv+yStep))-p;
    // Choose the derivative on the same surface at silhouette edges.
    vec3 dx=abs(left.z)<abs(right.z)?left:right;
    vec3 dy=abs(below.z)<abs(above.z)?below:above;
    vec3 frame=cross(dx,dy);
    float scale=max(max(abs(frame.x),abs(frame.y)),abs(frame.z));
    if(scale<1e-20)return 1.;
    vec3 N=normalize(frame/scale);if(dot(N,-p)<0.)N=-N;
    float radius=min(u_post[1].z,z*.22);
    vec2 spread=radius/(z*abs(u_post[0].zw))*.5;
    float resolved=smoothstep(2.,5.,length(spread/pixel));
    float sum=0.,weight=0.;
    // Fixed paired directions have no animated noise or old-frame samples.
    for(int tap=0;tap<20;++tap){
        float angle=float(tap)*2.399963;
        float reach=.30+mod(float(tap),4.)*.19;
        vec2 donor=uv+vec2(cos(angle),sin(angle))*spread*reach;
        if(min(donor.x,donor.y)<.001||max(donor.x,donor.y)>.999)continue;
        donor=(floor(donor/pixel)+.5)*pixel;
        float sampleZ=cabinDepth(donor);
        if(sampleZ>=farClip*.98)continue;
        vec3 delta=cabinPosition(donor,sampleZ)-p;
        float distance=length(delta);
        float horizon=max(0.,dot(N,delta)/max(distance,.0001)-.16);
        float proximity=1.-smoothstep(radius*.28,radius,distance);
        sum+=horizon*proximity;weight+=1.;
    }
    float occlusion=clamp(sum/max(weight,1.)*2.7,0.,.56)*resolved;
    float lamps=1.-smoothstep(.64,1.35,max(colour.r,max(colour.g,colour.b)));
    return 1.-occlusion*u_post[1].w*lamps;
}

vec4 cabinSample(vec2 uv){
    vec4 layer=texture2D(s_opaqueDepth,uv);
    float peak=max(layer.r,max(layer.g,layer.b));
    // Lamp cores retain their colour before the halo is composited.
    layer.rgb/=1.+max(0.,peak-layer.a)*.92;
    return layer;
}

vec2 cabinProject(vec3 p){
    return .5+.5*(p.xy/max(-p.z,.001))/u_post[0].zw;
}

vec3 cabinSceneReflection(vec2 uv,vec3 body,float coverage){
    if(u_lens[0].w<.5 || coverage<.96)return body;
    vec4 finish=texture2D(s_ringLayer,uv);
    float reflectance=length(finish.xyz),rough=finish.w;
    if(reflectance<.06 || rough>.64)return body;
    float depth=cabinDepth(uv);
    if(depth>=u_post[1].y*.98)return body;
    vec3 p=cabinPosition(uv,depth),N=normalize(finish.xyz),V=normalize(-p);
    if(dot(N,V)<0.)N=-N;
    vec3 ray=reflect(-V,N),origin=p+N*.012;
    vec3 reflected=body;
    float confidence=0.,previous=.015;
    for(int tap=0;tap<30;++tap){
        float i=float(tap),t=.025+i*.025+i*i*.0048;
        vec3 q=origin+ray*t;
        if(q.z>=-.03)break;
        vec2 donor=cabinProject(q);
        if(min(donor.x,donor.y)<.008 || max(donor.x,donor.y)>.992)break;
        float sampleZ=cabinDepth(donor),gap=-q.z-sampleZ;
        float thickness=.018+t*.028;
        if(tap>1 && sampleZ<u_post[1].y*.98 && gap>0. && gap<thickness){
            float low=previous,high=t;
            for(int refine=0;refine<5;++refine){
                float mid=(low+high)*.5;
                vec3 test=origin+ray*mid;
                float delta=-test.z-cabinDepth(cabinProject(test));
                if(delta>0.)high=mid;else low=mid;
            }
            donor=cabinProject(origin+ray*high);
            float border=min(min(donor.x,donor.y),min(1.-donor.x,1.-donor.y));
            float separation=length((donor-uv)/u_post[0].xy);
            vec2 blur=u_post[0].xy*(1.+rough*8.);
            vec4 centre=cabinSample(donor);
            vec4 a=cabinSample(donor+blur),b=cabinSample(donor-blur);
            vec3 hitColour=(centre.rgb*2.+a.rgb+b.rgb)/max(.001,centre.a*2.+a.a+b.a);
            vec3 hitNormal=texture2D(s_ringLayer,donor).xyz;
            float facing=smoothstep(.02,.30,dot(normalize(hitNormal+vec3(1e-8)),-ray));
            confidence=smoothstep(.008,.10,border)*smoothstep(3.,15.,separation)*
                       smoothstep(.85,.99,centre.a)*facing*
                       (1.-smoothstep(thickness*.30,thickness,gap));
            reflected=hitColour;
            break;
        }
        previous=t;
    }
    // Distant bodies and the sky can be reflected through an actual visible
    // window opening. Covered pixels and off-screen directions fade back to
    // the stable analytic fixture lighting; no previous frame is sampled.
    if(ray.z<-.06){
        vec2 skyUV=cabinProject(ray);
        float edge=min(min(skyUV.x,skyUV.y),min(1.-skyUV.x,1.-skyUV.y));
        if(edge>.0){
            float opening=1.-smoothstep(.02,.18,cabinSample(skyUV).a);
            float skyConfidence=opening*smoothstep(.0,.12,edge)*.74;
            vec3 sky=texture2D(s_scene,skyUV).rgb;
            reflected=mix(sky,reflected,confidence);
            confidence=mix(skyConfidence,1.,confidence);
        }
    }
    float fresnel=pow(1.-max(0.,dot(N,V)),5.);
    float amount=reflectance*(.72+fresnel*.54)*(1.-smoothstep(.22,.75,rough));
    return mix(body,reflected,clamp(amount*confidence,0.,.52));
}

vec4 cabinLens(vec2 uv,vec4 sharp,float field){
    if(field<.025)return sharp;
    float aspect=u_lens[1].y/u_lens[1].x;
    vec2 radial=vintageRadial(uv,aspect);
    float radius=field*field*4.8;
    vec4 gathered=sharp*2.;
    for(int tap=0;tap<8;++tap){
        float angle=(float(tap)+.5)*2.3999632;
        float reach=sqrt((float(tap)+.5)/8.);
        vec2 offset=vintagePupil(radial,field,angle,reach)*u_lens[1].xy*radius;
        // Filter premultiplied colour and coverage together at window edges.
        gathered+=cabinSample(uv+offset);
    }
    return mix(sharp,gathered*.10,field*.82);
}

vec3 cabinLensGlow(vec2 uv,float field){
    vec2 radial=vintageRadial(uv,u_lens[1].y/u_lens[1].x);
    vec3 gathered=texture2D(s_bloom,uv).rgb*2.;
    for(int tap=0;tap<8;++tap){
        float angle=(float(tap)+.5)*2.3999632;
        float reach=sqrt((float(tap)+.5)/8.);
        vec2 offset=vintagePupil(radial,field,angle,reach)*u_lens[1].xy*(9.+field*32.);
        gathered+=texture2D(s_bloom,uv+offset).rgb;
    }
    return vintageGlare(gathered*.10);
}

vec3 cabinSunbeams(vec2 uv){
    if(u_cabinSun[0].w<.001)return vec3(0.);
    vec3 eye=mul(u_cabinFromEye,vec4(0.,0.,0.,1.)).xyz;
    if(eye.z>1.16 || abs(eye.x)>1.1)return vec3(0.);
    vec3 cameraRay=normalize(vec3((uv*2.-1.)*u_post[0].zw,-1.));
    vec3 direction=normalize(mul(mat3(u_cabinFromEye),cameraRay));
    vec3 light=normalize(u_cabinSun[0].xyz);
    float phase=.60*pow(max(0.,dot(direction,light)),12.);
    if(phase<.004)return vec3(0.);
    vec3 inv=sign(direction+vec3(.000001))/max(abs(direction),vec3(.000001));
    vec3 a=(vec3(-1.025,-.82,-1.462)-eye)*inv;
    vec3 b=(vec3(1.025,.56,1.14)-eye)*inv;
    vec3 lower=min(a,b),upper=max(a,b);
    float start=max(0.,max(lower.x,max(lower.y,lower.z)));
    float end=min(upper.x,min(upper.y,upper.z));
    end=min(end,cabinDepth(uv)/max(-cameraRay.z,.001));
    if(end<=start)return vec3(0.);
    float stepLength=(end-start)/8.,scatter=0.;
    for(int tap=0;tap<8;++tap){
        float distance=start+(float(tap)+.5)*stepLength;
        vec3 p=eye+direction*distance;
        scatter+=cabinWindowTransmission(p,light)*exp(-distance*.075)*stepLength;
    }
    // A thin, fixed particulate medium is visible only inside the forward
    // hull. There is no full-screen veil, animated grain or old-frame image.
    vec3 tint=mix(cabinWindowLight(),u_cabinSun[1].rgb,.64);
    return tint*scatter*phase*.075*u_cabinSun[0].w;
}

void main(){
    vec2 uv=v_texcoord0, pixel=u_post[0].xy;
    vec4 cabin=cabinSample(uv);
    vec4 a=cabinSample(uv+pixel*vec2(-.8,-.8));
    vec4 b=cabinSample(uv+pixel*vec2(.8,-.8));
    vec4 c=cabinSample(uv+pixel*vec2(-.8,.8));
    vec4 d=cabinSample(uv+pixel*vec2(.8,.8));
    float edge=max(max(abs(a.a-cabin.a),abs(b.a-cabin.a)),
                   max(abs(c.a-cabin.a),abs(d.a-cabin.a)));
    float energy=length(abs(a.rgb+d.rgb-b.rgb-c.rgb))*.5;
    cabin=mix(cabin,(a+b+c+d)*.25,.48*max(edge,smoothstep(.09,.43,energy)));
    cabin.rgb*=cabinContact(uv,cabin.rgb);
    float field=vintageField(uv,u_lens[1].y/u_lens[1].x);
    if(u_lens[0].x>.5)cabin=cabinLens(uv,cabin,field);
    cabin.rgb=cabinSceneReflection(uv,cabin.rgb/max(cabin.a,.0001),cabin.a)*cabin.a;
    if(u_lens[0].x>.5){
        vec3 body=vintageGrade(cabin.rgb/max(cabin.a,.0001),field);
        cabin.rgb=body*cabin.a;
    }
    vec3 exterior=texture2D(s_scene,uv).rgb;
    vec3 halo=texture2D(s_bloom,uv).rgb;
    if(u_lens[0].x>.5)
        halo=cabinLensGlow(uv,field)*.49;
    else
        halo=halo/(.78+max(halo.r,max(halo.g,halo.b)))*.31;
    gl_FragColor=vec4(cabin.rgb+exterior*(1.-cabin.a)+halo+cabinSunbeams(uv),1.);
}

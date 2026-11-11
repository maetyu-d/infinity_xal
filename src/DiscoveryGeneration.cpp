#define GLM_FORCE_INLINE
#include "infinity/Discovery.hpp"
#include "infinity/Prospecting.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace infinity {
namespace {
bool cancelled(const std::atomic<bool>* c) { return c&&c->load(); }
glm::dvec3 direction(const Random& r,uint64_t k) {
    const double z=r.signedUnit(k),a=r.unit(k+1)*2*std::numbers::pi;
    return {std::sqrt(1-z*z)*std::cos(a),z,std::sqrt(1-z*z)*std::sin(a)};
}
std::optional<RelicSite> relicSite(const Universe& u,const Node& p,const Random& r,const std::atomic<bool>* cancel) {
    if(!hasSolidSurface(p)||!p.geology||p.environment.ageGyr<.3||p.environment.temperature>950)return {};
    const double radius=std::pow(10.,p.frame.logRadius);
    std::optional<glm::dvec3> ground;
    for(unsigned i=0;i<48;++i){
        if(cancelled(cancel))return {};
        const auto d=direction(r,100+i*2);const auto h=u.terrainHeight(p,d);
        if(p.geology->liquidWater>=.08&&h<5)continue;
        if(glm::dot(d,u.terrainNormal(p,d))<.97)continue;
        ground=d*(radius+h);break;
    }
    if(!ground)return {};
    const auto rockAddress=samplingSourceNear(u,p,*ground,0);
    const auto rock=u.describe(rockAddress);
    const auto normal=rock.geographicDirection;
    const auto height=u.terrainHeight(p,normal);
    if((p.geology->liquidWater>=.08&&height<5)||glm::dot(normal,u.terrainNormal(p,normal))<.95)return {};
    RelicSite site;
    site.metres=normal*(radius+height);
    const auto axes=tangentFrame(normal);
    site.ageYears=80+std::floor(r.unit(20)*2400);
    const unsigned purpose=p.environment.chemistry.metal>.3?0:p.geology->ice>.3?1:2;
    constexpr std::array<std::string_view,3> purposes{"EXTRACTION OUTPOST","CLIMATE STATION","LISTENING ARRAY"};
    site.purpose=purposes[purpose];
    site.name="RELIC "+std::to_string(r.bits(21)%100000);
    // The generated local history inherits the real substrate and environment.
    std::array<double,4> stress{rock.rock.impact,rock.environment.peakTemperature/3200,
        p.geology->ice*(.5+rock.environment.strain),rock.rock.weathering+.15};
    site.cause=RuinCause(unsigned(std::max_element(stress.begin(),stress.end())-stress.begin()));
    const unsigned abandonment=unsigned(site.ageYears*(.15+.5*r.unit(22)));
    site.chronicle={std::to_string(unsigned(site.ageYears))+" YEARS AGO / "+site.purpose+" ESTABLISHED",
        std::to_string(abandonment)+" YEARS AGO / "+std::string(ruinCauseName(site.cause)),
        "PRESENT / "+std::string(rock.rock.weathering>.5?"WEATHERED FOUNDATIONS":"PARTLY PRESERVED STRUCTURE")};
    const auto grains=u.children(rock,1);if(grains.empty())return {};
    const auto grain=u.describe(grains.front());const auto lattices=u.children(grain,1);if(lattices.empty())return {};
    const auto lattice=u.describe(lattices.front());
    const double rockRadius=std::pow(10.,rock.frame.logRadius);
    site.evidence[0]={p.address,site.metres,1,350,"FOUNDATIONS",site.purpose+" / "+std::to_string(unsigned(site.ageYears))+" YEAR OCCUPATION LAYER",p.address};
    constexpr std::array<std::string_view,4> scars{"RADIAL BREAKS / EJECTA OVERLIES OCCUPATION","THERMAL SPALLING / COLLAPSE AFTER OCCUPATION",
        "SHEARED FOOTINGS / ICE TRANSPORT AFTER OCCUPATION","ROUNDED EDGES / GRADUAL LOSS OF OCCUPATION"};
    site.evidence[1]={rock.address,{0,0,0},.01,std::max(rockRadius*4,.7),"FRACTURE CONTACT",std::string(scars[unsigned(site.cause)]),rock.address};
    const auto& mineral=material(lattice.environment.mineral);
    site.evidence[2]={rock.address,{0,0,0},1e-5,std::max(rockRadius*4,.7),
        "MICRODIFFRACTION",std::string(mineral.name)+" / "+std::string(mineral.lattice)+" / GEOLOGICAL SUBSTRATE PREDATES THE STRUCTURE",lattice.address};
    // Preserve native material positions: ruins surround the host outcrop.
    site.metres=normal*(radius+height);
    const auto groundAt=[&](glm::dvec3 local){const auto d=glm::normalize(site.metres+axes*local);return d*(radius+u.terrainHeight(p,d));};
    site.landing=groundAt({0,0,135});
    for(unsigned i=0;i<22;++i){
        if(cancelled(cancel))return {};
        const double angle=2*std::numbers::pi*i/22;
        const double ring=purpose==2?58:45;
        glm::dvec3 local{std::cos(angle)*ring,0,std::sin(angle)*ring};
        if(purpose==0)local={double(int(i%5)-2)*19,0,double(int(i/5)-2)*19};
        if(purpose==1)local={i<11?-28.:28.,0,double(int(i%11)-5)*11};
        if(site.cause==RuinCause::IceMovement&&i%2)local.x+=12;
        if(glm::length(local)<rockRadius*4+10)continue; // Keep the real host outcrop accessible.
        auto point=groundAt(local);const auto d=glm::normalize(point);
        if(p.geology->liquidWater>=.08&&glm::length(point)-radius<3)continue;
        const bool fallen=(r.bits(300+i)%4)==0;
        const double damage=site.cause==RuinCause::Impact?.55:site.cause==RuinCause::Weathering?.3:.45;
        glm::dvec3 dimensions{6+r.unit(400+i)*7,fallen?1.3:3+r.unit(500+i)*12*(1-damage),4+r.unit(600+i)*5};
        auto q=tangentFrame(d)*glm::angleAxis(angle,glm::dvec3(0,1,0));
        const auto tint=glm::mix(material(rock.environment.mineral).color,glm::vec3(.24,.46,.48),.5f)*float(.65+r.unit(700+i)*.2);
        site.blocks.push_back({point,dimensions,q,tint,purpose==2&&i%5==0?1u:0u});
    }
    return site;
}
}
std::string_view ruinCauseName(RuinCause cause) {
    constexpr std::array<std::string_view,4> names{"IMPACT COLLAPSE","THERMAL FAILURE","ICE MOVEMENT","GRADUAL ABANDONMENT"};
    return unsigned(cause)<names.size()?names[unsigned(cause)]:"UNKNOWN";
}
std::optional<SignalSource> generateSignal(const Universe& u,const Node& planet,unsigned channel,const std::atomic<bool>* cancel) {
    if(planet.address.kind()!=Kind::Planet||channel>1||cancelled(cancel))return {};
    SignalSource s;s.id={planet.address,channel};s.seed=mix64(planet.seed^0x5349474e414c3635ULL^mix64(channel));
    const Random r{s.seed};s.planetRadius=std::pow(10.,planet.frame.logRadius);
    s.frequency=80+std::floor(r.unit(1)*360)*2.5;s.bandwidth=3+r.unit(2)*5;s.period=1.5+r.unit(3)*3;
    s.reach=3e9*(.5+r.unit(4));s.worldLabel=planet.label();
    if(channel==1){
        s.relic=relicSite(u,planet,r,cancel);if(!s.relic)return {};
        s.reach*=std::exp(-s.relic->ageYears/2400);
        s.kind=SignalKind::Archive;s.metres=s.relic->metres+glm::normalize(s.relic->metres)*8.;
        s.signature="REPEATING ARTIFICIAL CLOCK / ARCHIVE FRAME";
    }else{
        s.kind=hasSolidSurface(planet)&&planet.geology&&planet.geology->ice>.3?SignalKind::IceResonance:SignalKind::Magnetosphere;
        s.metres=direction(r,10)*s.planetRadius*(s.kind==SignalKind::IceResonance?1.015:1.16);
        s.bandwidth=12+r.unit(25)*16;
        s.signature=s.kind==SignalKind::IceResonance?"ICE FRACTURE HARMONICS / NATURAL":"MAGNETOSPHERIC HARMONICS / NATURAL";
    }
    s.descriptor=mix64(s.seed^descriptorFingerprint(planet));
    return s;
}
std::shared_ptr<const DiscoveryPage> generateDiscoveryPage(const Universe& u,const Address& address,const std::atomic<bool>* cancel) {
    if(address.kind()!=Kind::System)return {};
    auto page=std::make_shared<DiscoveryPage>();page->system=address;
    const auto system=u.describe(address);const auto planets=u.children(system,256);
    unsigned natural=0,artificial=0;
    for(const auto& a:planets){
        if(cancelled(cancel))return {};
        const auto p=u.describe(a);
        if(natural<4){if(auto s=generateSignal(u,p,0,cancel)){page->sources.push_back(std::move(*s));++natural;}}
        if(artificial<6&&hasSolidSurface(p)){if(auto s=generateSignal(u,p,1,cancel)){page->sources.push_back(std::move(*s));++artificial;}}
        if(natural==4&&artificial==6)break;
    }
    std::sort(page->sources.begin(),page->sources.end(),[](const auto& a,const auto& b){return a.frequency<b.frequency;});
    return page;
}
size_t DiscoveryPage::bytes() const {
    size_t n=sizeof(*this)+sources.capacity()*sizeof(SignalSource);
    for(const auto& s:sources)if(s.relic)n+=sizeof(RelicSite)+s.relic->blocks.capacity()*sizeof(RelicBlock)+2048;
    return n;
}
DiscoveryCache::~DiscoveryCache(){ {std::lock_guard lock(mutex_);for(auto& e:entries_)e.cancel->store(true);}tasks_.wait(); }
std::shared_ptr<const DiscoveryPage> DiscoveryCache::request(const Universe& u,const Address& system){
    if(system.kind()!=Kind::System)return {};
    std::lock_guard lock(mutex_);++serial_;
    for(auto& e:entries_)if(e.system==system){e.used=serial_;return e.page;}
    if(pending_>=2)return {};
    if(entries_.size()>=3){auto old=std::min_element(entries_.begin(),entries_.end(),[](const auto& a,const auto& b){return a.used<b.used;});old->cancel->store(true);entries_.erase(old);}
    auto token=std::make_shared<std::atomic<bool>>(false);entries_.push_back({system,serial_,token,{}});++pending_;
    tasks_.run([this,&u,system,token]{
        std::shared_ptr<const DiscoveryPage> page;try{page=generateDiscoveryPage(u,system,token.get());}catch(...){}
        std::lock_guard done(mutex_);--pending_;
        for(auto& e:entries_)if(e.system==system&&e.cancel==token){e.page=std::move(page);break;}
    });return {};
}
void DiscoveryCache::wait(){tasks_.wait();}
size_t DiscoveryCache::bytes() const{std::lock_guard lock(mutex_);size_t n=0;for(const auto& e:entries_)if(e.page)n+=e.page->bytes();return n;}
} // namespace infinity

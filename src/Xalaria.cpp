#include "infinity/Xalaria.hpp"
#include "infinity/StellarEvolution.hpp"
#include <algorithm>
#include <atomic>
#include <charconv>
#include <cmath>
#include <fstream>
#include <numbers>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>

namespace infinity {
namespace {
struct XalariaBindings {
    std::shared_ptr<const XalariaCatalog> catalog;
    std::unordered_map<uint64_t,std::string> names;
    std::unordered_map<uint64_t,const XalariaWorld*> worlds;
};
std::shared_ptr<const XalariaBindings> bindings;
std::vector<std::string> fields(const std::string& row,char sep='\t') {
    std::vector<std::string> out; size_t start=0,end;
    do {end=row.find(sep,start);out.push_back(row.substr(start,end-start));start=end+1;} while(end!=std::string::npos);
    return out;
}
uint64_t integer(const std::string& s) {
    uint64_t n=0;auto [end,error]=std::from_chars(s.data(),s.data()+s.size(),n);
    if(error!=std::errc{}||end!=s.data()+s.size())throw std::runtime_error("Invalid catalogue integer");
    return n;
}
double number(const std::string& s) {
    size_t end=0;double n=std::stod(s,&end);
    if(end!=s.size()||!std::isfinite(n))throw std::runtime_error("Invalid catalogue number");
    return n;
}
std::string upper(std::string_view s){std::string r(s);for(auto& c:r)if(c>='a'&&c<='z')c=char(c-32);return r;}
}
XalariaCatalog::XalariaCatalog(const std::filesystem::path& path) {
    std::ifstream file(path);if(!file)throw std::runtime_error("Cannot open Xalaria catalogue: "+path.string());
    std::string row;std::getline(file,row);auto header=fields(row);
    if(header.size()!=4||header[0]!="XALARIA_CATALOG"||header[1]!="1")throw std::runtime_error("Unsupported Xalaria catalogue");
    fingerprint=header[2];grid=unsigned(integer(header[3]));if(grid!=200)throw std::runtime_error("Unexpected map grid");
    while(std::getline(file,row)) {
        if(row.empty())continue;auto f=fields(row);
        if(f[0]=="S"&&f.size()==11) {
            XalariaStar s;s.seed=uint32_t(integer(f[1]));s.spectralClass=unsigned(integer(f[2]));s.name=f[3];
            s.radius=number(f[4]);s.temperature=number(f[5]);s.disc=number(f[6]);s.technology=unsigned(integer(f[7]));
            s.lifeGrade=unsigned(integer(f[8]));s.lifeWorlds=unsigned(integer(f[9]));
            if(f[10]!="-")for(const auto& v:fields(f[10],','))s.wormholes.push_back(uint32_t(integer(v)));
            if(s.seed>=grid*grid||s.spectralClass>6||s.radius<=0||s.temperature<=0||stars.contains(s.seed))throw std::runtime_error("Invalid/duplicate star");
            stars.emplace(s.seed,std::move(s));
        } else if(f[0]=="W"&&f.size()==24) {
            auto& s=stars.at(uint32_t(integer(f[1])));XalariaWorld w;
            w.id=integer(f[2]);w.uniqueId=integer(f[3]);w.name=f[4];w.primary=std::stoi(f[5]);
            w.radius=number(f[6]);w.distance=number(f[7]);w.period=number(f[8]);w.tilt=number(f[9]);w.tiltRotation=number(f[10]);
            w.temperature=number(f[11]);w.composition=std::stoi(f[12]);w.lifeStructure=std::stoi(f[13]);w.intelligence=std::stoi(f[14]);
            w.life=integer(f[15])!=0;w.communities=unsigned(integer(f[16]));w.stations=unsigned(integer(f[17]));w.mines=unsigned(integer(f[18]));
            w.freezing=integer(f[19])!=0;w.cloudCover=number(f[20]);w.cloudColor=f[21];w.ringInner=number(f[22]);w.ringOuter=number(f[23]);
            if(w.radius<=0||w.distance<=0||w.period<0||s.worlds.contains(w.id))throw std::runtime_error("Invalid/duplicate world");
            
            ++worldCount;lifeCount+=w.life;communityCount+=w.communities;s.worlds.emplace(w.id,std::move(w));
        } else throw std::runtime_error("Malformed Xalaria catalogue row");
    }
    if(stars.empty()||!worldCount)throw std::runtime_error("Empty Xalaria catalogue");
    for(const auto& [seed,s]:stars){
        for(auto to:s.wormholes)if(!stars.contains(to))throw std::runtime_error("Dangling wormhole");
        for(const auto& [id,w]:s.worlds)if(w.primary>=0&&!s.worlds.contains(uint64_t(w.primary)))throw std::runtime_error("Missing moon primary");
    }
}
const XalariaStar& XalariaCatalog::star(uint64_t seed) const {return stars.at(uint32_t(seed));}
const XalariaWorld& XalariaCatalog::world(const Address& a) const {return star(a.parent().id()).worlds.at(a.id());}
XalariaRegion XalariaCatalog::region(uint32_t seed) const {
    const unsigned x = (seed % grid) / regionSide;
    const unsigned y = (seed / grid) / regionSide;
    const uint64_t regionIndex = x + y * (grid / regionSide);
    // A region keeps its look across visits and catalogue reloads.
    if (mix64(0x45544348454441d8ULL ^ regionIndex) % 4 == 0)
        return {x, y, XalariaLook::AcidEtch};
    if (mix64(0x544944414c424c1bULL ^ regionIndex) % 3 == 0)
        return {x, y, XalariaLook::TidalBlue};
    const bool fog = (mix64(0x58414c4152494100ULL ^ regionIndex) & 1) != 0;
    return {x, y, fog ? XalariaLook::VerdantFog : XalariaLook::CharredGlass};
}
std::string_view xalariaLookName(XalariaLook look) {
    switch (look) {
    case XalariaLook::AcidEtch: return "ACID ETCH";
    case XalariaLook::TidalBlue: return "TIDAL BLUE";
    case XalariaLook::VerdantFog: return "VERDANT FOG";
    default: return "CHARRED GLASS";
    }
}
Address XalariaCatalog::address(uint32_t seed) const {star(seed);return Address{}.child(Kind::Galaxy,0).child(Kind::System,seed);}
glm::dvec3 XalariaCatalog::mapPosition(uint32_t seed) const {
    star(seed);const double x=seed%grid,y=seed/grid;
    const double lift=1.5*std::sin(x*.105+1.3)*std::cos(y*.083)+.7*std::sin((x+y)*.047+.4);
    return {(x-grid/2.)/110.,lift/110.,(y-grid/2.)/110.};
}
std::vector<uint32_t> XalariaCatalog::route(uint32_t from,uint32_t to) const {
    star(from);star(to);std::queue<uint32_t> pending;pending.push(from);
    std::unordered_map<uint32_t,uint32_t> previous{{from,from}};
    while(!pending.empty()){
        auto current=pending.front();pending.pop();if(current==to)break;
        for(auto next:star(current).wormholes)if(!previous.contains(next)){previous[next]=current;pending.push(next);}
    }
    if(!previous.contains(to))return {};
    std::vector<uint32_t> out{to};while(out.back()!=from)out.push_back(previous.at(out.back()));
    std::reverse(out.begin(),out.end());return out;
}
std::vector<uint32_t> XalariaCatalog::search(std::string_view query,size_t limit) const {
    std::vector<uint32_t> out;const auto q=upper(query);if(q.empty()||!limit)return out;
    std::vector<std::pair<size_t,uint32_t>> ranked;
    for(const auto& [id,s]:stars){
        size_t rank=SIZE_MAX;
        const auto score=[&](const std::string& name,bool world){const auto text=upper(name);const auto at=text.find(q);
            if(at!=std::string::npos)rank=std::min(rank,text==q?size_t(world?1:0):at==0?size_t(world?3:2):size_t(4+at));};
        score(s.name,false);if(std::to_string(id)==q)rank=0;
        for(const auto& [wid,w]:s.worlds)score(w.name,true);
        if(rank!=SIZE_MAX)ranked.emplace_back(rank,id);
    }
    std::sort(ranked.begin(),ranked.end());
    for(const auto& [rank,id]:ranked){out.push_back(id);if(out.size()==limit)break;}
    return out;
}
std::optional<uint64_t> XalariaCatalog::searchWorld(uint32_t system,std::string_view query) const {
    const auto q=upper(query);
    const auto& s=star(system);
    if(q.empty()||upper(s.name).find(q)!=std::string::npos)
        return {};
    std::optional<uint64_t> partial;
    for(const auto& [id,world] : s.worlds) {
        const auto name=upper(world.name);
        if(name==q)
            return id;
        if(!partial&&name.find(q)!=std::string::npos)
            partial=id;
    }
    return partial;
}
void XalariaCatalog::apply(Node& n,const Node& p) const {
    if(n.address.kind()==Kind::System){
        const auto& s=star(n.address.id());n.frame.position=mapPosition(s.seed);
        n.frame.logRadius=std::log10(std::max(s.disc,s.radius*100));
        n.temperatureK=s.temperature;n.stellarRadiusMetres=s.radius;
        n.luminositySolar=std::pow(s.radius/solarRadiusMetres,2)*std::pow(s.temperature/5772,4);
        n.massSolar=std::clamp(std::pow(n.luminositySolar,1/3.5),.08,80.);n.companions=0;
        auto st=std::make_shared<StellarProcess>();st->temperatureK=n.temperatureK;st->radiusMetres=s.radius;
        st->luminositySolar=n.luminositySolar;st->massSolar=n.massSolar;st->initialMassSolar=n.massSolar;st->ageGyr=4;
        n.stellar=st;n.environment.temperature=s.temperature;n.environment.ageGyr=4;
        for(const auto& [id,w]:s.worlds)if(w.primary<0)n.frame.logRadius=std::max(n.frame.logRadius,std::log10(w.distance*1.5));
        return;
    }
    const auto& w=world(n.address);
    n.frame.logRadius=n.planetLogRadius=std::log10(w.radius);n.planetSeed=n.seed;n.expandedPopulation=true;
    n.satelliteOf=w.primary<0?UINT64_MAX:uint64_t(w.primary);
    const double a=w.period>0?std::fmod(1700000000./w.period,1.)*2*std::numbers::pi:0;
    n.orbit={w.distance,0,a,std::atan(w.tilt),p.massSolar};
    if(w.period>0)n.orbit.centralMassSolar=4*std::numbers::pi*std::numbers::pi*std::pow(w.distance,3)/(1.32712440018e20*w.period*w.period);
    glm::dvec3 pos{w.distance*std::cos(a),w.distance*w.tilt*std::sin(a+w.tiltRotation),w.distance*std::sin(a)};
    if(w.primary>=0){
        Node primary;primary.address=p.address.child(Kind::Planet,uint64_t(w.primary));primary.environment=p.environment;
        apply(primary,p);pos+=primary.frame.position*std::pow(10.,p.frame.logRadius);
    }
    n.frame.position=pos/std::pow(10.,p.frame.logRadius);
    auto& e=n.environment;e.temperature=std::max(2.725,w.temperature);e.ageGyr=4;
    e.chemistry=w.freezing?Composition{.40,.12,.08,.40}:Composition{.68,.22,.08,.02};
    e.pressureGPa=w.composition==0?.3:(w.cloudCover>0?.0001:.00001);
    e.waterActivity=w.life&&e.temperature>240&&e.temperature<400?.6:(w.freezing?.04:.01);
    e.peakTemperature=std::max(1100.,e.temperature);e.coolingRate=.5;e.strain=.25;e.impact=.3;
    e.mineral=e.temperature<3900?selectMineral(n.seed,e):Mineral::Graphite;n.bulkDensity=w.composition==0?1100:4200;
    n.massSolar=4.188790204786391*std::pow(w.radius,3)*n.bulkDensity/1.98847e30;
    n.geology=planetProcess(n.seed,n.frame.logRadius,e,true,w.composition==0);
}
void bindXalariaLabels(std::shared_ptr<const XalariaCatalog> c,uint64_t universeSeed){
    auto b=std::make_shared<XalariaBindings>();b->catalog=c;
    for(const auto& [id,s]:c->stars){
        const auto a=c->address(id);b->names[seedOf(a,universeSeed)]=s.name;
        for(const auto& [wid,w]:s.worlds){
            const auto key=seedOf(a.child(Kind::Planet,wid),universeSeed);
            b->names[key]=w.name;b->worlds[key]=&w;
        }
    }
    std::atomic_store(&bindings,std::shared_ptr<const XalariaBindings>(b));
}
std::string xalariaObjectLabel(uint64_t seed){
    const auto b=std::atomic_load(&bindings);if(!b)return {};
    const auto it=b->names.find(seed);return it==b->names.end()?std::string{}:it->second;
}
std::shared_ptr<const XalariaWorld> xalariaWorldFacts(uint64_t seed){
    const auto b=std::atomic_load(&bindings);if(!b)return {};
    const auto it=b->worlds.find(seed);
    return it==b->worlds.end()?std::shared_ptr<const XalariaWorld>{}:std::shared_ptr<const XalariaWorld>(b->catalog,it->second);
}
} // namespace infinity

#include "infinity/XalariaStrategy.hpp"
#include "infinity/Cockpit.hpp"
#include "AtlasFont.hpp"
#include <algorithm>
namespace infinity {
XalariaStrategyLayout::XalariaStrategyLayout(double aspect):width(900*aspect),close{width-166,20,134,40},list{32,252,width-64,280}{
 for(unsigned i=0;i<3;++i)tabs[i]={32+i*(width-64)/3,166,(width-64)/3-8,36};
 for(unsigned i=0;i<4;++i)acquire[i]={32+i*(width-64)/4,646,(width-64)/4-8,36};
 for(unsigned i=0;i<6;++i){orders[i]={32+i*(width-64)/6,693,(width-64)/6-8,36};intel[i]={32+i*(width-64)/6,740,(width-64)/6-8,36};}
}
CockpitOverlay xalariaStrategyOverlay(const XalariaStrategy& strategy,const XalariaEconomy& economy,double aspect){
 CockpitOverlay hud;const double width=900*aspect;const glm::vec3 ink(.86,.93,.95),muted(.46,.64,.69),amber(1.,.72,.32);
 const XalariaStrategyLayout layout(aspect);
 const auto panel=[&](XalariaMapRect r,glm::vec3 colour){auto& m=hud.panels;auto n=uint32_t(m.vertices.size());const auto c=rgba(colour);m.vertices.insert(m.vertices.end(),{{float(r.x),float(r.y),0,c},{float(r.x+r.w),float(r.y),0,c},{float(r.x),float(r.y+r.h),0,c},{float(r.x+r.w),float(r.y+r.h),0,c}});m.indices.insert(m.indices.end(),{n,n+1,n+2,n+1,n+3,n+2});};
 panel({0,0,width,900},{.008,.014,.021});
 const auto button=[&](XalariaMapRect r,std::string_view label,bool active=false){panel(r,active?glm::vec3(.11,.22,.27):glm::vec3(.033,.061,.078));atlasText(hud,label,r.x+9,r.y+10,r.w-18,16,active?amber:ink);};
 button(layout.close,"Back / F10");
 constexpr std::array pages{"Your fleet","Earned intelligence","Discovered campaigns"};for(unsigned i=0;i<3;++i)button(layout.tabs[i],pages[i],strategy.page==i);
 constexpr std::array acquire{"C  Claim boarded hull","H  Captain / repair","B  Buy freighter","W  Buy warship"};for(unsigned i=0;i<4;++i)button(layout.acquire[i],acquire[i]);
 constexpr std::array orders{"0  Hold","1  Follow","2  Trade","3  Escort","4  Blockade","5  Salvage"};if(strategy.page==0)for(unsigned i=0;i<6;++i)button(layout.orders[i],orders[i]);
 constexpr std::array intel{"N  Local scan","I  Informant","A  Allegiance","S  Salvage wreck","R  Rescue crew","T  Collect fees"};for(unsigned i=0;i<6;++i)button(layout.intel[i],intel[i]);
 auto line=[&](std::string_view text,double y,double size=18.,glm::vec3 colour=glm::vec3(.86,.93,.95)){atlasText(hud,text,32,y,width-64,size,colour);};
 atlasText(hud,"Fleet command / Living universe",32,24,width-230,32,ink);line("F10 / Esc  Return     Tab  Fleet | Intelligence | Campaigns     Up / Down  Select",76,16,muted);
 line("Treasury "+xMoney(economy.credits())+"   /   Fleet "+std::to_string(strategy.fleet().size())+"   /   Rescued captains "+std::to_string(strategy.recruits().size()),112,19,amber);
 line("Allegiance / "+std::string(xFactionName(strategy.allegiance())),141,16,muted);
 std::vector<std::string> rows;
 if(strategy.page==0){
  line("YOUR SHIPS",214,22);for(const auto& [id,f]:strategy.fleet())rows.push_back(std::to_string(id)+"  "+f.name+"  /  "+(f.lost?"LOST":f.captain.empty()?"Captain required":f.captain)+"  /  "+std::string(fleetOrderName(f.order)));
  if(rows.empty())line("Board a surrendered hull and press C, or dock and buy a ship with B / W.",252,19,muted);
  if(!strategy.fleet().empty()){
   auto it=strategy.fleet().begin();std::advance(it,std::min(strategy.row,strategy.fleet().size()-1));const auto& f=it->second;int64_t cargo=0;for(auto n:f.cargo)cargo+=n;
   line("Selected: "+f.name+" / hull "+std::to_string(int(f.hull))+"% / drives "+std::to_string(int(f.engines))+"% / loyalty "+std::to_string(f.loyalty)+" / cargo "+std::to_string(cargo)+(f.contraband?" (contraband)":""),552,18,amber);
   line("Destination / "+economy.placeName(f.destination)+(f.underway?" / ETA "+std::to_string(int(std::max(0.,f.arrival-strategy.time())))+" s":" / holding")+" / missions "+std::to_string(f.missions),585,17,muted);
  }
 }else if(strategy.page==1){
  line("EARNED INTELLIGENCE",214,22);
  for(auto it=strategy.reports().rbegin();it!=strategy.reports().rend();++it)if(strategy.knowledge().contains(it->system))rows.push_back(std::to_string(it->system)+" / "+it->text);
  if(rows.empty())line("No reports yet. Dock, scan local activity or speak to a crew.",252,19,muted);
 }else{
  line("DISCOVERED CAMPAIGNS",214,22);
  for(const auto& [id,c]:strategy.campaigns())if(strategy.knowledge().contains(c.origin.system)||strategy.knowledge().contains(c.target.system))rows.push_back(std::to_string(id)+" / "+economy.placeName(c.target)+" / "+(c.finished?(c.victory?"Surrendered":"Withdrawn"):"Blockade active")+" / "+std::string(xFactionName(c.attacker)));
  if(rows.empty())line("No frontier campaigns have been discovered.",252,19,muted);
  if(economy.currentWorld()){auto venues=economy.venues(economy.currentSystem(),*economy.currentWorld());double y=555;for(const auto& p:venues)if(!p.city&&y<600){line(economy.placeName(p)+" / "+strategy.advisory(p),y,16,muted);y+=29;}}
 }
 const auto start=(strategy.row/7)*7;for(size_t i=start;i<std::min(rows.size(),start+7);++i)line((i==strategy.row?"> ":"  ")+rows[i],252+(i-start)*40,18,i==strategy.row?amber:ink);
 line("Atlas destination / "+strategy.destinationLabel+" / close F10, then F2 to choose / Escort needs a scheduled service",615,15,muted);
 line(strategy.message,800,18,amber);line("Supply consumption, shipyards, expeditions and captain orders continue while you fly or visit a station.",858,15,muted);
 return hud;
}
}

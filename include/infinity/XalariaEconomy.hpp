#pragma once
#include "Xalaria.hpp"
#include "Piloting.hpp"
#include <array>
#include <set>

namespace infinity {
enum class XGood : uint8_t { Aluminium,Iron,Tin,Nickel,Chromium,Titanium,Iridium,Marble,Granite,Nanomites,Seed,Neurocells,Count };
inline constexpr size_t xGoodCount=size_t(XGood::Count);
using XStock=std::array<int64_t,xGoodCount>;
struct XGoodInfo {std::string_view name;int64_t base;bool structural=false;};
const XGoodInfo& xGoodInfo(XGood);
std::string xMoney(int64_t cents);
enum class XFaction : uint8_t { Trade,Nature,Freedom,Rebel,Independent };
std::string_view xFactionName(XFaction);
struct XPlace {
    uint32_t system=0;uint64_t world=0;uint32_t station=0;bool city=false;
    auto operator<=>(const XPlace&) const=default;
};
struct XMarket {XStock stock{},target{};int64_t cash=100000000000LL;int64_t restockedDay=0;};
struct XQuote {int64_t units=0,total=0,fee=0;bool available=false;};
enum class XAssetKind : uint8_t { Station,Vault,Farm };
enum class XAssetState : uint8_t { Frame,Active,Lost };
struct XArtwork {uint64_t id=0;int64_t value=0,placedDay=0;};
struct XAsset {
    uint32_t id=0;XAssetKind kind=XAssetKind::Station;XAssetState state=XAssetState::Frame;
    uint32_t system=0;uint64_t world=0;unsigned level=1;
    int64_t nanoNeeded=0,metalNeeded=0,seedNeeded=0,till=0,culturalTill=0,lastFed=0,lastPaidDay=0;
    unsigned guards=0,lasers=0;uint64_t guardCity=UINT64_MAX;bool tradeFlag=false,fuelStation=false,hall=false,gallery=false;
    XStock stored{};std::vector<XArtwork> artwork;
};
struct XDeposit {uint32_t system=0;uint64_t world=0;unsigned index=0;XGood good=XGood::Iron;int64_t total=0,left=0,restored=0;bool filled=false;};
struct XContract {uint64_t id=0;XPlace origin,destination;int64_t deadline=0,reward=0,units=0;enum class State {Active,Delivered,Expired} state=State::Active;};
struct XOpportunity {XPlace destination;int64_t profit=0;unsigned hops=0;};
struct XEconomyView {
    bool open=false;enum class Page {Market,Holdings,Mining,Services,Contracts} page=Page::Market;
    XPlace place;size_t good=0,scroll=0,asset=0,deposit=0;int64_t quantity=1;unsigned buildLevel=1;
    std::string message="F4 TRADE / DOCK AT A STATION TO BUY OR SELL";
};
struct CockpitOverlay;
struct XEconomyLayout {
    double width;std::array<XalariaMapRect,5> tabs;
    XalariaMapRect rows,details,buy,sell,travel,venue,minus,plus,action,secondary,tertiary;
    explicit XEconomyLayout(double aspect);
};
class XalariaEconomy {
  public:
    explicit XalariaEconomy(const Universe&);
    XEconomyView view;
    int64_t credits() const {return credits_;}
    double fuel() const {return fuel_;}
    double fuelCapacity() const {return fuelCapacity_;}
    double energy() const {return energy_;}
    unsigned technology() const;
    unsigned culture() const;
    unsigned holdCapacity() const {return capacity_;}
    int64_t cargoUnits() const;
    double cargoKg() const {return double(cargoUnits())*10;}
    const XStock& cargo() const {return cargo_;}
    int64_t day() const {return int64_t(seconds_/86400);}
    double economicSeconds() const {return seconds_;}
    const std::vector<XAsset>& assets() const {return assets_;}
    const std::vector<XArtwork>& artwork() const {return artwork_;}
    const std::vector<XContract>& contracts() const {return contracts_;}
    const std::map<std::tuple<uint32_t,uint64_t,unsigned>,XDeposit>& deposits() const {return deposits_;}
    const std::optional<XPlace>& dockedPlace() const {return docked_;}
    bool landed() const {return landed_;}
    bool nearOrbit() const {return nearOrbit_;}
    uint32_t currentSystem() const {return currentSystem_;}
    std::optional<uint64_t> currentWorld() const {return currentWorld_;}
    unsigned aesthetics(uint32_t,uint64_t) const;
    int64_t goodwill() const {return tradeSpend_/400000;}
    int64_t goodwillDue() const {return std::max<int64_t>(0,goodwill()-rebatePoints_)*10000;}
    std::vector<XPlace> venues(uint32_t,uint64_t) const;
    std::string placeName(const XPlace&) const;
    XFaction faction(const XPlace&) const;
    bool canTrade(const XPlace&) const;
    void show(uint32_t,uint64_t);
    void cycleVenue();
    XQuote quote(const XPlace&,XGood,int64_t units,bool buy) const;
    int64_t stock(const XPlace&,XGood) const;
    bool buy(XGood,int64_t);
    bool sell(XGood,int64_t);
    bool refuel();
    bool buyTool(bool precision=false);
    bool buyFillingTool();
    bool upgradeHold();
    bool buyArtwork();
    bool studyCulture();
    bool seedAsset(XAssetKind,unsigned level=1);
    bool feedAsset(uint32_t id);
    bool collect(uint32_t id);
    bool guard(uint32_t id);
    bool flag(uint32_t id);
    bool buildHall(uint32_t id,bool gallery);
    bool donateArtwork(uint32_t id);
    bool buildFuelStation(uint32_t id);
    bool store(uint32_t id,XGood,int64_t units,bool retrieve);
    bool survey();
    bool mine(unsigned deposit,bool fill=false);
    XContract offer(const XPlace&) const;
    bool acceptContract();
    bool deliverContracts();
    std::vector<XOpportunity> opportunities(XGood,int64_t units) const;
    static int64_t stationNanomites(unsigned);
    static int64_t stationIncome(unsigned);
    static unsigned constructionChance(unsigned level,unsigned technology,bool vault=false);
    int64_t dailyIncome(const XAsset&) const;
    std::vector<ActivityBody> orbitalBodies(const Node&,double seconds) const;
    ActivityBody portBody(const XPlace&,double seconds=0) const;
    bool travelToPort(const XPlace&,Navigation&,Pilot&);
    void sync(const Navigation&,const Pilot&);
    void advance(double realSeconds);
    void useManualFuel(double realSeconds,double thrust,bool warp);
    bool beginVoyage(bool interstellar);
    bool payWormholeFuel();
    void voyageProgress(double);
    void endVoyage(bool completed=false);
    void catchUp(int64_t unixTime);
    std::string serialize(int64_t unixTime=0) const;
    bool restore(std::string_view);
  private:
    const Universe& universe_;const XalariaCatalog& catalog_;
    int64_t credits_=2000000,tradeSpend_=0,rebatePoints_=0,technologyXP_=0,cultureXP_=0;
    XStock cargo_{};unsigned capacity_=30,shipTier_=0;double fuel_=80,fuelCapacity_=80,energy_=20;
    bool tool_=false,precision_=false,fillingTool_=false;double seconds_=0,miningTimer_=0,voyageBudget_=0,voyageUsed_=0;
    int64_t wallTime_=0;uint64_t randomCounter_=0,nextArt_=1,nextAsset_=1;
    uint32_t currentSystem_=14940;std::optional<uint64_t> currentWorld_;bool landed_=false,nearOrbit_=false;
    std::optional<XPlace> docked_;
    mutable std::map<XPlace,XMarket> markets_;
    std::vector<XAsset> assets_;std::vector<XArtwork> artwork_;std::vector<XContract> contracts_;
    std::map<std::tuple<uint32_t,uint64_t,unsigned>,XDeposit> deposits_;
    std::map<std::pair<uint32_t,uint64_t>,unsigned> known_;
    std::set<std::pair<uint32_t,uint64_t>> surveyed_,visited_;
    XMarket& market(const XPlace&) const;
    bool validPlace(const XPlace&) const;
    int64_t midpoint(const XPlace&,XGood,int64_t stock) const;
    void spent(int64_t);
    double random();
    XAsset* asset(uint32_t);
    bool atAsset(const XAsset&,bool requireDock=false) const;
    void settleDay(int64_t);
    bool failure(std::string);
};
CockpitOverlay xalariaEconomyOverlay(const XalariaEconomy&,double aspect);
void xalariaEconomyHud(CockpitOverlay&,const XalariaEconomy&,double aspect,bool map=false);
} // namespace infinity

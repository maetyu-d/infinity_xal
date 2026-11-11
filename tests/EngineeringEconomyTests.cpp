#define GLM_FORCE_INLINE
#include "infinity/Economy.hpp"
#include <iostream>
#include <sstream>
using namespace infinity;
namespace {
unsigned checks = 0;
void check(bool x, const char* why) {
    ++checks;
    if (!x)
        throw std::runtime_error(why);
}
} // namespace
int main() {
    try {
        auto net = std::make_shared<EconomicNetwork>();
        net->system = Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9);
        net->fingerprint = 630063;
        for (unsigned i = 0; i < 2; ++i) {
            EconomicSite d;
            d.id = {net->system.child(Kind::Planet, i), i};
            d.name = i ? "DAMAGED COLONY" : "SUPPLY PORT";
            d.orbital = !i;
            d.population = i ? 1000 : 0;
            d.sealed = true;
            d.workers = 20;
            d.power = 1;
            d.capacityKg = 1000000;
            d.initial.fill(2000);
            net->sites.push_back(d);
        }
        Economy e(net);
        EconomyPlayer p;
        p.credits = 10000000;
        uint64_t sequence = 0;
        auto issue = [&](EconomicAction action, unsigned site = 0, uint64_t id = 0) {
            EconomicCommand c;
            c.action = action;
            c.port = net->sites[site].id;
            c.contract = id;
            c.sequence = ++sequence;
            c.limitCents = 1000000000;
            return e.execute(c, p);
        };
        EconomicCommand buy;
        buy.sequence = ++sequence;
        buy.action = EconomicAction::Buy;
        buy.port = net->sites[0].id;
        buy.good = Good::Propellant;
        buy.kg = 40;
        buy.limitCents = 1000000000;
        check(e.execute(buy, p).starts_with("BOUGHT"), "Cannot source real reactor fuel");
        const auto money = e.moneyTotal() + p.credits;
        EconomicCommand fill;
        fill.sequence = ++sequence;
        fill.action = EconomicAction::SupplyEngineering;
        fill.plantSupply = EngineeringSupply::Fuel;
        fill.kg = 20;
        const auto inventory = e.inventoryTotal(), delta = e.externalGoodsDelta();
        const auto cargo = p.cargo;
        check(e.execute(fill, p).starts_with("ENGINEERING LOADED") &&
                  p.cargo[size_t(Good::Propellant)] == 20 && p.engineering.kg[0] == 20,
              "Tank transfer is not backed by cargo");
        check(e.externalGoodsDelta()[size_t(Good::Propellant)] == delta[size_t(Good::Propellant)] &&
                  e.inventoryTotal() == inventory,
              "Tank transfer evades commodity conservation");
        const auto after = p.serialize();
        check(e.execute(fill, p) == "ORDER ALREADY PROCESSED" && p.serialize() == after,
              "Replayed supply creates a second tank receipt");
        ShipPlant plant;
        plant.stores[0] = 0;
        plant.reconcile(p.engineering);
        check(plant.stores[0] == 20, "Player engineering did not receive its paid mass");
        plant.reconcile(p.engineering);
        check(plant.stores[0] == 20, "Reconciliation duplicates fuel");
        EconomyPlayer restoredWallet;
        check(restoredWallet.restore(p.serialize()) && restoredWallet.serialize() == p.serialize(),
              "Engineering wallet receipt lost after restart");
        auto legacy = p.serialize();
        legacy.replace(legacy.find("WALLET 11"), 9, "WALLET 9");
        for (unsigned i = 0; i < 6; ++i)
            legacy.resize(legacy.rfind(' '));
        check(restoredWallet.restore(legacy) && restoredWallet.engineering.kg[0] == 0,
              "Legacy wallet invents paid fuel");
        auto state = e.serialize();
        const auto first = state.find('\n'), second = state.find('\n', first + 1),
                   third = state.find('\n', second + 1);
        auto row = state.substr(second + 1, third - second - 1);
        std::istringstream input(row);
        int64_t cash, house;
        unsigned condition, health;
        uint64_t outage;
        input >> cash >> house >> condition >> health >> outage;
        std::string rest;
        std::getline(input, rest);
        state.replace(second + 1, third - second - 1,
                      std::to_string(cash) + " " + std::to_string(house) + " 200 1000 200" + rest);
        check(e.restore(state), "Cannot load an actual severe plant outage");
        e.advanceTo(12);
        auto found = std::find_if(e.recoveries().begin(), e.recoveries().end(),
                                  [](const auto& j) { return bool(j.engineering); });
        check(found != e.recoveries().end(), "Severe outage posts no shared power/cooling recovery work");
        const auto job = *found;
        check(job.required[size_t(Good::Water)] == 20 && job.required[size_t(Good::Propellant)] == 20 &&
                  job.required[size_t(Good::Oxygen)] == 10 && job.escrow > 0,
              "Engineering kit is not derived from the failed plant");
        check(issue(EconomicAction::AcceptRecovery, 1, job.id).starts_with("RECOVERY ACCEPTED"),
              "Cannot accept engineering work");
        for (size_t i = 0; i < goodCount; ++i) {
            const auto missing = std::max<int64_t>(0, job.required[i] - p.availableCargo(Good(i)));
            if (!missing)
                continue;
            buy.sequence = ++sequence;
            buy.good = Good(i);
            buy.kg = missing;
            check(e.execute(buy, p).starts_with("BOUGHT"), "Cannot buy required engineering kit");
        }
        check(issue(EconomicAction::DeliverRecovery, 1, job.id).starts_with("RECOVERY DELIVERED"),
              "Cannot deliver funded engineering kit");
        e.advanceTo(16);
        found = std::find_if(e.recoveries().begin(), e.recoveries().end(),
                             [&](const auto& j) { return j.id == job.id; });
        check(found->phase == RecoveryPhase::Working && found->engineering->heat < .95 &&
                  found->engineering->stores[0] > 0,
              "Local engineers do not operate the shared plant model");
        Economy loaded(net);
        check(loaded.restore(e.serialize()) && loaded.serialize() == e.serialize(),
              "Working power/cooling job lost on restart");
        e.advanceTo(90);
        loaded.advanceTo(90);
        check(loaded.serialize() == e.serialize(), "Recovery outcome changes across save/reload");
        found = std::find_if(e.recoveries().begin(), e.recoveries().end(),
                             [&](const auto& j) { return j.id == job.id; });
        check(found != e.recoveries().end() && found->phase == RecoveryPhase::Complete &&
                  found->engineering->heat < .5 && e.sites()[1].outageUntil <= e.tick(),
              "Delivered fuel and coolant do not restore actual service");
        check(e.moneyTotal() + p.credits == money, "Engineering work creates or loses payment money");
        const auto total = e.inventoryTotal(), external = e.externalGoodsDelta(), initial = e.initialTotal();
        for (size_t i = 0; i < goodCount; ++i)
            check(total[i] == initial[i] + external[i],
                  "Engineering work violates finite commodity accounting");
        auto naturalNet=std::make_shared<EconomicNetwork>(*net);naturalNet->fingerprint=630064;
        naturalNet->sites[1].initial[size_t(Good::Machinery)]=0;naturalNet->sites[1].initial[size_t(Good::Filters)]=0;
        Economy natural(naturalNet);auto worn=natural.serialize();const auto l0=worn.find('\n'),l1=worn.find('\n',l0+1),l2=worn.find('\n',l1+1);
        std::istringstream target(worn.substr(l1+1,l2-l1-1));target>>cash>>house>>condition>>health>>outage;std::getline(target,rest);
        worn.replace(l1+1,l2-l1-1,std::to_string(cash)+" "+std::to_string(house)+" 310 1000 0"+rest);
        check(natural.restore(worn),"Natural-wear fixture invalid");
        while(natural.tick()<320)natural.advanceTo(std::min<uint64_t>(320,natural.tick()+64));
        check(natural.sites()[1].outageUntil==0&&std::any_of(natural.recoveries().begin(),natural.recoveries().end(),[](const auto& j){return bool(j.engineering);}),
            "Engineering jobs require a scripted outage instead of real wear and shortages");
        std::cout << "PASS: " << checks
                  << " cargo-backed supplies, replay, wallet migration, funded outages, shared cooling, "
                     "recovery and conservation checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}

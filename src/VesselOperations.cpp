#define GLM_FORCE_INLINE
#include "infinity/VesselSystems.hpp"
#include "infinity/NpcCombat.hpp"
#include "infinity/SaveTag.hpp"
#include <algorithm>
#include <iomanip>
#include <queue>
#include <set>
#include <sstream>
namespace infinity {
bool ShipCrew::externalWork(uint64_t id, double seconds, double injury) {
    if (!std::isfinite(seconds) || seconds <= 0 || seconds > 1 || !std::isfinite(injury) || injury < 0) return false;
    for (auto& m : members_) if (m.id == id && m.health > 0) {
        m.fatigue = std::min(100., m.fatigue + seconds * .04);
        m.health = std::max(0., m.health - std::min(injury, 5.));
        m.experience[3] = std::min(1000000., m.experience[3] + seconds * .015);
        return m.health >= 25;
    }
    return false;
}
std::string_view vesselTaskName(VesselTask t) {
    constexpr std::array<std::string_view, 5> names{"NONE", "FIELD REPAIR", "EVACUATE", "RECOVER EQUIPMENT", "RETURN TO SHIP"};
    return unsigned(t) < names.size() ? names[unsigned(t)] : "NONE";
}
void VesselOperations::observe(const Universe& u, const NpcPlayer& ship, const std::optional<ActivityBody>& body,
        const VesselEngineering*, double dt, bool focus, double navigator, std::span<const ActivityBody> nearby,
        const Geometry* geometry) {
    if (!std::isfinite(dt) || dt <= 0 || dt > 1 || !std::isfinite(ship.seconds) ||
        ship.seconds<0 || ship.seconds>1e12 || !std::isfinite(navigator) ||
        !std::isfinite(glm::length(ship.position)) || !std::isfinite(glm::length(ship.velocity)) ||
        !std::isfinite(glm::length(ship.forward))) return;
    if(ship.seconds < clock_)for(auto& scan:scans_)scan.at=std::max(0.,scan.at+ship.seconds-clock_);
    clock_ = ship.seconds; accessible_ = false; range_ = -1;
    selected_ = body ? std::optional(body->id) : std::nullopt;
    if (!body || body->kind == ActivityKind::Harbour || body->kind == ActivityKind::SurfacePort || body->kind == ActivityKind::Debris) return;
    auto origin = ship.position, velocity = ship.velocity, forward = ship.forward;
    if (ship.reference != body->id.home) {
        origin = u.pointRelative(ship.reference, ship.position / std::pow(10., u.describe(ship.reference).frame.logRadius), body->id.home, 0);
        const auto rot = u.relative(ship.reference, body->id.home, 0).orientation;
        velocity = rot * velocity; forward = rot * forward;
    }
    const auto delta = body->position - origin;
    range_ = glm::length(delta); relativeSpeed_ = glm::length(body->velocity - velocity);
    if (ship.field || ship.landed || ship.size < .99 || ship.hull <= 0) { status_ = "FULL SIZE / DISENGAGE DRIVE"; return; }
    bool clear = false;
    if ((focus || target_ || glm::dot(forward, delta / std::max(1e-9, range_)) > .99) && range_ < 8000) {
        WeaponRay ray; ray.reference = body->id.home; ray.origin = origin; ray.direction = delta / std::max(1e-9, range_);
        const auto hit = traceWeapon(u, ray, range_ + body->radius + 1, nearby.empty() ? std::span(&*body, 1) : nearby, geometry);
        clear = hit.body && hit.body->id == body->id;
    }
    accessible_ = clear && range_ <= body->radius + 120 && relativeSpeed_ <= 3 &&
                  (!body->hostile || body->disabled || body->systems[size_t(ShipSystem::Engines)] <= 0);
    status_ = accessible_ ? "ACCESS CLEAR / READY TO TRANSFER" : relativeSpeed_ > 3 ? "MATCH VELOCITY BELOW 3 M/S" : "APPROACH WITHIN 120 M OF HULL";
    auto it = std::find_if(scans_.begin(), scans_.end(), [&](const auto& s) { return s.id == body->id; });
    if (it != scans_.end()) {
        bool changed = clock_ < it->at || clock_ - it->at > 45;
        for (size_t i = 0; i < shipSystemCount; ++i) changed |= std::abs(it->systems[i] - body->systems[i]) > 5;
        if (changed && it->progress >= 1) it->progress = .55;
    }
    if (!clear || range_ > (focus ? 8000 : 4000) || glm::dot(forward, delta / std::max(1e-9, range_)) < .985) return;
    if (it == scans_.end()) {
        if (scans_.size() >= 64) scans_.erase(std::min_element(scans_.begin(), scans_.end(), [](const auto& a, const auto& b) { return a.at < b.at; }));
        scans_.push_back({body->id}); it = std::prev(scans_.end());
    }
    it->progress = std::min(1., it->progress + dt * (focus ? .22 : .025) * (1 + .3 * std::clamp(navigator, 0., 1.5)));
    if (it->progress >= 1) { it->at = clock_; it->systems = body->systems; }
}
bool VesselOperations::confirmed(const ActivityId& id, double seconds) const {
    for (const auto& s : scans_) if (s.id == id) return s.progress >= 1 && seconds >= s.at && seconds - s.at <= 45;
    return false;
}
std::string VesselOperations::begin(VesselTask task, const ActivityBody& body, const VesselEngineering* engineering, ShipCrew& crew) {
    if (target_) return "PARTY ALREADY DEPLOYED / 4 RECALL";
    if (task < VesselTask::Repair || task > VesselTask::Salvage) return "SELECT A FIELD TASK";
    if (!confirmed(body.id, clock_)) return "HOLD F12 / CONFIRM INTERNAL SCAN FIRST";
    if (!selected_ || *selected_ != body.id || !accessible_) return status_;
    if(body.status.starts_with("ION OFFLINE"))return "TEMPORARY ION OUTAGE / HOLD 0 TO SECURE FIRST";
    if (body.systems[size_t(ShipSystem::Engines)] > 0 && !(engineering && engineering->surrendered) && !body.status.starts_with("FIELD REPAIRED")) return "VESSEL MUST BE ADRIFT OR SURRENDERED";
    if (task == VesselTask::Repair && body.hull <= 0) return "HULL LOST / EVACUATION AND SALVAGE ONLY";
    size_t best = crew.members().size(); double fit = -1;
    for (size_t i = 0; i < crew.members().size(); ++i) {
        const auto& m = crew.members()[i];
        if (m.health < 60 || m.fatigue > 80 || m.travel > 0 || m.duty == CrewStation::Bridge || m.id == crew.reliefHelmId()) continue;
        const auto score = crew.stationFit(i, task == VesselTask::Evacuate ? CrewStation::Medbay : CrewStation::Engineering);
        if (score > fit) { fit = score; best = i; }
    }
    if (best == crew.members().size()) return "NO READY AWAY CREW / F9 CREW";
    party_ = crew.members()[best].id; partyName_ = crew.members()[best].name;
    crew.setAway(party_); target_ = body.id; task_ = task; room_ = 0;
    destination_ = task == VesselTask::Repair ? 3 : task == VesselTask::Evacuate ? 4 : 5;
    work_ = leg_ = returning_ = 0; waiting_ = issued_ = false;
    return status_ = "PARTY TRANSFERRING / KEEP POSITION";
}
void VesselOperations::returnParty(ShipCrew& crew) {
    crew.setAway(0); target_.reset(); party_ = 0; task_ = VesselTask::None;
    waiting_ = issued_ = false; work_ = leg_ = returning_ = 0;
}
void VesselOperations::recall() { if (target_) { task_ = VesselTask::Return; waiting_ = issued_ = false; returning_ = 0; } }
void VesselOperations::acknowledge(bool success) {
    if (target_ && waiting_) { status_ = success ? "TASK COMPLETE / PARTY RETURNING" : "TRANSFER DECLINED / PARTY RETURNING"; task_ = VesselTask::Return; waiting_ = issued_ = false; }
}
std::optional<VesselTaskResult> VesselOperations::advance(const Universe&, const NpcPlayer& ship,
        const std::optional<ActivityBody>& body, const VesselEngineering* engineering, ShipCrew& crew, double dt,
        std::span<const ActivityBody>, const Geometry*) {
    if (!target_) { crew.setAway(0); return {}; }
    if (!std::isfinite(dt) || dt <= 0 || dt > 1) return {};
    const auto person = std::find_if(crew.members().begin(), crew.members().end(), [&](const auto& m) { return m.id == party_; });
    if (person == crew.members().end() || person->health <= 0) { returnParty(crew); status_ = "AWAY PARTY LOST / MISSION CLOSED"; return {}; }
    crew.setAway(party_);
    if (!body || body->id != *target_ || !selected_ || *selected_ != *target_) {
        if (!waiting_) recall();
        status_ = "PARTY WAITING / RESELECT CONTACT / MATCH SPEED"; return {};
    }
    if(!accessible_&&!waiting_)recall();
    if (waiting_) {
        status_ = "TRANSFER PENDING / PARTY HOLDING";
        issued_ = true; return VesselTaskResult{*target_, task_, party_, ship.seconds};
    }
    const auto deck = vesselProfile(*body);
    const unsigned goal=task_==VesselTask::Return?0:destination_;
    if (room_ != goal) {
        std::array<unsigned, 7> previous; previous.fill(7); previous[room_] = room_;
        std::array<unsigned,7> queue{};unsigned head=0,tail=1;queue[0]=room_;
        while (head<tail) {
            auto at = queue[head++];
            for (auto [a,b] : deck.doors) {
                unsigned next = a == at ? b : b == at ? a : 7;
                const bool unsafe=engineering&&next<7&&(engineering->rooms[next].fire>65||engineering->rooms[next].oxygen<5);
                if (next < 7 && previous[next] == 7 && !unsafe) { previous[next] = at; queue[tail++]=next; }
            }
        }
        if (previous[goal] == 7) { recall(); status_ = "NO SAFE DECK ROUTE / CLEAR FIRE AND AIR FAULTS"; return {}; }
        unsigned next = goal; while (previous[next] != room_) next = previous[next];
        leg_ += dt;
        if (leg_ >= 1.5 + glm::length(deck.rooms[room_] - deck.rooms[next])) { room_ = next; leg_ = 0; }
        status_ = std::string(task_==VesselTask::Return?"RETURN ROUTE / ":"PARTY MOVING / ") + std::string(crewStationName(CrewStation(room_)));
    } else {
        if(task_==VesselTask::Return){
            if(!accessible_){status_="PARTY AT AIRLOCK / APPROACH AND MATCH SPEED";return {};}
            returning_+=dt;status_="PARTY TRANSFERRING / "+std::to_string(int(std::max(0.,6-returning_)))+"S";
            if(returning_>=6){returnParty(crew);status_="PARTY ABOARD / DUTIES RESTORED";}return {};
        }
        double hazard = 0;
        if (engineering) {
            const auto& r = engineering->rooms[room_];
            hazard = r.fire * .006 + std::max(0., 18 - r.oxygen) * .02;
            if (r.fire > 65 || r.oxygen < 5) { recall(); status_ = "DECK UNSAFE / PARTY WITHDRAWING"; return {}; }
        }
        if (!crew.externalWork(party_, dt, hazard * dt)) { recall(); status_ = "CREW INJURED / RETURNING"; return {}; }
        const double skill = crew.stationFit(size_t(person - crew.members().begin()), task_ == VesselTask::Evacuate ? CrewStation::Medbay : CrewStation::Engineering);
        work_ = std::min(12., work_ + dt * (.7 + skill * .5));
        status_ = std::string(vesselTaskName(task_)) + " / " + std::to_string(int(work_ / 12 * 100)) + "%";
        if (work_ >= 12) { waiting_ = issued_ = true; return VesselTaskResult{*target_, task_, party_, ship.seconds}; }
    }
    return {};
}
std::optional<ActivityId> VesselOperations::target() const { return target_; }
VesselView VesselOperations::view(const std::optional<ActivityBody>& body, const VesselEngineering* engineering, bool open) const {
    VesselView v; v.open = open; v.status = accessible_?"ACCESS CLEAR / HOLD POSITION":status_; v.taskStatus = status_; v.accessible = accessible_;
    v.deployed = target_&&body&&*target_==body->id; v.party = partyName_; v.partyRoom = room_; v.destination = destination_; v.progress = work_ / 12;
    if (!body) { v.name = "SELECT A SHIP / K CONTACTS"; return v; }
    v.name = body->name; v.identity = body->id.str(); v.deck = vesselProfile(*body);
    for (const auto& s : scans_) if (s.id == body->id) { v.scan = s.progress; v.confirmed = confirmed(body->id, clock_); v.stale = !v.confirmed && s.at > 0; v.systems = s.systems; }
    v.scanStatus = v.confirmed ? "INTERNAL LOCK / CONFIRMED" : v.stale ? "READING STALE / HOLD F12" : "HOLD F12 / FOCUS INTERNAL SCAN";
    if (v.confirmed && engineering) {
        const auto& p = engineering->plant.readout(); v.heat = p.heat; v.power = p.load; v.fuel = p.stores[0]; v.air = p.stores[2]; v.rooms = engineering->rooms;
    }
    v.supplies = "REPAIR: 2 ALLOY / 1 ELEC / 2 FUEL / 2 WATER / 1 O2";
    if(target_&&task_==VesselTask::Evacuate)v.supplies="EVACUATION / SPARE CREW BERTH REQUIRED / F2 RESCUE OFFICE";
    if(target_&&task_==VesselTask::Salvage)v.supplies="FINITE EQUIPMENT / FREE CARGO CAPACITY REQUIRED";
    return v;
}
std::string VesselOperations::serialize(uint64_t seed) const {
    std::ostringstream o; o << std::setprecision(17) << "INFINITY_VESSELS 1 " << seed << ' ' << clock_ << ' ' << scans_.size();
    for (const auto& s : scans_) { o << ' ' << s.id.str() << ' ' << s.progress << ' ' << s.at; for (auto x : s.systems) o << ' ' << x; }
    o << ' ' << bool(target_);
    if (target_) o << ' ' << target_->str() << ' ' << unsigned(task_) << ' ' << party_ << ' ' << std::quoted(partyName_) << ' ' << room_ << ' ' << destination_ << ' ' << work_ << ' ' << leg_ << ' ' << returning_ << ' ' << waiting_;
    return o.str();
}
bool VesselOperations::restore(std::string_view text, const Universe& u) {
    try {
        VesselOperations v; std::istringstream in{std::string(text)}; std::string tag, id; uint64_t seed; unsigned version, count, active;
        if (!(in >> tag >> version >> seed >> v.clock_ >> count) || !matchesSaveTag(tag, "INFINITY_VESSELS") || version != 1 || seed != u.seed() || !std::isfinite(v.clock_) || v.clock_ < 0 || v.clock_ > 1e12 || count > 64) return false;
        std::set<ActivityId> ids;
        auto number = [&](double& x, double max) { return bool(in >> x) && std::isfinite(x) && x >= 0 && x <= max; };
        auto valid = [&](const ActivityId& a) { auto b = sampleCombatActivity(u, a, 0); return b.kind == ActivityKind::Freighter || b.kind == ActivityKind::Ferry || b.kind == ActivityKind::Survey || b.kind == ActivityKind::Patrol || b.kind == ActivityKind::Raider; };
        for (unsigned i = 0; i < count; ++i) {
            Scan s; if (!(in >> id) || !number(s.progress, 1) || !number(s.at, v.clock_)) return false;
            s.id = ActivityId::parse(id); if (!valid(s.id) || !ids.insert(s.id).second) return false;
            for (auto& x : s.systems) if (!number(x, 100)) return false;
            v.scans_.push_back(s);
        }
        if (!(in >> active) || active > 1) return false;
        if (active) {
            unsigned task, waiting;
            if (!(in >> id >> task >> v.party_ >> std::quoted(v.partyName_) >> v.room_ >> v.destination_) || task < 1 || task > 4 || !v.party_ || v.partyName_.size() > 80 || v.room_ >= 7 || v.destination_ >= 7 || !number(v.work_, 12) || !number(v.leg_, 20) || !number(v.returning_, 6) || !(in >> waiting) || waiting > 1) return false;
            v.target_ = ActivityId::parse(id); if (!valid(*v.target_)) return false;
            v.task_ = VesselTask(task); v.waiting_ = waiting;
        }
        in >> std::ws; if (!in.eof()) return false;
        *this = std::move(v); return true;
    } catch (...) { return false; }
}
} // namespace infinity

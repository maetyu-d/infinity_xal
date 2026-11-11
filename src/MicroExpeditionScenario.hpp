#pragma once
#include "infinity/Economy.hpp"
#include "infinity/Piloting.hpp"
#include <iostream>
#include <sstream>

namespace infinity {
// Shared headless/native proof. Only setup places the ship. Every subsequent
// displacement/rotation/resize uses the same Pilot and manual helm as gameplay.
class MicroExpeditionScenario {
    const Universe& u_;
    Navigation& nav_;
    Pilot& pilot_;
    Node planet_, rock_, grain_, lattice_;
    ProspectSite site_;
    glm::dvec3 entry_{};
    Address holdFrame_;
    glm::dvec3 holdMetres_{};
    std::shared_ptr<EconomicNetwork> network_;
    std::unique_ptr<Economy> economy_;
    std::shared_ptr<MiningProbeState> excavations_;
    EconomyPlayer player_;
    ExplorationLedger ledger_;
    SurveyController scanner_;
    MiningController miner_;
    unsigned stage_ = 0, age_ = 0, dwell_ = 0;
    uint64_t sequence_ = 0;
    double seconds_ = 100;
    double distance_ = 0;
    uint64_t rockIdentity_ = 0, grainIdentity_ = 0, latticeIdentity_ = 0;
    std::string feedback_;
    static void expect(bool b, const char* m) {
        if (!b)
            throw std::runtime_error(m);
    }
    SurveyObserver observer() const {
        SurveyObserver o;
        o.reference = nav_.anchor;
        o.position = nav_.cameraPosition() * std::pow(10., nav_.logDistance);
        o.attitude = nav_.orientation;
        o.velocity = pilot_.ship().velocity;
        o.size = pilot_.sizeRatio();
        o.hull = pilot_.ship().hull;
        o.seconds = seconds_;
        return o;
    }
    std::string issue(EconomicAction action, const Address& target) {
        EconomicCommand c;
        c.sequence = ++sequence_;
        c.action = action;
        c.surveyTarget = target;
        c.observer = observer();
        c.workSeconds = action == EconomicAction::ExtractProspect ? 6 : 8;
        c.combatPresent = true;
        c.weaponMount = 1;
        return ledger_.execute(u_, *economy_, player_, c);
    }
    void hold() {
        holdFrame_ = nav_.anchor;
        holdMetres_ = nav_.cameraPosition() * std::pow(10., nav_.logDistance);
        pilot_.brake();
    }
    void next() {
        std::cout << "MICRO_EXPEDITION stage=" << stage_ << " complete / " << label()
                  << " / anchor=" << nav_.anchor.str() << " / hull=" << pilot_.ship().hull
                  << " / size=" << pilot_.instruments().hullMetres
                  << " / flight_seconds=" << pilot_.ship().flightSeconds << std::endl;
        ++stage_;
        age_ = dwell_ = 0;
        scanner_.reset();
        miner_.reset();
        hold();
    }
    double fly(const Address& frame, glm::dvec3 metres, const Address& look, double tolerance, double dt,
               const std::shared_ptr<const Geometry>& visible) {
        if (look != nav_.anchor && (!nav_.hasExplicitSelection() || !nav_.selected || *nav_.selected != look))
            nav_.select(look);
        const auto n = u_.describe(frame);
        const auto goal =
            u_.pointRelative(frame, metres / std::pow(10., n.frame.logRadius), nav_.anchor, nav_.logDistance);
        const auto delta = (goal - nav_.cameraPosition()) * std::pow(10., nav_.logDistance);
        const double distance = glm::length(delta);
        distance_ = distance;
        const auto target = u_.relative(look, nav_.anchor, nav_.logDistance).position - nav_.cameraPosition();
        const auto forward = distance > tolerance * 2      ? glm::normalize(delta)
                             : glm::length(target) > 1e-20 ? glm::normalize(target)
                                                           : nav_.orientation * glm::dvec3(0, 0, -1);
        auto up = nav_.orientation * glm::dvec3(0, 1, 0);
        if (std::abs(glm::dot(forward, up)) > .98)
            up = glm::normalize(glm::cross(forward, glm::dvec3(1, .1, .03)));
        const auto course = glm::quatLookAtRH(forward, up);
        const double limit = std::max(1e-12, pilot_.instruments().driveSpeed);
        const double cap = limit * .20;
        const auto velocity =
            distance > tolerance ? delta * (std::min(cap, distance * .65) / distance) : glm::dvec3(0);
        auto turn = glm::inverse(nav_.orientation) * course;
        if (turn.w < 0)
            turn = -turn;
        const auto spin = glm::dvec3(turn.x, turn.y, turn.z) * 4.;
        const auto local = glm::inverse(nav_.orientation) * velocity;
        HelmInput input;
        input.pitch = std::clamp(spin.x / 1.2, -1., 1.);
        input.yaw = std::clamp(-spin.y / 1.1, -1., 1.);
        input.roll = std::clamp(-spin.z / 1.8, -1., 1.);
        input.strafe = std::clamp(local.x / (limit * .28), -1., 1.);
        input.lift = std::clamp(local.y / (limit * .30), -1., 1.);
        const double throttle = std::clamp(-local.z / limit, 0., 1.);
        input.throttle = std::clamp((throttle - pilot_.ship().throttle) / (.36 * dt), -1., 1.);
        pilot_.update(input, dt, visible);
        return distance;
    }
    bool at(const Address& frame, glm::dvec3 metres, const Address& look, double tolerance, double dt,
            const std::shared_ptr<const Geometry>& visible) {
        const double distance = fly(frame, metres, look, tolerance, dt, visible);
        const bool settled = distance <= tolerance &&
                             glm::length(pilot_.ship().velocity) < std::max(5e-13, pilot_.sizeRatio() * .5);
        dwell_ = settled ? dwell_ + 1 : 0;
        return dwell_ >= 4;
    }
    void restart() {
        const auto camera = nav_.serialize(), ship = pilot_.serialize(), ledger = ledger_.serialize();
        const auto wallet = player_.serialize(), market = economy_->serialize();
        expect(nav_.restore(camera) && pilot_.restore(ship) && ledger_.restore(ledger, u_),
               "Live micro expedition could not reload camera, moving hull and marks");
        expect(player_.restore(wallet) && economy_->restore(market) && player_.serialize() == wallet &&
                   economy_->serialize() == market,
               "Live micro expedition changed cargo, wallet or economic stock on reload");
        expect(nav_.serialize() == camera && pilot_.serialize() == ship && ledger_.serialize() == ledger,
               "Reload changed live micro expedition state");
        const auto boreSave = serializeProbes(*excavations_);
        MiningProbeState restored;
        expect(restoreProbes(restored, boreSave, u_) && serializeProbes(restored) == boreSave,
               "Procedurally derived departure cavern changed after restart");
        *excavations_ = std::move(restored);
        pilot_.setExcavations(excavations_, seconds_);
        expect(descriptorFingerprint(u_.describe(rock_.address)) == rockIdentity_ &&
                   descriptorFingerprint(u_.describe(grain_.address)) == grainIdentity_ &&
                   descriptorFingerprint(u_.describe(lattice_.address)) == latticeIdentity_,
               "Flight or restart changed procedural source identity");
        std::cout << "MICRO_RESTART exact camera / ship / ledger / original identities" << std::endl;
    }

  public:
    MicroExpeditionScenario(const Universe& u, Navigation& nav, Pilot& pilot)
        : u_(u), nav_(nav), pilot_(pilot) {
        planet_ = u_.describe(Address{}.child(Kind::Galaxy, 0).child(Kind::System, 9).child(Kind::Planet, 0));
        const auto sites = generateProspects(u_, planet_, tileAt(glm::normalize(glm::dvec3(.37, .74, 1)), 7));
        expect(!sites.empty(), "No procedural seam expedition clearing");
        site_ = sites.front();
        rock_ = u_.describe(site_.rocks.front().address);
        grain_ = u_.describe(u_.children(rock_, 1).front());
        lattice_ = u_.describe(u_.children(grain_, 1).front());
        rockIdentity_ = descriptorFingerprint(rock_);
        grainIdentity_ = descriptorFingerprint(grain_);
        latticeIdentity_ = descriptorFingerprint(lattice_);
        entry_ = site_.metres + site_.direction * 16.;
        WeaponRay drill;
        drill.reference = planet_.address;
        drill.origin = entry_;
        drill.direction = -site_.direction;
        drill.seconds = 100;
        auto plan = planMiningProbe(u_, planet_.address, drill, 2, 61026102);
        expect(bool(plan.hole), "No actual solid-terrain departure cavern");
        excavations_ = std::make_shared<MiningProbeState>();
        excavations_->holes.push_back(*plan.hole);
        const auto& bore = excavations_->holes.front();
        seconds_ = bore.blastTime() + 30;
        const auto cavernEye = bore.entry - site_.direction * bore.finalDepth();
        const auto q =
            glm::quatLookAtRH(site_.direction, tangentFrame(site_.direction) * glm::dvec3(1, 0, 0));
        const auto offset = cavernEye / 100. - q * glm::dvec3(0, 0, 1);
        std::ostringstream camera;
        camera.precision(17);
        camera << planet_.address.str() << " 2 " << q.w << ' ' << q.x << ' ' << q.y << ' ' << q.z << ' '
               << offset.x << ' ' << offset.y << ' ' << offset.z;
        expect(nav_.restore(camera.str()), "Expedition departure camera invalid");
        pilot_.setExcavations(excavations_, seconds_);
        pilot_.resetMotion();
        hold();
        network_ = std::make_shared<EconomicNetwork>();
        network_->system = planet_.address.parent();
        network_->universeSeed = u_.seed();
        network_->fingerprint = 61026102;
        EconomicSite port;
        port.id = {planet_.address, 0};
        port.orbital = true;
        port.capacityKg = 2000000;
        port.initial.fill(10000);
        network_->sites.push_back(port);
        economy_ = std::make_unique<Economy>(network_);
        EconomicCommand fit;
        fit.sequence = ++sequence_;
        fit.action = EconomicAction::FitMiningHead;
        fit.port = port.id;
        fit.weaponMount = 1;
        fit.limitCents = 100000000;
        SurveyObserver dock;
        dock.reference = port.id.home;
        dock.position = port.metres;
        fit.observer = dock;
        expect(ledger_.execute(u_, *economy_, player_, fit).starts_with("MINING HEAD FITTED"),
               "Actual mining equipment could not be fitted");
    }
    unsigned stage() const {
        return stage_;
    }
    double distance() const {
        return distance_;
    }
    double clock() const {
        return seconds_;
    }
    bool finished() const {
        return stage_ >= 13;
    }
    const Node& grain() const {
        return grain_;
    }
    const Node& lattice() const {
        return lattice_;
    }
    const EconomyPlayer& player() const {
        return player_;
    }
    const ExplorationLedger& ledger() const {
        return ledger_;
    }
    std::shared_ptr<EconomySnapshot> snapshot() const {
        auto s = std::make_shared<EconomySnapshot>();
        s->network = network_;
        s->sites = economy_->sites();
        s->player = player_;
        s->exploration = ledger_.state();
        s->exploration.probes = *excavations_;
        return s;
    }
    std::string label() const {
        static const char* labels[] = {"CAVERN EXIT / SURVEY / BEGIN AT FULL SIZE",
                                       "SHRINK AT ENTRY",
                                       "FLY TO HOST ROCK",
                                       "CLOSE ROCK SURVEY",
                                       "SHRINK BELOW CONTINUUM",
                                       "FLY TO MINERAL / SURVEY / MARK",
                                       "FLY TO ATOMIC LATTICE",
                                       "ATOMIC ROLL / RESTART",
                                       "LEAVE LATTICE FOR MINERAL",
                                       "LEAVE MINERAL FOR HOST ROCK",
                                       "RETURN TO ENTRY / REGROW",
                                       "SELECTIVE MARKED EXTRACTION",
                                       "VERIFY FINITE CARGO / RESTART",
                                       "EXPEDITION COMPLETE"};
        return labels[std::min(13u, stage_)];
    }
    void advance(double dt, const std::shared_ptr<const Geometry>& visible = {}) {
        if (finished())
            return;
        seconds_ += dt;
        pilot_.setExcavations(excavations_, seconds_);
        ++age_;
        if (age_ > 12000)
            throw std::runtime_error("Flight expedition stalled at " + label() + " / " +
                                     pilot_.instruments().phase);
        const double rockRadius = std::pow(10., rock_.frame.logRadius),
                     grainRadius = std::pow(10., grain_.frame.logRadius),
                     latticeRadius = std::pow(10., lattice_.frame.logRadius);
        switch (stage_) {
        case 0:
            if (!at(planet_.address, entry_, rock_.address, .25, dt, visible))
                break;
            expect(issue(EconomicAction::ScanSurvey, site_.surface).starts_with("DISCOVERY"),
                   "Actual departure surface survey failed");
            expect(issue(EconomicAction::BeginSeam, rock_.address).starts_with("SEAM EXPEDITION"),
                   "Actual departure clearing cannot begin seam expedition");
            next();
            pilot_.adjustSize(std::log10(std::min(.001, rockRadius * .001)));
            break;
        case 1:
            fly(holdFrame_, holdMetres_, rock_.address, .001, dt, visible);
            if (std::abs(pilot_.ship().logSize - pilot_.ship().targetLogSize) < 1e-9)
                next();
            break;
        case 2:
            if (at(rock_.address, {0, 0, rockRadius * 1.3}, rock_.address, rockRadius * .01, dt, visible))
                next();
            break;
        case 3:
            fly(holdFrame_, holdMetres_, rock_.address, rockRadius * .01, dt, visible);
            if (scanner_.update(u_, observer(), rock_.address, 1, 1, dt, true)) {
                feedback_ = issue(EconomicAction::ScanSurvey, rock_.address);
                expect(feedback_.starts_with("CLOSE SEAM SURVEY"), "Flight-controlled rock survey failed");
                next();
                pilot_.adjustSize(-12 - pilot_.ship().targetLogSize);
            }
            break;
        case 4:
            fly(holdFrame_, holdMetres_, rock_.address, .0001, dt, visible);
            if (pilot_.ship().logSize == -12)
                next();
            break;
        case 5:
            if (at(grain_.address, {0, 0, grainRadius * 1.1}, grain_.address, grainRadius * .01, dt,
                   visible)) {
                if (scanner_.update(u_, observer(), grain_.address, 1, 1, dt, true)) {
                    feedback_ = issue(EconomicAction::ScanSurvey, grain_.address);
                    expect(feedback_.starts_with("CLOSE SEAM SURVEY"),
                           "Flight-controlled grain survey failed");
                    expect(issue(EconomicAction::MarkSeam, grain_.address).starts_with("MATERIAL MARKED"),
                           "Actual mineral could not be marked");
                    restart();
                    next();
                }
            }
            break;
        case 6:
            if (at(lattice_.address, {0, 0, latticeRadius * 3.1}, lattice_.address, latticeRadius * .04, dt,
                   visible)) {
                expect(nav_.anchor == lattice_.address, "Flight did not enter the addressed lattice");
                next();
            }
            break;
        case 7: {
            HelmInput roll;
            roll.roll = age_ < 30 ? .35 : 0;
            pilot_.update(roll, dt, visible);
            if (age_ >= 50) {
                restart();
                next();
            }
            break;
        }
        case 8:
            if (at(grain_.address, {0, 0, grainRadius * 1.1}, grain_.address, grainRadius * .01, dt, visible))
                next();
            break;
        case 9:
            if (at(rock_.address, {0, 0, rockRadius * 1.3}, rock_.address, rockRadius * .01, dt, visible)) {
                next();
                pilot_.restoreFullSize();
            }
            break;
        case 10:
            if (at(planet_.address, entry_, rock_.address, .3, dt, visible) && pilot_.sizeRatio() > .999)
                next();
            break;
        case 11: {
            fly(holdFrame_, holdMetres_, grain_.address, .3, dt, visible);
            const auto access = miningAccess(u_, observer(), rock_.address, 1, {},
                                             markedSeam(ledger_.state(), rock_.address));
            if (miner_.update(access, 1, 1, dt, true)) {
                feedback_ = issue(EconomicAction::ExtractProspect, rock_.address);
                expect(feedback_.starts_with("SEAM EXTRACTED") && player_.massKg() == 20,
                       "Actual return failed selective finite mining");
                next();
            }
            break;
        }
        case 12:
            restart();
            expect(ledger_.state().seams.back().minedKg == 20 && ledger_.state().minedKg == 20,
                   "Round trip lost the finite extraction outcome");
            expect(markedSeam(ledger_.state(), grain_.address) != nullptr, "Return lost actual mineral mark");
            {
                const auto cargo = player_.cargo;
                EconomicCommand replay;
                replay.sequence = sequence_;
                replay.action = EconomicAction::ExtractProspect;
                replay.surveyTarget = rock_.address;
                replay.observer = observer();
                replay.workSeconds = 6;
                replay.weaponMount = 1;
                expect(ledger_.execute(u_, *economy_, player_, replay).starts_with("ORDER ALREADY") &&
                           player_.cargo == cargo && ledger_.state().minedKg == 20,
                       "Restart replay duplicated selective cargo");
                const auto& seam = ledger_.state().seams.back();
                const auto& stock = ledger_.state().prospects.front();
                expect(stock.capacity == prospectResources(u_, seam.pool) &&
                           stock.extracted[seam.resource] == 20,
                       "Round trip created a replacement resource pool");
            }
            next();
            std::cout << "PASS flight-controlled surface / rock / mineral / atoms / return / full-size "
                         "selective mining / exact restarts"
                      << std::endl;
            break;
        }
        expect(pilot_.ship().hull > 0 && std::isfinite(pilot_.instruments().speed) && !nav_.transitioning,
               "Expedition lost the physical hull or invoked cinematic focus navigation");
    }
};
} // namespace infinity

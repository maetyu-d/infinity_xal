#define GLM_FORCE_INLINE
#include "infinity/Discovery.hpp"
#include "infinity/SaveTag.hpp"
#include "infinity/SignalTraffic.hpp"
#include "infinity/ShipSignals.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <set>
#include <sstream>

namespace infinity {
namespace {
bool finite(glm::dvec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
bool visible(const SignalSource& source, glm::dvec3 eye) {
    const auto ray = source.metres - eye;
    const double length2 = glm::dot(ray, ray);
    if (length2 < 1)
        return true;
    const double t = std::clamp(-glm::dot(eye, ray) / length2, 0., .99999);
    // Terrain relief belongs to the existing world. The conservative inner
    // limb blocks a planet crossing without hiding highland surface sources.
    const double radius = source.planetRadius * .995;
    return glm::length(eye + ray * t) >= radius;
}
glm::dvec3 observerAt(const Universe& u, const ReceiverObserver& o, const Address& target) {
    if (o.reference == target)
        return o.position;
    return u.pointRelative(o.reference, o.position / std::pow(10., u.describe(o.reference).frame.logRadius),
                           target, 0);
}
} // namespace
void DiscoveryController::page(std::shared_ptr<const DiscoveryPage> page) {
    if (page_ == page)
        return;
    const bool changed = !page_ || !page || page_->system != page->system;
    page_ = std::move(page);
    if (changed) {
        traffic_.clear();
        detected_.clear();
        strength_ = quality_ = pulse_ = work_ = 0;
        spectrum_.fill(0);
    }
    if (page_ && !source() && (!active_ || !liveSignalId(*active_))) {
        active_.reset();
        nextCarrier();
    }
}
const SignalSource* DiscoveryController::source() const {
    if (active_)
        for (const auto& s : traffic_)
            if (s.id == *active_)
                return &s;
    if (page_ && active_)
        for (const auto& s : page_->sources)
            if (s.id == *active_)
                return &s;
    return nullptr;
}
const DiscoveryRecord* DiscoveryController::record() const {
    if (active_)
        for (const auto& r : records_)
            if (r.id == *active_)
                return &r;
    return nullptr;
}
DiscoveryRecord* DiscoveryController::writable() {
    const auto* s = source();
    if (!s)
        return nullptr;
    for (auto& r : records_)
        if (r.id == s->id) {
            if (r.descriptor != s->descriptor) {
                status_ = "SOURCE RECIPE CHANGED / OLD RECORD RETAINED";
                return nullptr;
            }
            return &r;
        }
    if (records_.size() >= 1024) {
        const auto blank = std::find_if(records_.begin(), records_.end(),
                                        [](const auto& r) { return r.decoded == 0 && r.count == 0; });
        if (blank != records_.end())
            records_.erase(blank);
        else {
            status_ = "DISCOVERY JOURNAL FULL";
            return nullptr;
        }
    }
    records_.push_back({});
    auto& r = records_.back();
    r.id = s->id;
    r.descriptor = s->descriptor;
    return &r;
}
void DiscoveryController::tune(double MHz) {
    if (std::isfinite(MHz)) {
        frequency_ = std::clamp(frequency_ + MHz, 40., 1000.);
        work_ = quality_ = 0;
    }
}
void DiscoveryController::traffic(std::vector<SignalSource> sources) {
    sources.erase(std::remove_if(sources.begin(), sources.end(),
                                 [&](const auto& s) {
                                     return !page_ || !liveSignalId(s.id) ||
                                            s.id.planet.parent() != page_->system;
                                 }),
                  sources.end());
    if (sources.size() > 32)
        sources.resize(32);
    traffic_ = std::move(sources);
}
void DiscoveryController::nextCarrier(int direction) {
    std::vector<const SignalSource*> choices;
    for (const auto& id : detected_) {
        for (const auto& s : traffic_)
            if (s.id == id)
                choices.push_back(&s);
        if (page_)
            for (const auto& s : page_->sources)
                if (s.id == id)
                    choices.push_back(&s);
    }
    // Initial selection before the first instrument sweep. Later cycling only
    // visits received peaks, never the hidden catalog beyond radio range.
    if (choices.empty() && detected_.empty() && strength_ == 0 && !active_ && page_ &&
        !page_->sources.empty())
        choices.push_back(&page_->sources.front());
    if (choices.empty()) {
        status_ = "NO RECEIVED PEAK / MOVE OR CLEAR THE LIMB";
        return;
    }
    size_t index = 0;
    for (size_t i = 0; i < choices.size(); ++i)
        if (active_ && choices[i]->id == *active_) {
            index = (i + choices.size() + (direction < 0 ? -1 : 1)) % choices.size();
            break;
        }
    active_ = choices[index]->id;
    frequency_ = std::round(choices[index]->frequency / 10) * 10;
    work_ = quality_ = 0;
    evidence_ = 0;
    status_ = "CARRIER PEAK SELECTED / FINE TUNE AND LISTEN";
}
void DiscoveryController::nextCall() {
    std::vector<const SignalSource*> calls;
    for (const auto& s : traffic_)
        if (!shipSignalId(s.id) && s.broadcasting && std::find(detected_.begin(), detected_.end(), s.id) != detected_.end())
            calls.push_back(&s);
    if (calls.empty()) {
        status_ = "NO LOCAL CALL RECEIVED / CLEAR THE LIMB";
        return;
    }
    size_t index = 0;
    for (size_t i = 0; i < calls.size(); ++i)
        if (active_ && calls[i]->id == *active_) {
            index = (i + 1) % calls.size();
            break;
        }
    active_ = calls[index]->id;
    frequency_ = calls[index]->frequency;
    work_ = quality_ = 0;
    status_ = "EMERGENCY BAND / HOLD SPACE TO DECODE";
}
void DiscoveryController::nextShip() {
    std::vector<const SignalSource*> ships;
    for (const auto& s : traffic_)
        if (shipSignalId(s.id) && s.broadcasting &&
            std::find(detected_.begin(), detected_.end(), s.id) != detected_.end()) ships.push_back(&s);
    if (ships.empty()) { status_ = "NO SHIP CARRIER RECEIVED / CLEAR THE LIMB"; return; }
    size_t next = 0;
    for (size_t i = 0; i < ships.size(); ++i)
        if (active_ && ships[i]->id == *active_) { next = (i + 1) % ships.size(); break; }
    active_ = ships[next]->id; frequency_ = ships[next]->frequency; quality_ = work_ = 0;
    status_ = "SHIP BAND / HOLD SPACE TO IDENTIFY";
}
std::optional<ActivityId> DiscoveryController::shipContact() const {
    const auto* s = source(); const auto* r = record();
    if (!s || !shipSignalId(s->id) || !s->broadcasting || !r || r->decoded < 1 ||
        observer_.field || occluded_ || quality_ < .35) return {};
    return s->contact;
}
void DiscoveryController::evidence(unsigned which) {
    if (which < 3) {
        evidence_ = which;
        work_ = 0;
    }
}
void DiscoveryController::hypothesis() {
    theory_ = (theory_ + 1) % 4;
}
void DiscoveryController::nextJournal(int direction) {
    if (records_.empty() || !page_)
        return;
    for (size_t attempts = 0; attempts < records_.size(); ++attempts) {
        journalRow_ = (journalRow_ + records_.size() + (direction < 0 ? -1 : 1)) % records_.size();
        if (records_[journalRow_].id.planet.parent() == page_->system && records_[journalRow_].decoded >= 1)
            break;
    }
    const auto id = records_[journalRow_].id;
    const SignalSource* selected = nullptr;
    if (page_)
        for (const auto& s : page_->sources)
            if (s.id == id)
                selected = &s;
    for (const auto& s : traffic_)
        if (s.id == id)
            selected = &s;
    if (selected) {
        active_ = id;
        frequency_ = selected->frequency;
        work_ = 0;
    }
}
void DiscoveryController::update(const Universe& u, const ReceiverObserver& o, double dt, bool listening,
                                 double navigator) {
    if (!std::isfinite(dt) || dt <= 0 || dt > 1 || !finite(o.position) || !finite(o.forward) ||
        !std::isfinite(o.size) || o.size <= 0 || o.size > 1 || !std::isfinite(o.seconds) || o.seconds < 0 ||
        !std::isfinite(o.interference) || !std::isfinite(o.speed) || o.speed < 0 || !std::isfinite(navigator))
        return;
    observer_ = o;
    listening_ = listening;
    strength_ = quality_ = pulse_ = 0;
    occluded_ = false;
    spectrum_.fill(0);
    detected_.clear();
    const auto* source = this->source();
    if (!page_) {
        status_ = o.reference.kind() < Kind::System ? "ENTER A STAR SYSTEM / LOCAL RADIO"
                                                    : "MAPPING LOCAL RADIO SOURCES / ASYNC";
        return;
    }
    if (o.field) {
        status_ = "RECEIVER SATURATED / DROP WARP TO LISTEN";
        work_ = 0;
        return;
    }
    // Spectrum is an omnidirectional coarse sweep; its peaks reveal frequencies,
    // not source names, destinations or exact coordinates.
    std::vector<const SignalSource*> sources;
    sources.reserve(42);
    for (const auto& s : page_->sources)
        sources.push_back(&s);
    for (const auto& s : traffic_)
        if (s.broadcasting)
            sources.push_back(&s);
    for (const auto* pointer : sources) {
        const auto& s = *pointer;
        const auto eye = observerAt(u, o, s.id.planet);
        const double range = glm::length(s.metres - eye);
        double flux = 1 / (1 + std::pow(range / s.reach, 2));
        if (!visible(s, eye))
            flux *= .035;
        flux *= 1 - .65 * std::clamp(o.interference, 0., 1.);
        if (flux >= .06)
            detected_.push_back(s.id);
        for (unsigned i = 0; i < spectrum_.size(); ++i) {
            const double frequency = 40 + i * 960. / 63;
            spectrum_[i] =
                std::min(1., spectrum_[i] + flux * std::exp(-std::pow((frequency - s.frequency) / 12, 2)));
        }
    }
    if (!source) {
        status_ =
            page_->sources.empty() ? "NO LOCAL SOURCES / TRY ANOTHER SYSTEM" : "TAB / SELECT A RADIO PEAK";
        return;
    }
    if (source->contact && !source->broadcasting) {
        status_ = source->transmission[0];
        work_ = 0;
        return;
    }
    const auto eye = observerAt(u, o, source->id.planet);
    const auto delta = source->metres - eye;
    const double range = glm::length(delta);
    if (range < 1e-12)
        return;
    const auto direction = delta / range;
    const auto rotation = u.relative(o.reference, source->id.planet, 0).orientation;
    const auto forward = rotation * o.forward;
    const double pointing = std::max(0., glm::dot(glm::normalize(forward), direction));
    const double tuning = std::exp(-std::pow((frequency_ - source->frequency) / source->bandwidth, 2));
    occluded_ = !visible(*source, eye);
    strength_ = 1 / (1 + std::pow(range / source->reach, 2)) * (occluded_ ? .035 : 1) *
                (1 - .65 * std::clamp(o.interference, 0., 1.));
    const double antenna = std::min(1., std::sqrt(o.size * 10));
    quality_ = strength_ * tuning * (.15 + .85 * std::pow(pointing, 4)) * antenna;
    pulse_ = (source->kind == SignalKind::Archive || source->contact)
                 ? .25 + .75 * (std::fmod(o.seconds, source->period) < source->period * .32 ? 1. : 0.)
                 : .55 + .45 * std::sin(o.seconds / source->period * 6.283185307179586);
    const double sigma = .00002 + .00015 * (1 - quality_);
    uint64_t key = source->seed;
    for (unsigned i = 0; i < 3; ++i)
        key = mix64(key ^ mix64(uint64_t(int64_t(std::floor(eye[i] / 1000)))));
    const Random noise{key};
    auto side = glm::cross(direction, glm::dvec3(0, 1, 0));
    if (glm::length(side) < .01)
        side = glm::cross(direction, glm::dvec3(1, 0, 0));
    side = glm::normalize(side);
    const auto measured = glm::normalize(direction + side * sigma * noise.signedUnit(1) +
                                         glm::cross(direction, side) * sigma * noise.signedUnit(2));
    measurement_ = {eye, measured, quality_};
    localBearing_ = glm::inverse(rotation) * measured;
    if (!listening && !record())
        return;
    auto* record = writable();
    if (!record)
        return;
    status_ = occluded_        ? "SOURCE BELOW LIMB / MOVE FOR LINE OF SIGHT"
              : quality_ < .35 ? "TUNE PEAK / TURN TOWARD SIGNAL"
                               : "HOLD SPACE / LISTEN";
    if (record->decoded < 1) {
        const auto known =
            std::count_if(records_.begin(), records_.end(), [](const auto& r) { return r.solved; });
        const double recognition = source->relic ? 1 + std::min(.5, double(known) * .1) : 1;
        if (listening && quality_ >= .35)
            record->decoded =
                std::min(1., record->decoded +
                                 dt * quality_ * (.25 + .08 * std::clamp(navigator, 0., 1.5)) * recognition);
        return;
    }
    if (source->contact) {
        // Live transponders transmit their own physical fix. Ancient and natural
        // sources still require measured parallax; no hidden range is revealed.
        record->located = source->transmitsPosition || source->measuredFix;
        if(record->located) {
            record->estimate = source->metres;
            record->uncertainty = 25;
        } else record->uncertainty = 1e30;
        status_ = source->transmission[0];
        work_ = 0;
        return;
    }
    if (assistance_ && monitoring_ && listening && quality_ >= .35 && !record->located) {
        bool baseline = true;
        for (unsigned i = 0; i < record->count; ++i)
            baseline &= glm::length(record->bearings[i].origin - measurement_.origin) >= 1000;
        bool angle = record->count == 0;
        for (unsigned i = 0; i < record->count; ++i)
            angle |= glm::length(glm::cross(record->bearings[i].direction, measurement_.direction)) >= .012;
        if (baseline && angle)
            recordBearing();
    }
    status_ = record->located ? "SOURCE LOCATED / J APPROACH" : "ENTER / RECORD BEARING / MOVE LATERALLY";
    if (record->located && range < 2500 && quality_ > .35) {
        record->estimate = source->metres;
        record->uncertainty = 25;
    }
    if (!source->relic || !record->located) {
        work_ = 0;
        return;
    }
    const auto& evidence = source->relic->evidence[evidence_];
    const auto relative = observerAt(u, o, evidence.address) - evidence.metres;
    if (record->facts & (1u << evidence_)) {
        status_ = "EVIDENCE RECORDED / SELECT NEXT CLUE";
        work_ = 0;
        return;
    }
    if (o.size > evidence.requiredSize * 1.02) {
        status_ = "G / MATCH PROBE SIZE / J APPROACH EVIDENCE";
        work_ = 0;
        return;
    }
    if (glm::length(relative) > evidence.reach) {
        status_ = "J / APPROACH " + evidence.name;
        work_ = 0;
        return;
    }
    if (o.speed > std::max(1e-10, evidence.reach * .5)) {
        status_ = "STABILISE / RELATIVE MOTION TOO HIGH";
        work_ = 0;
        return;
    }
    status_ = "HOLD SPACE / READ " + evidence.name;
    if (listening) {
        work_ = std::min(4., work_ + dt * (1 + .2 * std::clamp(navigator, 0., 1.5)));
        if (work_ >= 4) {
            record->facts |= 1u << evidence_;
            work_ = 0;
            status_ = "EVIDENCE RECORDED / JOURNAL UPDATED";
        }
    } else
        work_ = 0;
}
bool DiscoveryController::solve(DiscoveryRecord& r) {
    if (r.count < 2)
        return false;
    double baseline = 0, crossing = 0;
    glm::dmat3 a(0);
    glm::dvec3 b(0);
    const auto origin = r.bearings[0].origin;
    for (unsigned i = 0; i < r.count; ++i) {
        const auto& sample = r.bearings[i];
        const auto projection = glm::dmat3(1) - glm::outerProduct(sample.direction, sample.direction);
        a += projection;
        b += projection * (sample.origin - origin);
        for (unsigned j = 0; j < i; ++j) {
            baseline = std::max(baseline, glm::length(sample.origin - r.bearings[j].origin));
            crossing = std::max(crossing, glm::length(glm::cross(sample.direction, r.bearings[j].direction)));
        }
    }
    if (baseline < 1000 || crossing < .012 || std::abs(glm::determinant(a)) < 1e-9)
        return false;
    const auto inverse = glm::inverse(a);
    const auto estimate = origin + inverse * b;
    if (!finite(estimate))
        return false;
    double residual = 0, range = 0;
    for (unsigned i = 0; i < r.count; ++i) {
        const auto d = estimate - r.bearings[i].origin;
        if (glm::dot(d, r.bearings[i].direction) <= 0)
            return false;
        residual += std::pow(glm::length(glm::cross(d, r.bearings[i].direction)), 2);
        range = std::max(range, glm::length(d));
    }
    const double uncertainty = std::max(
        25., std::sqrt(residual / r.count) +
                 range * .00017 * std::sqrt(std::max(0., inverse[0][0] + inverse[1][1] + inverse[2][2])));
    if (uncertainty > std::max(200., range * .05))
        return false;
    r.estimate = estimate;
    r.uncertainty = uncertainty;
    r.located = true;
    return true;
}
std::string DiscoveryController::recordBearing() {
    auto* r = writable();
    if (!r)
        return status_;
    if (r->decoded < 1)
        return status_ = "DECODE CARRIER FIRST / HOLD SPACE";
    if (const auto* s = source(); s && shipSignalId(s->id))
        return status_ = "MOVING SHIP / S ACTIVE SCAN / NO STATIC TRIANGULATION";
    if (observer_.field || occluded_ || quality_ < .35)
        return status_ = "BEARING REJECTED / WEAK OR OCCLUDED CARRIER";
    for (unsigned i = 0; i < r->count; ++i)
        if (glm::length(r->bearings[i].origin - measurement_.origin) < 1000)
            return status_ = "BASELINE TOO SHORT / MOVE AT LEAST 1 KM";
    if (r->count == r->bearings.size()) {
        for (unsigned i = 1; i < r->count; ++i)
            r->bearings[i - 1] = r->bearings[i];
        --r->count;
    }
    r->bearings[r->count++] = measurement_;
    return status_ = solve(*r) ? "TRIANGULATION SOLVED / J APPROACH SOURCE"
                               : "BEARING STORED / NEED A WIDER CROSSING ANGLE";
}
std::string DiscoveryController::conclude() {
    auto* r = writable();
    const auto* s = source();
    if (!r || !s || !s->relic)
        return "NO ARCHIVE TO INTERPRET";
    if (r->solved)
        return status_ = "HISTORY ALREADY RECONSTRUCTED / RECORD RETAINED";
    if (r->facts != 7)
        return status_ = "READ FOUNDATIONS, FRACTURE AND SUBSTRATE FIRST";
    if (theory_ != unsigned(s->relic->cause))
        return status_ = "HYPOTHESIS DOES NOT FIT THE RECORDED CONTACT";
    r->solved = true;
    return status_ = "HISTORY RECONSTRUCTED / ARCHIVE SIGNATURE ADDED";
}
std::optional<DiscoveryTarget> DiscoveryController::target() const {
    const auto* s = source();
    const auto* r = record();
    if (!s || !r || !r->located)
        return {};
    if (s->contact) {
        if (!s->broadcasting || (shipSignalId(s->id) &&
            ((!s->transmitsPosition && !s->measuredFix) || observer_.field || occluded_ || quality_ < .35)))
            return {};
        return DiscoveryTarget{s->id.planet, s->metres, 100, 1, false, s->worldLabel, s->contact};
    }
    if (s->relic) {
        if (r->uncertainty > 150)
            return DiscoveryTarget{
                s->id.planet,        r->estimate, std::min(1000., r->uncertainty), 1, false,
                "RADIO SEARCH AREA", {}};
        const auto& e = s->relic->evidence[evidence_];
        if (evidence_ == 0)
            return DiscoveryTarget{s->id.planet,
                                   s->relic->landing + glm::normalize(s->relic->landing) * 10.,
                                   12,
                                   1,
                                   true,
                                   s->relic->name,
                                   {}};
        // Navigation to the actual host address preserves nanometre precision.
        return DiscoveryTarget{e.address, e.metres, std::max(1e-11, e.reach * .35), e.requiredSize, false,
                               e.name,    {}};
    }
    return DiscoveryTarget{s->id.planet, r->estimate, std::max(100., r->uncertainty), 1, false,
                           s->signature, {}};
}
std::optional<std::pair<ActivityId, uint64_t>> DiscoveryController::offer() const {
    const auto* s = source();
    const auto* r = record();
    if (observer_.field || occluded_ || quality_ < .35 || !s || !s->contact || !s->offer || s->accepted ||
        !s->broadcasting || !r || r->decoded < 1)
        return {};
    return std::pair{*s->contact, s->offer};
}
DiscoveryView DiscoveryController::view(bool open) const {
    DiscoveryView v;
    v.open = open;
    v.journal = journal_;
    v.page = page_;
    v.active = active_;
    v.frequency = frequency_;
    v.strength = strength_;
    v.quality = quality_;
    v.pulse = pulse_;
    v.work = work_ / 4;
    v.bearing = localBearing_;
    v.spectrum = spectrum_;
    v.status = status_;
    v.listening = listening_;
    v.occluded = occluded_;
    v.evidence = evidence_;
    v.theory = theory_;
    v.loading = !page_;
    v.size = observer_.size;
    v.monitoring = monitoring_;
    v.assistance = assistance_;
    for (const auto& s : traffic_)
        if (s.broadcasting && std::find(detected_.begin(), detected_.end(), s.id) != detected_.end())
            shipSignalId(s.id) ? ++v.ships : ++v.calls;
    const auto* s = source();
    const auto* r = s ? record() : nullptr;
    v.hasSignal = bool(s);
    if (r) {
        v.decode = r->decoded;
        v.decoded = r->decoded >= 1;
        v.located = r->located;
        v.solved = r->solved;
        v.uncertainty = r->uncertainty;
        v.observations = r->count;
        v.facts = r->facts;
    }
    if (s && v.decoded) {
        v.signature = s->signature;
        v.archaeology = s->relic.has_value();
        if (v.located)
            v.destination = s->relic ? s->relic->name : s->worldLabel;
    }
    if (s && s->contact && v.decoded) {
        v.live = true;
        v.accepted = s->accepted;
        v.closed = !s->broadcasting;
        v.transmission = s->transmission;
        if (shipSignalId(s->id) && !s->transmitsPosition && !s->measuredFix) {
            v.located = false; v.uncertainty = 1e30;
        }
        if (shipSignalId(s->id) && (observer_.field || occluded_ || quality_ < .35)) {
            v.located=false;
            v.transmission={"CARRIER LOST / LAST ID RETAINED","NO CURRENT TELEMETRY / CLEAR LIMB","V NEXT SHIP / HOLD SPACE TO LISTEN"};
        }
    }
    if (s && s->relic && v.decoded) {
        v.requiredSize = s->relic->evidence[evidence_].requiredSize;
        v.hypothesis = ruinCauseName(RuinCause(theory_));
        for (unsigned i = 0; i < 3; ++i)
            v.clues[i] = (v.facts & (1u << i)) ? s->relic->evidence[i].finding
                                               : "UNREAD / " + s->relic->evidence[i].name;
        v.finding = v.clues[evidence_];
        if (v.solved)
            v.chronicle = s->relic->chronicle;
    }
    for (const auto& entry : records_) {
        if (entry.solved)
            ++v.discoveries;
        if (entry.decoded >= 1 && page_ && entry.id.planet.parent() == page_->system) {
            if (active_ && entry.id == *active_)
                v.journalSelection = unsigned(v.journalRows.size());
            std::string row;
            if (liveSignalId(entry.id)) {
                const auto live = std::find_if(traffic_.begin(), traffic_.end(),
                                               [&](const auto& s) { return s.id == entry.id; });
                row = live == traffic_.end()
                          ? "PAST CALL / " + std::to_string(entry.id.episode)
                          : std::string(live->broadcasting ? "CALL / " : "CLOSED / ") + live->worldLabel;
            } else {
                row = entry.solved ? "HISTORY / " : entry.located ? "LOCATED / " : "BEARINGS / ";
                row += (isMoon(entry.id.planet) ? "MOON " : "PLANET ") +
                       std::to_string(entry.id.planet.id() % 100000) + " #" +
                       std::to_string(entry.id.channel);
            }
            v.journalRows.push_back(std::move(row));
        }
    }
    v.reward = v.discoveries
                   ? "ARCHIVE PATTERNS " + std::to_string(v.discoveries) + " / KNOWN ARTIFICIAL SIGNATURES"
                   : "RECONSTRUCT A HISTORY / LEARN AN ARCHIVE PATTERN";
    if (v.live)
        v.reward = v.closed   ? "TRANSMISSION ENDED / LEDGER RECEIPT RETAINED"
                   : s->offer ? "REAL COLONY SUPPLIES / EXISTING FUNDED JOB"
                              : "REAL SHIP / EXISTING RESCUE, REPAIR AND SALVAGE";
    if (s && shipSignalId(s->id) && v.decoded)
        v.reward = "PASSIVE BEARINGS / ACTIVE SCAN AND JAM REVEAL YOUR PRESENCE";
    return v;
}
std::string DiscoveryController::serialize(uint64_t seed) const {
    std::ostringstream o;
    o << std::setprecision(17) << "INFINITY_DISCOVERY 2 " << seed << ' ' << frequency_ << ' ' << evidence_
      << ' ' << theory_ << ' ' << bool(active_);
    if (active_)
        o << ' ' << std::quoted(active_->planet.str()) << ' ' << active_->channel << ' ' << active_->episode;
    o << ' ' << monitoring_ << ' ' << assistance_ << ' ' << records_.size();
    for (const auto& r : records_) {
        o << ' ' << std::quoted(r.id.planet.str()) << ' ' << r.id.channel << ' ' << r.id.episode << ' '
          << r.descriptor << ' ' << r.decoded << ' ' << r.count << ' ' << r.located << ' ' << r.solved << ' '
          << r.estimate.x << ' ' << r.estimate.y << ' ' << r.estimate.z << ' ' << r.uncertainty << ' '
          << r.facts;
        for (unsigned i = 0; i < r.count; ++i) {
            const auto& b = r.bearings[i];
            o << ' ' << b.origin.x << ' ' << b.origin.y << ' ' << b.origin.z << ' ' << b.direction.x << ' '
              << b.direction.y << ' ' << b.direction.z << ' ' << b.quality;
        }
    }
    return o.str();
}
bool DiscoveryController::restore(std::string_view text, const Universe& u) {
    if (text.size() > 2 * 1024 * 1024)
        return false;
    try {
        DiscoveryController c;
        std::istringstream in{std::string(text)};
        std::string tag, address;
        unsigned version, active, count;
        uint64_t seed;
        auto id = [&](DiscoveryId& a) {
            if (!(in >> std::quoted(address) >> a.channel))
                return false;
            if (version >= 2 && !(in >> a.episode))
                return false;
            a.planet = Address::parse(address);
            return (version >= 2 || a.channel <= 1) && a.planet.kind() == Kind::Planet &&
                   ((a.channel <= 1 && a.episode == 0) || liveSignalId(a));
        };
        if (!(in >> tag >> version >> seed >> c.frequency_ >> c.evidence_ >> c.theory_ >> active) ||
            !matchesSaveTag(tag, "INFINITY_DISCOVERY") || (version != 1 && version != 2) ||
            seed != u.seed() || !std::isfinite(c.frequency_) || c.frequency_ < 40 || c.frequency_ > 1000 ||
            c.evidence_ > 2 || c.theory_ > 3 || active > 1)
            return false;
        if (active) {
            DiscoveryId a;
            if (!id(a))
                return false;
            c.active_ = a;
        }
        unsigned monitor, assist = 1;
        if (!(in >> monitor))
            return false;
        if (version >= 2 && !(in >> assist))
            return false;
        if (!(in >> count) || monitor > 1 || assist > 1 || count > 1024)
            return false;
        c.monitoring_ = monitor;
        c.assistance_ = assist;
        std::set<DiscoveryId> seen;
        for (unsigned i = 0; i < count; ++i) {
            DiscoveryRecord r;
            unsigned located, solved;
            if (!id(r.id) || !seen.insert(r.id).second ||
                !(in >> r.descriptor >> r.decoded >> r.count >> located >> solved >> r.estimate.x >>
                  r.estimate.y >> r.estimate.z >> r.uncertainty >> r.facts) ||
                !std::isfinite(r.decoded) || r.decoded < 0 || r.decoded > 1 || r.count > 4 || located > 1 ||
                solved > 1 || r.facts > 7 || !finite(r.estimate) || !std::isfinite(r.uncertainty) ||
                r.uncertainty < 0 || r.uncertainty > 1e30 ||
                (located && r.count < 2 && !liveSignalId(r.id)) || (r.facts && !located) ||
                (solved && r.facts != 7) || (r.id.channel == 0 && (r.facts || solved)))
                return false;
            if (liveSignalId(r.id)) {
                if (r.descriptor != liveSignalDescriptor(u, r.id) || r.facts || solved)
                    return false;
            } else {
                const auto planet = u.describe(r.id.planet);
                const auto key = mix64(planet.seed ^ 0x5349474e414c3635ULL ^ mix64(r.id.channel));
                if (r.descriptor != mix64(key ^ descriptorFingerprint(planet)))
                    return false;
                if (r.id.channel == 1 && (!hasSolidSurface(planet) || planet.environment.ageGyr < .3 ||
                                          planet.environment.temperature > 950))
                    return false;
            }
            r.located = located;
            r.solved = solved;
            for (unsigned j = 0; j < r.count; ++j) {
                auto& b = r.bearings[j];
                if (!(in >> b.origin.x >> b.origin.y >> b.origin.z >> b.direction.x >> b.direction.y >>
                      b.direction.z >> b.quality) ||
                    !finite(b.origin) || !finite(b.direction) ||
                    std::abs(glm::length(b.direction) - 1) > 1e-6 || !std::isfinite(b.quality) ||
                    b.quality < .35 || b.quality > 1)
                    return false;
            }
            c.records_.push_back(r);
        }
        in >> std::ws;
        if (!in.eof())
            return false;
        *this = std::move(c);
        return true;
    } catch (...) {
        return false;
    }
}
} // namespace infinity

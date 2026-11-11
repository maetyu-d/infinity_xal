#pragma once
#include "Universe.hpp"
#include "Activity.hpp"
#include <array>
#include <mutex>
#include <oneapi/tbb/task_group.h>

namespace infinity {
struct DiscoveryId {
    Address planet;
    unsigned channel = 0;
    uint64_t episode = 0; // Live job identity; never part of a universe Address.
    auto operator<=>(const DiscoveryId&) const = default;
};
enum class SignalKind : uint8_t { Magnetosphere, IceResonance, Archive, Distress, Relief, Transponder, Tactical };
struct ShipSignalView;
enum class RuinCause : uint8_t { Impact, Heating, IceMovement, Weathering };
std::string_view ruinCauseName(RuinCause);
struct RelicBlock {
    glm::dvec3 metres{}, dimensions{1};
    glm::dquat orientation{1,0,0,0};
    glm::vec3 tint{};
    unsigned mesh = 0;
};
struct RelicEvidence {
    Address address; // Existing material address, never a substitute rock/grain.
    glm::dvec3 metres{};
    double requiredSize = 1, reach = 300;
    std::string name, finding;
    Address specimen; // Canonical grain/lattice read by standoff diffraction.
};
struct RelicSite {
    glm::dvec3 metres{}, landing{};
    double ageYears = 0;
    RuinCause cause = RuinCause::Weathering;
    std::string name, purpose;
    std::array<std::string,3> chronicle;
    std::array<RelicEvidence,3> evidence;
    std::vector<RelicBlock> blocks;
};
struct SignalSource {
    DiscoveryId id;
    uint64_t seed = 0, descriptor = 0;
    SignalKind kind = SignalKind::Magnetosphere;
    glm::dvec3 metres{};
    double frequency = 100, bandwidth = 4, period = 2, reach = 1e9, planetRadius = 1;
    std::string signature, worldLabel;
    std::optional<RelicSite> relic;
    std::optional<ActivityId> contact;
    uint64_t offer = 0;
    bool accepted = false, broadcasting = true;
    bool transmitsPosition = true, measuredFix = false;
    std::array<std::string,3> transmission;
};
struct DiscoveryPage { Address system; std::vector<SignalSource> sources; size_t bytes() const; };
std::optional<SignalSource> generateSignal(const Universe&, const Node&, unsigned channel,
                                         const std::atomic<bool>* cancel = nullptr);
std::shared_ptr<const DiscoveryPage> generateDiscoveryPage(const Universe&, const Address&,
                                                        const std::atomic<bool>* cancel = nullptr);
std::optional<double> discoveryCollision(const Universe&, const DiscoveryPage&, const Address& reference,
                                       glm::dvec3 from, glm::dvec3 to, double radius);
// Three immutable pages, at most two cancellable worker jobs. No terrain work
// or archaeology geometry generation runs in the rendering thread.
class DiscoveryCache {
  public:
    ~DiscoveryCache();
    std::shared_ptr<const DiscoveryPage> request(const Universe&, const Address&);
    void wait();
    size_t bytes() const;
  private:
    struct Entry { Address system; uint64_t used=0; std::shared_ptr<std::atomic<bool>> cancel; std::shared_ptr<const DiscoveryPage> page; };
    mutable std::mutex mutex_;
    std::vector<Entry> entries_;
    oneapi::tbb::task_group tasks_;
    uint64_t serial_=0;
    unsigned pending_=0;
};
struct ReceiverObserver {
    Address reference;
    glm::dvec3 position{}, forward{0,0,-1}; // SI in reference axes
    double size=1, seconds=0, speed=0, interference=0;
    bool field=false, landed=false;
};
struct SignalBearing { glm::dvec3 origin{}, direction{0,0,-1}; double quality=0; };
struct DiscoveryRecord {
    DiscoveryId id;
    uint64_t descriptor=0;
    double decoded=0;
    std::array<SignalBearing,4> bearings;
    unsigned count=0;
    bool located=false, solved=false;
    glm::dvec3 estimate{};
    double uncertainty=1e30;
    unsigned facts=0;
};
struct DiscoveryTarget { Address reference; glm::dvec3 metres{}; double standOff=100, size=1; bool surface=false; std::string label; std::optional<ActivityId> contact; };
struct DiscoveryView {
    bool open=false, journal=false, listening=false, decoded=false, located=false, solved=false, loading=false;
    bool hasSignal=false, archaeology=false, occluded=false, monitoring=false, live=false, accepted=false, closed=false, assistance=false;
    unsigned calls=0;
    unsigned ships=0;
    std::shared_ptr<const ShipSignalView> ship;
    std::shared_ptr<const DiscoveryPage> page;
    std::optional<DiscoveryId> active;
    double frequency=100, strength=0, quality=0, pulse=0, decode=0, uncertainty=1e30, work=0;
    double size=1, requiredSize=1;
    glm::dvec3 bearing{};
    std::array<double,64> spectrum{};
    unsigned observations=0, evidence=0, facts=0, theory=0, discoveries=0, journalSelection=0;
    std::string status, signature, destination, finding, hypothesis, reward;
    std::array<std::string,3> clues, chronicle;
    std::array<std::string,3> transmission;
    std::vector<std::string> journalRows;
};
class DiscoveryController {
  public:
    void page(std::shared_ptr<const DiscoveryPage>);
    void traffic(std::vector<SignalSource>); // Read-only activity/economy adapter, bounded to 32.
    void nextCall();
    void nextShip();
    std::optional<ActivityId> shipContact() const;
    void toggleAssistance() { assistance_=!assistance_; }
    void tune(double MHz);
    void nextCarrier(int direction=1);
    void evidence(unsigned);
    void hypothesis();
    void toggleJournal() { journal_=!journal_; }
    void toggleMonitor() { monitoring_=!monitoring_; }
    bool monitoring() const { return monitoring_; }
    const std::shared_ptr<const DiscoveryPage>& catalog() const { return page_; }
    void nextJournal(int);
    void update(const Universe&, const ReceiverObserver&, double dt, bool listening, double navigator=0);
    std::string recordBearing();
    std::string conclude();
    std::optional<DiscoveryTarget> target() const;
    std::optional<std::pair<ActivityId,uint64_t>> offer() const;
    DiscoveryView view(bool open=false) const;
    const SignalSource* source() const;
    const DiscoveryRecord* record() const;
    const std::vector<DiscoveryRecord>& records() const { return records_; }
    std::string serialize(uint64_t universeSeed) const;
    bool restore(std::string_view, const Universe&);
  private:
    std::shared_ptr<const DiscoveryPage> page_;
    std::vector<SignalSource> traffic_;
    std::vector<DiscoveryId> detected_;
    std::vector<DiscoveryRecord> records_;
    std::optional<DiscoveryId> active_;
    ReceiverObserver observer_;
    SignalBearing measurement_;
    glm::dvec3 localBearing_{};
    std::array<double,64> spectrum_{};
    double frequency_=100, strength_=0, quality_=0, pulse_=0, work_=0;
    unsigned evidence_=0, theory_=0, journalRow_=0;
    bool journal_=false, listening_=false, occluded_=false, monitoring_=false, assistance_=true;
    std::string status_;
    DiscoveryRecord* writable();
    bool solve(DiscoveryRecord&);
};
} // namespace infinity

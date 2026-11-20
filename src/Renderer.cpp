#include "infinity/Renderer.hpp"
#include "infinity/Xalaria.hpp"
#include "infinity/XalariaEconomy.hpp"
#include "infinity/XalariaTraffic.hpp"
#include "infinity/XalariaStories.hpp"
#include "infinity/XalariaCombat.hpp"
#include "infinity/ShipTrail.hpp"
#include "HyperionRenderer.hpp"
#include "infinity/ShipInterior.hpp"
#include "AtlasFont.hpp"
#include "infinity/Cockpit.hpp"
#include "infinity/ExpeditionPlanner.hpp"
#include "infinity/MiningConsequences.hpp"
#include "infinity/CrewMinimap.hpp"
#include "infinity/Discovery.hpp"
#include "infinity/Economy.hpp"
#include "infinity/NpcCombat.hpp"
#include "infinity/OrbitalDebris.hpp"
#include "infinity/PointLight.hpp"
#include "infinity/ProbePresentation.hpp"
#include "infinity/CavernHazard.hpp"
#include "infinity/Excavation.hpp"
#include "infinity/StationControl.hpp"
#include "infinity/StellarEvolution.hpp"
#include "infinity/Streaming.hpp"
#include "infinity/SurfaceShading.hpp"
#include "infinity/MicroView.hpp"
#include "infinity/Weapons.hpp"
#include "infinity/WorldDepth.hpp"
#include "infinity/SurfaceOperations.hpp"
#include "infinity/Urban.hpp"
#include "infinity/WarpTravel.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>
#include <algorithm>
#include <bgfx/bgfx.h>
#include <bgfx/embedded_shader.h>
#include <chrono>
#include <bit>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fs_debugdraw_lines.bin.h>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <map>
#include <numbers>
#include <oneapi/tbb/task_group.h>
#include <stdexcept>
#include <tuple>
#include <unordered_set>
#include <vs_debugdraw_lines.bin.h>

namespace infinity {
static const bgfx::EmbeddedShader embedded[] = {BGFX_EMBEDDED_SHADER(vs_debugdraw_lines),
                                                BGFX_EMBEDDED_SHADER(fs_debugdraw_lines),
                                                BGFX_EMBEDDED_SHADER_END()};
struct Callbacks final : bgfx::CallbackI {
    void fatal(const char*, uint16_t, bgfx::Fatal::Enum, const char* message) override {
        std::fprintf(stderr, "bgfx fatal: %s\n", message);
        std::abort();
    }
    void traceVargs(const char*, uint16_t, const char* format, va_list args) override {
        std::vfprintf(stderr, format, args);
    }
    void profilerBegin(const char*, uint32_t, const char*, uint16_t) override {}
    void profilerBeginLiteral(const char*, uint32_t, const char*, uint16_t) override {}
    void profilerEnd() override {}
    uint32_t cacheReadSize(uint64_t) override {
        return 0;
    }
    bool cacheRead(uint64_t, void*, uint32_t) override {
        return false;
    }
    void cacheWrite(uint64_t, const void*, uint32_t) override {}
    void screenShot(const char* filename, uint32_t width, uint32_t height, uint32_t pitch,
                    bgfx::TextureFormat::Enum, const void* data, uint32_t, bool flip) override {
        // Portable, lossless PPM; no extra codec dependency. Source is BGRA8.
        std::ofstream out(filename, std::ios::binary);
        out << "P6\n" << width << ' ' << height << "\n255\n";
        auto bytes = static_cast<const uint8_t*>(data);
        std::vector<char> row(width * 3);
        for (uint32_t y = 0; y < height; ++y) {
            auto p = bytes + (flip ? height - y - 1 : y) * pitch;
            for (uint32_t x = 0; x < width; ++x) {
                row[x * 3] = static_cast<char>(p[x * 4 + 2]);
                row[x * 3 + 1] = static_cast<char>(p[x * 4 + 1]);
                row[x * 3 + 2] = static_cast<char>(p[x * 4]);
            }
            out.write(row.data(), row.size());
        }
    }
    void captureBegin(uint32_t, uint32_t, uint32_t, bgfx::TextureFormat::Enum, bool) override {}
    void captureEnd() override {}
    void captureFrame(const void*, uint32_t) override {}
};
struct GPUMesh {
    bgfx::VertexBufferHandle vertices = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle indices = BGFX_INVALID_HANDLE;
    size_t count = 0;
};
static void destroy(GPUMesh& m) {
    if (bgfx::isValid(m.vertices))
        bgfx::destroy(m.vertices);
    if (bgfx::isValid(m.indices))
        bgfx::destroy(m.indices);
    m = {};
}
struct NormalWork {
    std::atomic<bool> cancel{false}, ready{false};
    std::vector<ShadingNormal> result;
};
struct GPUGeometry {
    GPUMesh solid, lines;
    bgfx::VertexBufferHandle normals = BGFX_INVALID_HANDLE;
    std::shared_ptr<NormalWork> normalWork;
    bgfx::VertexBufferHandle instances = BGFX_INVALID_HANDLE;
    std::weak_ptr<const Geometry> source;
    uint64_t lastUsed = 0;
    size_t bytes = 0;
    bgfx::DynamicVertexBufferHandle probeLight = BGFX_INVALID_HANDLE;
    std::optional<ProbeWallLighting> probeLightKey;
    uint64_t miningWearKey = UINT64_MAX;
};
struct Instance {
    glm::mat4 transform;
    glm::vec4 color;
};
static_assert(sizeof(Instance) == 80);
struct Pick {
    Address address;
    glm::dvec2 screen;
    double depth;
};
struct Renderer::Impl {
    const XalariaMapView* xalariaMap=nullptr;
    glm::vec3 regionalLook{0.f}; // Fog, tidal and acid blend. The remainder is Charred Glass.
    float dockedFocus=0.f;
    const XalariaEconomy* xalariaEconomy=nullptr;
    const XalariaRadioView* xalariaRadio=nullptr;
    const XalariaStories* xalariaStories=nullptr;
    const XalariaCombat* xalariaCombat=nullptr;
    StoryContext storyContext;
    std::string storyRouteAction;
    std::optional<ActivityId> distressVessel;
    const Universe& universe;
    SDL_Window* window;
    Callbacks callbacks;
    bgfx::VertexLayout layout, instanceLayout, normalLayout;
    oneapi::tbb::task_group normalTasks;
    std::atomic<unsigned> normalJobs{0};
    bgfx::ProgramHandle colorProgram = BGFX_INVALID_HANDLE;
    bgfx::SwapChain swapChain;
    uint32_t resetFlags = BGFX_RESET_VSYNC | BGFX_RESET_MAXANISOTROPY;
    std::array<GPUMesh, 4> atomSpheres;
    GPUMesh ringMesh;
    GPUMesh hyperionBillboardMesh;
    GPUMesh pointLightBillboardMesh;
    GPUMesh cabin, cabinGlazing, cabinYoke, cabinThrottle, cabinHatch, shipDeckMesh;
    GPUMesh dockingDeckMesh, dockingGlassMesh, airlockDoorMesh, airlockSillMesh, airlockIndicatorMesh, charterCrateMesh;
    std::optional<std::pair<StationKey,XalariaLook>> renderedBoardingStation;
    std::vector<CabinDisplay> renderedStationSigns;
    std::optional<CabinDisplay> renderedStationSurvey;
    std::optional<CabinDisplay> renderedFreightDispatch;
    bool renderedFreightClaimed = false;
    std::optional<XContract> boardingCharter;
    int64_t boardingCharterDay = 0;
    const ShipInterior* shipInterior = nullptr;
    double cabinAspect = 0;
    bool cabinIsXalaria = false;
    std::optional<FlightInstruments> instruments;
    const ExpeditionPlan* expeditionPlan = nullptr;
    CockpitMotion cockpitMotion;
    std::vector<ActivityId> explosionImpulses;
    CockpitMotionPose head;
    SeatTransition seatTransition;
    FlightHudStability flightHud;
    DeckHudLabel deckRoomLabel, deckPromptLabel;
    std::optional<Address> previousHudReference;
    glm::dquat previousHudView{1,0,0,0};
    glm::dvec2 cockpitLook{};
    bool cockpitMotionEnabled = true;
    bool cockpitHelp = false, cockpitVisible = false, validateCockpit = false;
    std::vector<CockpitRect> cockpitHitRegions;
    std::vector<CockpitContactHit> cockpitContactHits;
    CockpitViewport hudViewport;
    bool polarLights = true;
    bool cloudVolumes = true;
    bool stellarOptics = true;
    bool pointLightFiltering = true;
    bool landingShade = true;
    bool weatherEffects = true;
    bool planetRings = true;
    float stellarExposure = 1;
    std::shared_ptr<const Geometry> skyGeometry;
    std::vector<CelestialContact> contacts;
    std::optional<Address> lifeSelection;
    std::vector<CelestialContact> skyOccluders;
    const ActivityState* activity = nullptr;
    const WeaponSystem* weapons = nullptr;
    const NpcCombat* npcCombat = nullptr;
    const CrewView* crew = nullptr;
    std::vector<ActivityBody> combatBodies;
    std::vector<ActivityBody> weaponTargets;
    std::shared_ptr<const EconomySnapshot> economy;
    const MarketView* market = nullptr;
    std::shared_ptr<const EconomicNetwork> lightingNetwork;
    std::vector<float> settlementLights;
    std::array<std::array<GPUMesh,3>,size_t(SurfaceMachine::Count)> workMeshes;
    struct WorkClock { SurfaceWorkClock clock, machinery; uint64_t used = 0; };
    std::map<ActivityId,WorkClock> workClocks;
    void updateEconomicLighting(double dt) {
        if (!economy || !economy->network || economy->catchingUp ||
            economy->portSignals.size() != economy->network->sites.size())
            return;
        if (lightingNetwork != economy->network) {
            lightingNetwork = economy->network;
            settlementLights.clear();
            for (const auto& signal : economy->portSignals)
                settlementLights.push_back(signal.lighting);
            return;
        }
        const float blend = float(1 - std::exp(-std::clamp(dt, 0., .1) * 2.));
        for (size_t i = 0; i < settlementLights.size(); ++i)
            settlementLights[i] = glm::mix(settlementLights[i], economy->portSignals[i].lighting, blend);
    }
    std::vector<ActivityContact> activityContacts;
    std::vector<std::pair<ActivityId, glm::dvec2>> activityPicks;
    static constexpr size_t debrisSurfaceRole = 6;
    static constexpr size_t spaceJunkFirstRole = debrisSurfaceRole + 1;
    static constexpr size_t vesselCollarRole = spaceJunkFirstRole + size_t(SpaceJunkKind::Count);
    static constexpr size_t vesselCollarMountRole = vesselCollarRole+1;
    static constexpr size_t evidenceFirstRole = vesselCollarMountRole+1;
    static constexpr size_t activityRoles = evidenceFirstRole+3;
    using ActivityBatches = std::array<std::array<std::vector<Instance>, 3>, activityRoles>;
    std::array<std::array<GPUMesh, 3>, activityRoles> craftMeshes;
    struct StationMeshEntry { GPUMesh mesh, glass; size_t bytes=0; uint64_t lastFrame=0; };
    using StationMeshKey=std::pair<StationKey,unsigned>;
    std::map<StationMeshKey,StationMeshEntry> stationMeshes;
    size_t craftBytes = 0;
    SurfaceLifeCache surfaceLife;
    std::array<std::array<GPUMesh, 3>, size_t(SurfaceMesh::Count)> surfaceMeshes;
    size_t surfaceMeshBytes = 0;
    std::string weatherReport;
    std::string weatherDetail;
    Mesh weatherRain, weatherFlakes;
    double weatherSeverity = 0, weatherPriority = 1e100;
    std::unique_ptr<HyperionRenderer> hyperion;
    static constexpr RenderMode mode = RenderMode::Hyperion;
    std::optional<Address> specimen;
    std::shared_ptr<const std::vector<ProspectSite>> prospectSites;
    Mesh prospectMarkers;
    double visualTime = 0, fixedVisualTime = -1;
    WarpOptics warpOptics;
    double warpClock = 0;
    std::optional<Address> warpOpticalDestination;
    glm::dvec2 warpFocus{};
    bgfx::ViewId atomView = 2;
    std::map<const Geometry*, GPUGeometry> gpu;
    std::vector<Pick> picks;
    std::vector<Instance> billboards;
    std::vector<Instance> trafficLights;
    Mesh shieldFields;
    std::map<ActivityId,ShipTrail> trafficTrails;
    ShipTrail playerTrail;
    Mesh trailRibbons;
    ShieldReadout playerShield;
    struct ShieldFade {float visibility;uint64_t seen;};
    std::map<ActivityId,ShieldFade> shieldFades;
    // Shares the camera-relative effects pass with weapon traces, after opaque actors.
    static constexpr bgfx::ViewId trafficLightView=63;
    std::vector<Instance> cosmicBillboards;
    std::vector<Instance> pointLightBillboards;
    std::vector<Instance> stellarBillboards;
    std::vector<LocalFrame> anchorFrames;
    std::map<Address, std::vector<LocalFrame>> frameFrames;
    std::map<Address, Pose> framePoses;
    int width = 1280, height = 800;
    uint64_t frame = 0;
    size_t gpuBytes = 0;
    size_t staticUploadBytes = 0;
    glm::dmat4 view{1}, projection{1};
    glm::dquat cameraQ{1, 0, 0, 0}, cameraInverse{1, 0, 0, 0};
    glm::dvec3 eye{};
    bool initialized = false;
    FrameStats stats;
    struct DetailState {
        unsigned level = 0;
        int sectionWindow = 0;
        bool initialized = false;
        GeometryBlend blend;
        uint64_t used = 0;
    };
    std::map<Address, DetailState> details;
    struct VisibleRing {
        Address planet;
        PlanetTraits traits;
        Pose pose;
        double distance;
        float opacity;
    };
    std::vector<VisibleRing> visibleRings;
    std::map<std::string, DetailState> localSections;
    std::chrono::steady_clock::time_point previousTime = std::chrono::steady_clock::now();
    double frameSeconds = 0, fieldOfView = 1;
    std::string sectionLabel;
    TerrainStreamer terrain;
    bool terrainActive = false, terrainSunComplete = true, sunShadows = true;
    bool probeRemote = false;
    IceStress iceStress;
    ExcavationWork excavationWork;
    bool probeOptimisation = true, exteriorHidden = false;
    uint64_t enclosedBore = 0;
    const Navigation* shipNavigation = nullptr;
    double probeSeconds = 0;
    GPUMesh probeHousing, probeCutter, probeLamps, probeRoller, probeDrive, probeActuator, probeRod, probeHotTeeth;
    size_t probeRigBytes = 0;
    std::vector<Vertex> probeLightScratch;
    Mesh probeFragments, probeDust, probeSparks;
    bool terrainCoverageDebug = false;
    std::optional<Address> terrainSunPlanet;
    glm::dvec3 terrainSunAnchor{};
    double terrainSunSpan = 0;
    explicit Impl(SDL_Window* w, const Universe& u) : universe(u), window(w), surfaceLife(u), terrain(u) {}
    bgfx::ViewId worldView() const {
        return instruments ? 2 : 0; // Flight sky precedes terrain (1) and opaque local geometry (2).
    }
    CockpitViewport cockpitViewport() const {
        int logicalW = width, logicalH = height;
        SDL_GetWindowSize(window, &logicalW, &logicalH);
        SDL_Rect safe{0, 0, logicalW, logicalH};
        if (!SDL_GetWindowSafeArea(window, &safe))
            safe = {0, 0, logicalW, logicalH};
        const double sx = double(width) / std::max(1, logicalW);
        const double sy = double(height) / std::max(1, logicalH);
        return {width, height, {safe.x * sx, safe.y * sy, safe.w * sx, safe.h * sy}};
    }
    ~Impl() {
        for (auto& [key, g] : gpu)
            if (g.normalWork)
                g.normalWork->cancel.store(true);
        normalTasks.wait();
        if (initialized) {
            for (auto& [key, g] : gpu) {
                destroy(g.solid);
                destroy(g.lines);
                if(bgfx::isValid(g.probeLight))bgfx::destroy(g.probeLight);
                if (bgfx::isValid(g.normals))
                    bgfx::destroy(g.normals);
                if (bgfx::isValid(g.instances))
                    bgfx::destroy(g.instances);
            }
            for (auto& mesh : atomSpheres)
                destroy(mesh);
            destroy(ringMesh);
            destroy(cabin);
            destroy(cabinGlazing);
            destroy(cabinYoke);
            destroy(cabinThrottle);
            destroy(cabinHatch);
            destroy(shipDeckMesh);
            destroy(dockingDeckMesh);
            destroy(dockingGlassMesh);
            destroy(airlockDoorMesh);
            destroy(airlockSillMesh);
            destroy(airlockIndicatorMesh);
            destroy(charterCrateMesh);
            destroy(probeHousing);destroy(probeCutter);destroy(probeLamps);
            destroy(probeRoller);destroy(probeDrive);destroy(probeActuator);destroy(probeHotTeeth);
            destroy(probeRod);
            destroy(hyperionBillboardMesh);
            destroy(pointLightBillboardMesh);
            for (auto& [key,station] : stationMeshes) {
                destroy(station.mesh);
                destroy(station.glass);
            }
            for (auto& role : craftMeshes)
                for (auto& mesh : role)
                    destroy(mesh);
            for (auto& role : surfaceMeshes)
                for (auto& mesh : role)
                    destroy(mesh);
            for (auto& role : workMeshes)
                for (auto& mesh : role)
                    destroy(mesh);
            hyperion.reset();
            if (bgfx::isValid(colorProgram))
                bgfx::destroy(colorProgram);
            bgfx::shutdown();
        }
    }
    GPUMesh upload(const Mesh& m, std::span<const uint32_t> colors = {}) {
        GPUMesh result;
        if (m.vertices.empty() || m.indexCount() == 0)
            return result;
        stats.uploadBytes += m.bytes();
        staticUploadBytes += m.bytes();
        const auto memory = bgfx::copy(m.vertices.data(), uint32_t(m.vertices.size() * sizeof(Vertex)));
        if (colors.size() == m.vertices.size()) {
            auto vertices = reinterpret_cast<Vertex*>(memory->data);
            for (size_t i = 0; i < colors.size(); ++i)
                vertices[i].color = colors[i];
        }
        result.vertices = bgfx::createVertexBuffer(memory, layout);
        result.indices = bgfx::createIndexBuffer(
            bgfx::copy(m.indexData(), static_cast<uint32_t>(m.indexCount() * m.indexWidth())),
            m.wideIndices() ? BGFX_BUFFER_INDEX32 : 0);
        result.count = m.indexCount();
        return result;
    }
    glm::dvec3 cameraSpace(glm::dvec3 p) const {
        return cameraInverse * (p - eye);
    }
    std::optional<glm::dvec2> project(glm::dvec3 p) const {
        auto clip = projection * view * glm::dvec4(p, 1);
        if (clip.w <= .0001)
            return {};
        auto q = glm::dvec3(clip) / clip.w;
        if (std::abs(q.x) > 1.2 || std::abs(q.y) > 1.2)
            return {};
        return glm::dvec2((q.x * .5 + .5) * width, (.5 - q.y * .5) * height);
    }
    const std::vector<LocalFrame>& framesFor(const Address& address) {
        auto it = frameFrames.find(address);
        if (it != frameFrames.end())
            return it->second;
        return frameFrames.emplace(address, universe.frames(address)).first->second;
    }
    Pose poseFor(const Address& address, const Navigation& nav) {
        auto it = framePoses.find(address);
        if (it != framePoses.end())
            return it->second;
        auto pose = relativePose(framesFor(address), address, anchorFrames, nav.anchor, nav.logDistance);
        framePoses.emplace(address, pose);
        return pose;
    }
    Instance billboardInstance(glm::dvec3 p,double radius,glm::vec3 color,float opticalMaterial=0) const {
        const auto right=cameraQ*glm::dvec3(radius,0,0), up=cameraQ*glm::dvec3(0,radius,0);
        glm::mat4 transform(1);
        transform[0]=glm::vec4(glm::vec3(right),0);
        transform[1]=glm::vec4(glm::vec3(up),0);
        transform[3]=glm::vec4(glm::vec3(p),1);
        return {transform,glm::vec4(color,1.f+opticalMaterial)};
    }
    void addBillboard(glm::dvec3 p, double radius, glm::vec3 color, bool cosmic = false,
                      float opticalMaterial = 0, bool background = false) {
        ++stats.particles;
        if (background)
            color *= stellarExposure;
        const bool filteredPoint = pointLightFiltering && background && opticalMaterial == 0;
        if (filteredPoint) {
            const double depth = -cameraSpace(p).z;
            if (depth > 0 && radius > 0) {
                const double radiusPixels = radius * height * .5 * projection[1][1] / depth;
                const auto footprint = pointLightFootprint(radiusPixels);
                radius *= footprint.radiusScale;
                color *= float(footprint.radianceScale);
            }
        }
        (filteredPoint          ? pointLightBillboards
         : opticalMaterial >= 3 ? stellarBillboards
         : cosmic               ? cosmicBillboards
                                : billboards)
            .push_back(billboardInstance(p,radius,color,opticalMaterial));
    }
    bool prepareGPU(const std::shared_ptr<const Geometry>& geometry) {
        if (!geometry)
            return false;
        auto it = gpu.find(geometry.get());
        if (it != gpu.end() && it->second.source.expired()) {
            gpuBytes -= it->second.bytes;
            destroy(it->second.solid);
            destroy(it->second.lines);
            if(bgfx::isValid(it->second.probeLight))bgfx::destroy(it->second.probeLight);
            if (it->second.normalWork)
                it->second.normalWork->cancel.store(true);
            if (bgfx::isValid(it->second.normals))
                bgfx::destroy(it->second.normals);
            if (bgfx::isValid(it->second.instances))
                bgfx::destroy(it->second.instances);
            gpu.erase(it);
            it = gpu.end();
        }
        if (it != gpu.end())
            return true;
        size_t bytes = geometry->solid.bytes() + geometry->lines.bytes() +
                       geometry->atomInstances.size() * sizeof(Instance);
        const bool sectionNormals = geometry->section &&
            geometry->terrainNormals.size() == geometry->solid.vertices.size();
        if (sectionNormals)
            bytes += geometry->terrainNormals.size() * sizeof(ShadingNormal);
        if (staticUploadBytes && staticUploadBytes + bytes > 2 * 1024 * 1024) {
            ++stats.uploadDeferred;
            return false;
        }
        GPUGeometry item;
        item.solid = upload(geometry->solid, geometry->exposureColors);
        item.lines = upload(geometry->lines);
        if (sectionNormals && !geometry->terrainNormals.empty()) {
            // Density queries ran in the existing section-generation worker.
            // Only a bounded four-byte normal stream is packed/uploaded here.
            std::vector<ShadingNormal> normals;
            normals.reserve(geometry->terrainNormals.size());
            for (const auto& normal : geometry->terrainNormals)
                normals.push_back(packNormal(normal));
            const auto count = uint32_t(normals.size() * sizeof(ShadingNormal));
            item.normals = bgfx::createVertexBuffer(bgfx::copy(normals.data(), count), normalLayout);
            stats.uploadBytes += count;
            staticUploadBytes += count;
        }
        if (!geometry->atomInstances.empty()) {
            size_t count = geometry->atomInstances.size() * sizeof(Instance);
            item.instances = bgfx::createVertexBuffer(
                bgfx::copy(geometry->atomInstances.data(), static_cast<uint32_t>(count)), instanceLayout);
            stats.uploadBytes += count;
            staticUploadBytes += count;
        }
        item.source = geometry;
        item.bytes = bytes;
        gpuBytes += bytes;
        gpu.emplace(geometry.get(), std::move(item));
        return true;
    }
    void prepareNormals(GPUGeometry& item, const std::shared_ptr<const Geometry>& geometry) {
        // One bounded CPU task at a time. Keep the original flat-shaded mesh
        // visible until its read-only lighting stream is ready.
        const auto count = geometry->solid.vertices.size();
        if (bgfx::isValid(item.normals) || !count || count > 65536 || geometry->address.kind() >= Kind::Grain)
            return;
        if (item.normalWork && item.normalWork->ready.load(std::memory_order_acquire)) {
            auto& data = item.normalWork->result;
            const size_t bytes = data.size() * sizeof(ShadingNormal);
            if (bytes && staticUploadBytes + bytes <= 2 * 1024 * 1024) {
                item.normals =
                    bgfx::createVertexBuffer(bgfx::copy(data.data(), uint32_t(bytes)), normalLayout);
                item.bytes += bytes;
                gpuBytes += bytes;
                stats.uploadBytes += bytes;
                staticUploadBytes += bytes;
                item.normalWork.reset();
            }
        } else if (!item.normalWork && normalJobs.load() == 0) {
            auto work = item.normalWork = std::make_shared<NormalWork>();
            ++normalJobs;
            normalTasks.run([this, geometry, work] {
                try {
                    work->result = surfaceNormals(geometry->solid, &work->cancel);
                } catch (...) {
                    work->cancel.store(true);
                }
                work->ready.store(true, std::memory_order_release);
                --normalJobs;
            });
        }
    }
    void drawMesh(const GPUMesh& m, const glm::mat4& model, float opacity = 1, bool lines = false,
                  bgfx::VertexBufferHandle normals = BGFX_INVALID_HANDLE, bool stellar = false,glm::vec3 tint=glm::vec3(1)) {
        if (!bgfx::isValid(m.vertices) || !bgfx::isValid(m.indices) ||
            bgfx::getAvailInstanceDataBuffer(1, 80) < 1)
            return;
        bgfx::InstanceDataBuffer idb;
        bgfx::allocInstanceDataBuffer(&idb, 1, 80);
        Instance item{model, glm::vec4(tint,opacity)};
        std::memcpy(idb.data, &item, sizeof(item));
        bgfx::setVertexBuffer(0, m.vertices);
        bgfx::setIndexBuffer(m.indices);
        const bool smooth = bgfx::isValid(normals);
        if (smooth)
            bgfx::setVertexBuffer(1, normals);
        bgfx::setInstanceDataBuffer(&idb);
        uint64_t state =
            BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_GREATER | BGFX_STATE_MSAA;
        if (lines)
            state |= BGFX_STATE_PT_LINES;
        if (opacity > .99f)
            state |= BGFX_STATE_WRITE_Z;
        else
            state |= BGFX_STATE_BLEND_ALPHA;
        bgfx::setState(state);
        bgfx::submit(worldView(), hyperion->geometryProgram(smooth, stellar));
    }
    void drawLines(const GPUMesh& m, const glm::mat4& model) {
        if (!bgfx::isValid(m.vertices))
            return;
        bgfx::setTransform(glm::value_ptr(model));
        bgfx::setVertexBuffer(0, m.vertices);
        bgfx::setIndexBuffer(m.indices);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_PT_LINES | BGFX_STATE_DEPTH_TEST_GEQUAL |
                       BGFX_STATE_MSAA);
        bgfx::submit(worldView(), colorProgram);
    }
    void transient(const Mesh& mesh, bgfx::ViewId viewId, uint64_t state, const glm::mat4* model = nullptr) {
        if (mesh.vertices.empty())
            return;
        uint32_t nv = static_cast<uint32_t>(mesh.vertices.size()),
                 ni = static_cast<uint32_t>(mesh.indexCount());
        if (bgfx::getAvailTransientVertexBuffer(nv, layout) < nv ||
            bgfx::getAvailTransientIndexBuffer(ni, mesh.wideIndices()) < ni)
            return;
        bgfx::TransientVertexBuffer vb;
        bgfx::TransientIndexBuffer ib;
        bgfx::allocTransientVertexBuffer(&vb, nv, layout);
        bgfx::allocTransientIndexBuffer(&ib, ni, mesh.wideIndices());
        std::memcpy(vb.data, mesh.vertices.data(), nv * sizeof(Vertex));
        std::memcpy(ib.data, mesh.indexData(), ni * mesh.indexWidth());
        bgfx::setVertexBuffer(0, &vb);
        bgfx::setIndexBuffer(&ib);
        if (model)
            bgfx::setTransform(glm::value_ptr(*model));
        bgfx::setState(state);
        bgfx::submit(viewId, colorProgram);
    }
    void transient(const Mesh& mesh, bgfx::ViewId viewId, bool lines, bool additive, bool depth = true,
                   bool writeDepth = false, bool alpha = false) {
        uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_MSAA;
        if (lines)
            state |= BGFX_STATE_PT_LINES | (depth ? BGFX_STATE_DEPTH_TEST_GEQUAL : 0);
        else if (depth)
            state |= BGFX_STATE_DEPTH_TEST_GEQUAL;
        if (writeDepth)
            state |= BGFX_STATE_WRITE_Z;
        if (additive)
            state |= BGFX_STATE_BLEND_ADD;
        else if (!depth || alpha)
            state |= BGFX_STATE_BLEND_ALPHA;
        transient(mesh, viewId, state);
    }
    void bindPresentation(const Geometry& geometry, const Node& node, const Navigation& nav,
                          const glm::mat4& model, const Pose& pose, double radius, float primitive) {
        auto presentation = extractPresentation(node, geometry, mode);
        const auto& sourceFrames = framesFor(geometry.referenceFrame);
        double sourceFactor = std::pow(10.0, geometry.logUnits - sourceFrames.back().logRadius);
        // A patch and its enclosing section share the same authored material
        // frame. Sampling windows must never appear as coloured rectangles.
        const auto& materialAddress = presentation.materialFrame;
        if (materialAddress.kind() == Kind::Planet && node.address != materialAddress)
            describePlanetPresentation(presentation, universe.describe(materialAddress));
        if (materialAddress.kind() == Kind::Planet)
            presentation.planet[4] =
                glm::vec4(glm::vec3(glm::inverse(poseFor(materialAddress, nav).orientation) *
                                    lightAt(materialAddress, nav)),
                          0);
        if (!planetRings)
            presentation.planetRings = {};
        const auto& materialFrames = framesFor(materialAddress);
        auto transform = pointTransform(sourceFrames, geometry.referenceFrame, materialFrames,
                                        materialAddress, materialFrames.back().logRadius);
        glm::dmat4 mapping(1);
        auto linear = transform.linear * sourceFactor;
        for (unsigned i = 0; i < 3; ++i)
            mapping[i] = glm::dvec4(linear[i], 0);
        mapping[3] = glm::dvec4(transform.translation - transform.linear * transform.pivot, 1);
        auto pattern = glm::mat4(mapping);
        if (primitive == 1) {
            auto localEye = glm::inverse(pose.orientation) * (eye - pose.position) / radius;
            const auto light = glm::inverse(pose.orientation) * lightAt(materialAddress, nav);
            hyperion->bind(presentation, glm::vec3(localEye), glm::vec3(light), pattern, primitive,
                           visualTime);
        } else {
            auto light = lightAt(materialAddress, nav);
            glm::mat4 micro(0);
            float relief = 0;
            if (node.address.kind() >= Kind::Planet) {
                const double materialRadius = std::pow(10., materialFrames.back().logRadius);
                const auto surface = geometry.section && geometry.referenceFrame.kind() == Kind::Grain
                    ? universe.describe(geometry.referenceFrame) : node;
                const double spacing = materialDetailSpacing(materialAddress.kind(), materialRadius,
                    surface.address.kind(), std::pow(10., surface.frame.logRadius));
                const auto localEye = glm::inverse(pose.orientation) * (eye - pose.position) / radius;
                const auto canonical = mapping * glm::dvec4(localEye, 1);
                const auto materialPose = poseFor(materialAddress, nav);
                const glm::dmat3 linear = glm::mat3_cast(glm::inverse(materialPose.orientation)) *
                                          (std::pow(10., nav.logDistance) / spacing);
                for (unsigned i = 0; i < 3; i++)
                    micro[i] = glm::vec4(glm::vec3(linear[i]), 0);
                micro[3] = glm::vec4(
                    glm::vec3(glm::mod(glm::dvec3(canonical) * (materialRadius / spacing), glm::dvec3(64))),
                    1);
                relief = float(spacing * .045 / std::pow(10., nav.logDistance));
            }
            hyperion->bind(presentation, glm::vec3(eye), glm::vec3(light), pattern * glm::inverse(model),
                           primitive, visualTime, {}, micro, relief);
        }
    }
    glm::dvec3 lightAt(const Address& material, const Navigation& nav) {
        if (instruments || (material.kind() >= Kind::Planet && material.kind() <= Kind::Mountain)) {
            auto system = material;
            while (system.kind() > Kind::System)
                system = system.parent();
            if (system.kind() == Kind::System && material.kind() > Kind::System) {
                const auto vector = poseFor(system, nav).position - poseFor(material, nav).position;
                if (glm::length(vector) > 1e-30)
                    return glm::normalize(vector);
            }
        }
        return poseFor(material, nav).orientation * glm::dvec3(.6, .7, -.4);
    }
    void drawGeometry(const std::shared_ptr<const Geometry>& geometry, const Node& node,
                      const Navigation& nav, float opacity) {
        if (!geometry || opacity < .001f)
            return;
        auto pose = poseFor(geometry->referenceFrame, nav);
        double r = std::pow(10.0, geometry->logUnits - nav.logDistance);
        if (geometry->section && geometry->inspectionRadius > 0) {
            opacity *= sectionCoverage(glm::length(eye - pose.position), geometry->inspectionRadius * r);
            if (opacity < .001f)
                return;
        }
        if (!prepareGPU(geometry))
            return;
        auto& item = gpu.at(geometry.get());
        item.lastUsed = frame;
        prepareNormals(item, geometry);
        stats.vertices += geometry->solid.vertices.size();
        stats.triangles += geometry->solid.indexCount() / 3;
        auto model = glm::translate(glm::mat4(1), glm::vec3(pose.position)) *
                     glm::mat4_cast(glm::quat(pose.orientation)) *
                     glm::scale(glm::mat4(1), glm::vec3(static_cast<float>(r)));
        if (bgfx::isValid(item.solid.vertices))
            bindPresentation(*geometry, node, nav, model, pose, r, bgfx::isValid(item.normals) ? .25f : 0.f);
        const auto wearTint=economy?prospectWearTint(universe,node,economy->exploration):glm::vec3(1);
        drawMesh(item.solid, model, opacity, false, item.normals, node.address.kind() == Kind::System,wearTint);
        if (bgfx::isValid(item.lines.vertices))
            bindPresentation(*geometry, node, nav, model, pose, r, 2);
        float lineOpacity=opacity;
        if(!geometry->lattice.bonds.empty()){
            const auto bond=geometry->lattice.bonds.front();
            const double span=glm::length(geometry->lattice.atoms[bond.a].position-
                                          geometry->lattice.atoms[bond.b].position)*r;
            const double depth=std::max(worldNearPlane,-cameraSpace(pose.position).z);
            lineOpacity*=latticeBondOpacity(LOD::pixels(span*.5,depth,height,fieldOfView));
        }
        if(lineOpacity>.001f)drawMesh(item.lines, model, lineOpacity, true);
        const auto& sourceFrames = framesFor(geometry->referenceFrame);
        double sourceFactor = std::pow(10.0, geometry->logUnits - sourceFrames.back().logRadius);
        auto transform =
            pointTransform(sourceFrames, geometry->referenceFrame, anchorFrames, nav.anchor, nav.logDistance);
        std::unordered_set<uint64_t> represented;
        if (geometry->address.kind() == Kind::Galaxy) {
            if (geometry->address.ancestorOf(nav.anchor) &&
                nav.anchor.path.size() > geometry->address.path.size())
                represented.insert(nav.anchor.path[geometry->address.path.size()].id);
            for (auto& [address, state] : details)
                if (address.kind() == Kind::System && address.parent() == geometry->address &&
                    state.used + 1 >= frame && state.blend.current)
                    represented.insert(address.id());
        }
        size_t particleIndex = 0;
        for (const auto& particle : geometry->particles) {
            bool skip = false;
            if (geometry->address.kind() == Kind::Galaxy && particle.objectId != UINT64_MAX) {
                skip = represented.contains(particle.objectId);
            }
            // The addressed stellar proxy below is the same emitter at all scales.
            if (geometry->address.kind() == Kind::System && particleIndex == 0)
                skip = true;
            ++particleIndex;
            if (skip)
                continue;
            auto position = transform(particle.position * sourceFactor);
            double distanceScale = 1;
            float attenuation = 1;
            if (geometry->address.kind() == Kind::Cosmic) {
                const double distance = glm::length(position - eye);
                const double rootRadius = std::pow(10., rootCellLogMetres - nav.logDistance);
                attenuation =
                    float((1 - smoothstep(1.2, 1.9, distance / rootRadius)) / double(geometry->detail + 1));
                if (attenuation < .001f)
                    continue;
                // Preserve the actual emitter's direction/angular size beyond
                // the local depth range. This is the streamed scene, not a sky texture.
                if (distance > 1e-12) {
                    distanceScale = 500 / distance;
                    position = eye + (position - eye) * distanceScale;
                }
            }
            auto p = cameraSpace(position);
            if (p.z >= 0)
                continue;
            const bool stellar = geometry->address.kind() == Kind::System &&
                                 glm::dot(particle.position, particle.position) < 1e-24;
            double size = std::clamp(static_cast<double>(particle.radius) * r * distanceScale, -p.z * .0005,
                                     -p.z * (stellar ? .8 : .035));
            if (LOD::visible(p, size, static_cast<double>(width) / height, fieldOfView))
                addBillboard(position, size, particle.color * (opacity * attenuation),
                             geometry->address.kind() == Kind::Cosmic,
                             stellar ? 3.f + hyperionPhase(node.seed) * .25f
                             : geometry->address.kind() == Kind::Cosmic ? 2.f
                             : geometry->address.kind() == Kind::Galaxy && particle.objectId == UINT64_MAX
                                 ? 1.f
                                 : 0.f,
                             geometry->address.kind() <= Kind::Galaxy);
        }
        if (!geometry->lattice.atoms.empty() && bgfx::isValid(item.instances) && atomView < 64) {
            double diameter =
                LOD::pixels(r * geometry->lattice.atoms.front().radius,
                            std::max(.05, glm::length(cameraSpace(pose.position)) - r), height, 1);
            unsigned level = atomPresentationLevel(diameter, false);
            unsigned rings = level == 3   ? 24
                             : level == 2 ? 12
                             : level == 1 ? 8
                                          : 4,
                     sectors = level == 3   ? 40
                               : level == 2 ? 20
                               : level == 1 ? 12
                                            : 6;
            size_t count = geometry->lattice.atoms.size();
            auto localView = glm::mat4(view) * model;
            glm::mat4 proj(projection);
            auto viewId = atomView++;
            bgfx::setViewTransform(viewId, glm::value_ptr(localView), glm::value_ptr(proj));
            bgfx::setViewRect(viewId, 0, 0, static_cast<uint16_t>(width), static_cast<uint16_t>(height));
            bgfx::setVertexBuffer(0, atomSpheres[level].vertices);
            bgfx::setIndexBuffer(atomSpheres[level].indices);
            bgfx::setInstanceDataBuffer(item.instances, 0, static_cast<uint32_t>(count));
            uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_GREATER | BGFX_STATE_MSAA;
            if (opacity > .99f)
                state |= BGFX_STATE_WRITE_Z;
            else
                state |= BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_FACTOR, BGFX_STATE_BLEND_INV_FACTOR);
            bgfx::setState(state, rgba(glm::vec3(opacity), opacity));

            bindPresentation(*geometry, node, nav, model, pose, r, 1);
            bgfx::submit(viewId, hyperion->geometryProgram());
            stats.atoms += count;
            stats.atomTriangles += count * rings * sectors * 2;
        }
    }
    glm::dvec3 wormholePosition(const XalariaWormholeMouth& mouth,const Navigation& nav) const {
        const double referenceUnits=std::pow(10.,universe.describe(mouth.reference).frame.logRadius);
        return universe.pointRelative(mouth.reference,mouth.metres/referenceUnits,nav.anchor,nav.logDistance);
    }
    void drawWormhole(const Navigation& nav) {
        if (!xalariaMap || !xalariaMap->wormhole || exteriorHidden || specimen || probeRemote)
            return;
        const auto& mouth = *xalariaMap->wormhole;
        const double radius = mouth.radius / std::pow(10., nav.logDistance);
        const auto centre = wormholePosition(mouth,nav)-eye;
        const auto cameraCentre = cameraInverse * centre;
        const double range = glm::length(centre);
        const double cullScale = 500. / std::max(range, radius);
        if (range > worldFarPlane * .8 || LOD::pixels(radius, range, height, fieldOfView) < 1 ||
            !LOD::visible(cameraCentre * cullScale, radius * (1.65 * cullScale), double(width) / height, fieldOfView))
            return;
        const auto orientation = universe.relative(mouth.reference, nav.anchor, nav.logDistance).orientation *
                                 mouth.orientation;
        if (bgfx::getAvailInstanceDataBuffer(2, 80) < 2)
            return;
        const auto right = orientation * glm::dvec3(1, 0, 0);
        const auto up = orientation * glm::dvec3(0, 1, 0);
        const auto normal = orientation * glm::dvec3(0, 0, 1);
        const auto plasma = glm::vec3(1.f,.34f,.075f) * (1.f-regionalLook.x-regionalLook.y-regionalLook.z)
                          + glm::vec3(.30f,.78f,.31f)*regionalLook.x
                          + glm::vec3(.19f,.57f,.94f)*regionalLook.y
                          + glm::vec3(.92f,.21f,.56f)*regionalLook.z;
        const bool opticalRim = hyperion->wormholeOptics(glm::vec3(cameraCentre),
            glm::vec3(cameraInverse * right), glm::vec3(cameraInverse * up), float(radius), plasma,
            float(std::tan(fieldOfView * .5)), visualTime);
        glm::mat4 rim(1);
        rim[0] = glm::vec4(glm::vec3(right * (radius * 1.32)), 0);
        rim[1] = glm::vec4(glm::vec3(up * (radius * 1.32)), 0);
        rim[2] = glm::vec4(glm::vec3(normal), 0);
        rim[3] = glm::vec4(glm::vec3(centre), 1);
        auto throat = rim;
        throat[3] += glm::vec4(glm::vec3(normal*std::copysign(radius*.012,glm::dot(centre,normal))),0);
        const std::array<Instance,2> layers{{{throat,{plasma,1}}, {rim,{plasma,2}}}};
        bgfx::InstanceDataBuffer buffer;
        bgfx::allocInstanceDataBuffer(&buffer, uint32_t(layers.size()), 80);
        std::memcpy(buffer.data,layers.data(),sizeof(layers));

        const glm::mat4 view = glm::mat4_cast(glm::quat(cameraInverse)), proj(projection);
        bgfx::setViewMode(66, bgfx::ViewMode::Sequential);
        bgfx::setViewTransform(66, glm::value_ptr(view), glm::value_ptr(proj));
        bgfx::setViewRect(66, 0, 0, uint16_t(width), uint16_t(height));
        const Geometry geometry;
        const ScenePresentation material{geometry,mouth.reference,{},hyperionPhase(universe.seed()),1.f,{}};
        const auto program = hyperion->wormholeProgram();
        // The physical throat writes depth. Readable-depth views draw the rim
        // during optical resolve, where it can bend the scene behind the gate.
        const uint32_t layerCount = opticalRim ? 1 : uint32_t(layers.size());
        for (uint32_t layer = 0; layer < layerCount; ++layer) {
            hyperion->bind(material, {}, {0,0,1}, glm::mat4(1), 0, visualTime);
            bgfx::setVertexBuffer(0, hyperionBillboardMesh.vertices);
            bgfx::setIndexBuffer(hyperionBillboardMesh.indices);
            bgfx::setInstanceDataBuffer(&buffer,layer,1);
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_MSAA | BGFX_STATE_DEPTH_TEST_GEQUAL |
                           (layer == 0 ? BGFX_STATE_WRITE_Z : BGFX_STATE_BLEND_ADD));
            bgfx::submit(66, program);
        }
        stats.uploadBytes += sizeof(layers);
    }
    void queuePlanetRing(const Node& planet, const Pose& globe, glm::dvec3 cameraCentre, float opacity) {
        if (!planetRings || specimen || !bgfx::isValid(ringMesh.vertices))
            return;
        auto traits = planetTraits(planet);
        if (!traits.rings.present())
            return;
        const auto ring = planetRingPose(globe, traits.rings);
        const double distance = glm::length(cameraCentre);
        if (distance - ring.scale > worldFarPlane ||
            LOD::pixels(ring.scale, distance, height, fieldOfView) < .7 ||
            !LOD::visible(cameraCentre, ring.scale, double(width) / height, fieldOfView))
            return;
        visibleRings.push_back({planet.address, std::move(traits), ring, distance, opacity});
    }
    void drawPlanetRings(const Navigation& nav) {
        if (visibleRings.empty())
            return;
        std::stable_sort(visibleRings.begin(), visibleRings.end(),
                         [](const VisibleRing& a, const VisibleRing& b) { return a.distance > b.distance; });
        const auto viewId = hyperion->planetRingView();
        const glm::mat4 view = glm::mat4_cast(glm::quat(cameraInverse)), proj(projection);
        bgfx::setViewMode(viewId, bgfx::ViewMode::Sequential);
        bgfx::setViewTransform(viewId, glm::value_ptr(view), glm::value_ptr(proj));
        bgfx::setViewRect(viewId, 0, 0, uint16_t(width), uint16_t(height));
        constexpr uint64_t coverageBlend = BGFX_STATE_BLEND_FUNC_SEPARATE(
            BGFX_STATE_BLEND_SRC_ALPHA, BGFX_STATE_BLEND_INV_SRC_ALPHA,
            BGFX_STATE_BLEND_ONE, BGFX_STATE_BLEND_INV_SRC_ALPHA);
        for (const auto& ring : visibleRings) {
            const auto centre = ring.pose.position - eye;
            const glm::mat4 model = glm::translate(glm::dmat4(1), centre) * glm::mat4_cast(ring.pose.orientation) *
                                    glm::scale(glm::dmat4(1), glm::dvec3(ring.pose.scale));
            const auto fromScene = glm::inverse(ring.pose.orientation);
            const double globeScale = ring.pose.scale / ring.traits.rings.outerRadius;
            const auto localEye = fromScene * (-centre / globeScale);
            const auto light = fromScene * lightAt(ring.planet, nav);
            const auto program = hyperion->ringProgram(ring.traits, glm::vec3(light), glm::vec3(localEye),
                                                       ring.opacity, float(std::log10(globeScale) + nav.logDistance));
            bgfx::setTransform(glm::value_ptr(model));
            bgfx::setVertexBuffer(0, ringMesh.vertices);
            bgfx::setIndexBuffer(ringMesh.indices);
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_GEQUAL | BGFX_STATE_MSAA |
                           (viewId == ringLayerViewId ? BGFX_STATE_WRITE_A | coverageBlend : BGFX_STATE_BLEND_ALPHA));
            bgfx::submit(viewId, program);
            ++stats.planetRings;
        }
    }
    void drawBlend(DetailState& state, const Node& node, const Navigation& nav, float opacity) {
        if (state.blend.active())
            ++stats.lodTransitions;
        drawGeometry(state.blend.previous, node, nav, opacity * (1 - state.blend.weight()));
        drawGeometry(state.blend.current, node, nav, opacity * state.blend.weight());
    }
    void stellarSky(const Node& galaxy, const Navigation& nav, RegionCache& cache) {
        if(exteriorHidden)return;
        if (!instruments)
            return;
        // Share the ordinary galaxy's fade interval so magnification never
        // removes its stars before a smaller reference frame becomes ready.
        const double radius = std::pow(10., galaxy.frame.logRadius - nav.logDistance);
        if (radius <= 3)
            return;
        const float handoff = float(smoothstep(3., 12., radius));
        // Refine the same addressed stellar population asynchronously. A pixel
        // floor, like Hyperion skyFragment's derivative-filtered points, keeps
        // unresolved emitters from disappearing between MSAA samples.
        auto geometry = cache.request(galaxy, 4, 6);
        if (!geometry)
            return;
        skyGeometry = geometry;
        const auto galaxyEye = universe.pointRelative(
            nav.anchor, eye * std::pow(10., nav.logDistance - anchorFrames.back().logRadius), galaxy.address,
            galaxy.frame.logRadius);
        const auto orientation = poseFor(galaxy.address, nav).orientation;
        const double factor = std::pow(10., geometry->logUnits - galaxy.frame.logRadius);
        uint64_t active = UINT64_MAX;
        if (nav.anchor.path.size() > galaxy.address.path.size())
            active = nav.anchor.path[galaxy.address.path.size()].id;
        for (const auto& star : geometry->particles) {
            if (star.objectId == active)
                continue;
            const auto delta = orientation * (star.position * factor - galaxyEye);
            const double distance = glm::length(delta);
            if (distance < 1e-15)
                continue;
            const auto direction = delta / distance;
            const auto local = cameraInverse * direction;
            if (local.z >= 0)
                continue;
            const bool gas = star.objectId == UINT64_MAX;
            const double pixelAngle = 2 * std::tan(fieldOfView * .5) / height;
            const double angular = gas ? std::clamp(double(star.radius) * factor / distance, .004, .04)
                                       : std::clamp(double(star.radius) * factor / distance,
                                                    pixelAngle * 1.05, pixelAngle * 2.7);
            const double size = 500 * angular;
            if (LOD::visible(local * 500., size, double(width) / height, fieldOfView)) {
                const double physicalDistance = distance * std::pow(10., galaxy.frame.logRadius);
                if (celestialOccluded(local, physicalDistance, skyOccluders))
                    continue;
                const auto point = eye + direction * 500.;
                addBillboard(point, size, star.color * (handoff * (gas ? .5f : 1.36f)), true, gas ? 1.f : 0.f,
                             true);
                if (gas)
                    ++stats.skyGas;
                else {
                    ++stats.skyStars;
                    if (auto screen = project(point))
                        picks.push_back(
                            {galaxy.address.child(Kind::System, star.objectId), *screen, physicalDistance});
                }
            }
        }
    }
    void galaxySky(const Node& root, const Navigation& nav, RegionCache& cache, double exposure) {
        if(exteriorHidden)return;
        if (!instruments || exposure < .0001)
            return;
        const auto catalog = cache.catalog(root);
        if (!catalog)
            return;
        const auto rootEye = universe.pointRelative(
            nav.anchor, eye * std::pow(10., nav.logDistance - anchorFrames.back().logRadius), root.address,
            root.frame.logRadius);
        const auto orientation = poseFor(root.address, nav).orientation;
        const double pixelAngle = 2 * std::tan(fieldOfView * .5) / height;
        for (const auto& galaxy : *catalog) {
            if (galaxy.address.ancestorOf(nav.anchor))
                continue;
            const auto delta = orientation * (galaxy.frame.position - rootEye);
            const double distance = glm::length(delta);
            if (distance <= 1e-15)
                continue;
            const auto direction = delta / distance;
            const auto local = cameraInverse * direction;
            if (local.z >= 0 ||
                celestialOccluded(local, distance * std::pow(10., root.frame.logRadius), skyOccluders))
                continue;
            const double angular =
                std::clamp(std::pow(10., galaxy.frame.logRadius - root.frame.logRadius) / distance,
                           pixelAngle * .85, pixelAngle * 2.);
            if (LOD::visible(local * 500., angular * 500., double(width) / height, fieldOfView)) {
                const auto point = eye + direction * 500.;
                const glm::vec3 tint = galaxy.galaxy.type == GalaxyType::Elliptical
                                           ? glm::vec3(.65, .49, .32)
                                           : glm::vec3(.34, .48, .68);
                addBillboard(point, angular * 500., tint * float(exposure), true, 0, true);
                if (auto screen = project(point))
                    picks.push_back(
                        {galaxy.address, *screen, distance * std::pow(10., root.frame.logRadius)});
                ++stats.skyGalaxies;
            }
        }
    }
#include "SurfaceRendering.hpp"
    void renderLanding(const Navigation& nav, const std::optional<Node>& planet) {
        if (!instruments || !planet || !terrainActive || exteriorHidden || probeRemote || specimen)
            return;
        const auto& landing = instruments->landing;
        if (landing.body != planet->address)
            return;
        const auto up = landing.direction;
        const auto tangent = landing.forward - up * glm::dot(landing.forward, up);
        const auto ahead = glm::length(tangent) > .01 ? glm::normalize(tangent) :
                           glm::normalize(glm::cross(up, std::abs(up.y) < .8 ? glm::dvec3(0, 1, 0) :
                                                                                            glm::dvec3(1, 0, 0)));
        const auto right = glm::cross(ahead, up);
        const double radius = std::pow(10., planet->frame.logRadius);
        const double units = std::pow(10., nav.logDistance);
        const double size = instruments->hullMetres / 12.;
        const bool settled = landing.phase == LandingPhase::Landed;
        const bool flare = landing.phase == LandingPhase::Flare;
        const auto planetEye = universe.pointRelative(
            nav.anchor, eye * std::pow(10., nav.logDistance - anchorFrames.back().logRadius),
            planet->address, 0);
        const auto orientation = poseFor(planet->address, nav).orientation;
        auto groundPoint = [&](glm::dvec2 p) {
            const auto n = glm::normalize(up + (right * p.x + ahead * p.y) * (size / radius));
            return orientation *
                   (n * (radius + universe.terrainHeight(*planet, n) + (settled ? .85 : .46) * size) -
                    planetEye) / units;
        };
        Mesh light;
        auto strip = [&](glm::dvec3 a, glm::dvec3 b, glm::dvec3 across, double width, glm::vec3 color) {
            const auto edge = orientation * across * (width * size / units);
            const auto first = uint32_t(light.vertices.size());
            for (int i : {-1, 0, 1}) {
                const auto tint = rgba(i == 0 ? color : glm::vec3(0));
                for (auto p : {a + double(i) * edge, b + double(i) * edge})
                    light.vertices.push_back({float(p.x), float(p.y), float(p.z), tint});
            }
            for (uint32_t i : {0, 1, 2, 1, 3, 2, 2, 3, 4, 3, 5, 4})
                light.indices.push_back(first + i);
        };
        const glm::vec3 violet(.71f, .20f, .96f), cyan(.12f, .86f, 1.f), mint(.33f, 1.f, .70f);
        const auto rim = settled || flare ? mint : cyan;
        // Four open corners keep the actual terrain and pad markings readable.
        const double ring = settled ? 20. + (1. - landing.settle) * 16. : 16.;
        for (int quadrant = 0; quadrant < 4; ++quadrant) {
            const double start = quadrant * std::numbers::pi / 2. - .57;
            glm::dvec3 previous{};
            for (int i = 0; i <= 6; ++i) {
                const double angle = start + i * .19;
                const glm::dvec2 direction(std::cos(angle), std::sin(angle));
                const auto point = groundPoint(direction * ring);
                if (i != 0) {
                    const float gain = settled ? float(landing.settle * landing.settle) :
                                       flare ? .92f : .66f;
                    const auto outward = right * direction.x + ahead * direction.y;
                    strip(previous, point, outward, settled ? 1.5 : .74, rim * gain);
                    strip(previous, point, outward, .28, rim * gain);
                }
                previous = point;
            }
        }
        if (!settled) {
            // Chase the lights toward the footprint. The forward section is
            // visible through a level cockpit during the vertical final.
            for (int row = 0; row < 7; ++row) {
                const double along = 22. + 12. * row + 1.35 * row * row;
                const double spread = 8. + 1.2 * row;
                const double wave = .5 + .5 * std::cos(instruments->flightSeconds * 6. + row * .85);
                const auto color = glm::mix(rim, violet, float(row) / 9.) *
                                   float((.54 + .46 * wave * wave) * (flare ? .75 : 1.));
                for (double side : {-1., 1.}) {
                    const auto a = groundPoint({side * spread, along});
                    const auto b = groundPoint({side * (spread + 2.8), along + 7.});
                    strip(a, b, right, 1.35, color * .68f);
                    strip(a, b, right, .45, color);
                }
            }
        }
        const auto view = glm::mat4_cast(glm::quat(cameraInverse));
        const glm::mat4 proj(projection);
        bgfx::setViewTransform(62, glm::value_ptr(view), glm::value_ptr(proj));
        bgfx::setViewRect(62, 0, 0, uint16_t(width), uint16_t(height));
        bgfx::setTransform(glm::value_ptr(glm::mat4(1)));
        stats.landingGuideVertices = light.vertices.size();
        transient(light, 62, false, true);
    }
    void shieldEnvelope(glm::dvec3 centre,glm::dquat axes,glm::dvec3 radii,const ShieldReadout& shield,
                        float outline,float flash) {
        if(outline<=.002f&&flash<=.002f)return;
        static const Mesh envelope=[]{
            Mesh sphere;
            appendSphere(sphere,{},1.,glm::vec3(1),24,40,true);
            return sphere;
        }();
        const auto localEye=glm::conjugate(axes)*(-centre);
        const double inside=1.-smoothstep(.94,1.12,glm::length(localEye/radii));
        const auto start=uint32_t(shieldFields.vertices.size());
        const double wave=.09+shield.age*2.18;
        for(const auto& seed:envelope.vertices){
            const glm::dvec3 direction(seed.x,seed.y,seed.z);
            const auto local=direction*radii;
            const auto toEye=localEye-local;
            const auto normal=glm::normalize(direction/radii);
            const double facing=glm::dot(normal,toEye)/std::max(1e-12,glm::length(toEye));
            const double hemisphere=std::lerp(smoothstep(-.15,.015,facing),1.,inside);
            const double edgeOn=1.-std::min(1.,std::abs(facing));
            const double rim=.24*std::pow(edgeOn,2.4)+.60*std::pow(edgeOn,6.);
            const auto edge=shieldBlue*(outline*float((.017+rim)*hemisphere));
            glm::vec3 pulse(0);
            if(flash>.002f){
                const double angle=std::acos(std::clamp(glm::dot(direction,shield.impactDirection),-1.,1.));
                const double ridge=std::exp(-std::pow((angle-wave)/.135,2.));
                const double spread=std::exp(-std::pow(angle/(.22+shield.age*.86),2.));
                pulse=glm::vec3(.60f,.87f,1.f)*flash*float((.87*ridge+.35*spread)*hemisphere);
            }
            const auto point=centre+axes*local;
            shieldFields.vertices.push_back({float(point.x),float(point.y),float(point.z),rgba(edge+pulse)});
        }
        for(const auto i:envelope.indices)shieldFields.indices.push_back(start+i);
    }

    void renderTraffic(const ActivityId& station,TrafficService service,std::span<const TrafficCraft> traffic,
                       glm::dvec3 centre,glm::dquat orientation,glm::dvec3 planetEye,glm::dquat planetRotation,
                       double units,ActivityBatches& batches,Mesh& exhaust,const ActivityBody* overrideBody=nullptr) {
        const double lens=std::tan(fieldOfView*.5);
        for (size_t index=0;index<traffic.size();++index) {
            const auto& ship=traffic[index];
            auto contact=overrideBody?*overrideBody:xalariaTrafficBody(station,service,index,ship,centre,orientation);
            if(!overrideBody&&xalariaCombat&&xalariaCombat->strategy().suppresses(contact.id))continue;
            if(!overrideBody)xalariaEconomy->applyTrafficRecovery(contact,activity->seconds);
            const auto delta=planetRotation*(contact.position-planetEye);
            const auto rotation=planetRotation*contact.orientation;
            const auto id=contact.id;
            const bool distress=distressVessel==id;
            const double beaconAge=std::fmod(activity->seconds,2.4);
            const double signal=distress?(1.-smoothstep(.04,.19,std::min(std::abs(beaconAge-.18),
                                                                        std::abs(beaconAge-.63)))):0.;
            if(distress)contact.status="FREIGHTER / DISTRESS BEACON";
            const bool cargo=ship.kind==ActivityKind::Freighter;
            const bool patrol=ship.kind==ActivityKind::Patrol, survey=ship.kind==ActivityKind::Survey;
            const auto nozzle=[&](double side){
                return glm::dvec3(side*(cargo?.90:patrol?.65:.44),cargo?.045:patrol?-.03:0.,survey?1.44:1.52);
            };
            const double speed=glm::length(contact.velocity);
            if(speed>.18&&glm::length(delta)<100000.){
                auto& trail=trafficTrails[id];
                trail.nozzleWidth=.14+.012*ship.radius;
                const auto emitter=[&](double side){
                    return contact.position+contact.orientation*(nozzle(side)*ship.radius);
                };
                const double power=distress?.20+.80*signal:1.;
                const auto intensity=float(smoothstep(.18,7.,speed)*(.72+.28*ship.thrust)*power);
                trail.observe({id.home,{emitter(-1.),emitter(1.)},activity->seconds,intensity});
            }else if(auto found=trafficTrails.find(id);found!=trafficTrails.end())
                found->second.cut();
            const bool tracking=xalariaRadio&&xalariaRadio->dockingTarget==id&&instruments&&instruments->docking.active;
            const auto shield=xalariaEconomy->trafficShield(contact,activity->seconds,tracking||ship.phase==TrafficPhase::AtBerth);
            const double dockingHold=xalariaEconomy->trafficDockingHold(contact.id,activity->seconds);
            const double range=std::max(.01,glm::length(delta));
            const auto view=cameraInverse*delta/range;
            const double angular=ship.radius/range;
            // The lamp tips, exhaust and docking collar extend past the nominal
            // ship radius. A centre-point test would drop the entire ship mid-pass.
            const double extent=2.4*ship.radius+2.;
            const auto occluded=range>extent &&
                celestialOccluded(view,range-extent,skyOccluders,nullptr,std::asin(extent/range));
            std::optional<ActivityId> pick;
            if (range<trafficRadioRange) {
                const auto relativeVelocity=cameraInverse*(planetRotation*contact.velocity)-
                    (instruments ? glm::conjugate(instruments->gunAim*head.orientation)*instruments->localVelocity
                                 : glm::dvec3(0));
                if(tracking||!occluded){
                    const bool selected=tracking||(xalariaRadio&&xalariaRadio->selected==contact.id);
                    pick=contact.id;
                    activityContacts.push_back({std::move(contact),view,tracking?instruments->docking.range:range,
                                                glm::length(relativeVelocity),selected,occluded});
                }
            }
            const double pixels=angular*height/lens;
            const auto relative=delta/units;
            if(occluded||(!tracking&&(pixels<.22 ||
               (range>extent && !LOD::visible(cameraInverse*relative,extent/units,double(width)/height,fieldOfView)))))
                continue;
            if (pick&&view.z<0.)
                if(auto screen=project(eye+cameraQ*view*500.))
                    activityPicks.emplace_back(*pick,*screen);
            const double scale=ship.radius/units;
            const auto model=glm::translate(glm::dmat4(1),relative)*glm::mat4_cast(rotation)*
                             glm::scale(glm::dmat4(1),glm::dvec3(scale));
            // A single silhouette and finish from subpixel contact to clamp distance.
            batches[size_t(ship.kind)][2].push_back({glm::mat4(model),glm::vec4(ship.paint,1)});
            ++stats.activityVisible;
            if(shield.capacity>0.&&range<1800.&&pixels>3.){
                const float desired=shield.raised?1.f:0.f;
                auto& fade=shieldFades.try_emplace(id,ShieldFade{desired,frame}).first->second;
                fade.seen=frame;
                fade.visibility=std::lerp(fade.visibility,desired,float(-std::expm1(-frameSeconds/.105)));
                const float footprint=float(smoothstep(3.,16.,pixels)*(1.-smoothstep(1050.,1800.,range)));
                const glm::dvec3 radii=ship.kind==ActivityKind::Freighter?glm::dvec3(1.58,.78,2.10):
                    ship.kind==ActivityKind::Patrol?glm::dvec3(1.25,.66,1.81):glm::dvec3(1.39,.67,1.94);
                shieldEnvelope(relative,rotation,radii*scale,shield,
                               footprint*fade.visibility*float(std::sqrt(shield.fraction)),
                               footprint*float(shield.pulse));
            }
            const auto point=[&](glm::dvec3 local) {return relative+rotation*local*scale;};
            const float signalStrength=float(smoothstep(.22,1.08,pixels));
            const auto glow=[&](glm::dvec3 position,double radius,glm::vec3 colour) {
                // Filter very close lights before their billboards fill a window
                // or hit the near plane. The metal and emissive insets stay put.
                const double depth=-(cameraInverse*position).z;
                const float weight=signalStrength*float(smoothstep(worldNearPlane,worldNearPlane+radius,depth));
                if(weight>0.f)
                    trafficLights.push_back(billboardInstance(position,
                        std::min(radius,depth*lens*.075),weight*colour));
            };
            if(ship.radius>=minimumTrafficCollarRadius){
                const auto collar=relative+rotation*glm::dvec3(vesselCollarCentre(ship.radius),0,0)/units;
                const auto transform=glm::translate(glm::dmat4(1),collar)*glm::mat4_cast(rotation);
                const auto flange=glm::scale(transform,glm::dvec3(1./units));
                const auto mount=glm::scale(transform,glm::dvec3(vesselCollarMountLength(ship.radius),1.,1.)/units);
                batches[vesselCollarRole][1].push_back({glm::mat4(flange),{1.2f,1.27f,1.25f,1.f}});
                batches[vesselCollarMountRole][1].push_back({glm::mat4(mount),glm::vec4(ship.paint,1.f)});
                const auto clearTint=tracking?glm::mix(glm::vec3(.28f,1.46f,2.f),glm::vec3(.22f,2.f,.95f),
                                                    float(instruments->docking.capture)):
                                         glm::vec3(1.9f,1.21f,.49f);
                const auto tint=glm::mix(clearTint,glm::vec3(2.3f,.19f,.07f),float(smoothstep(0.,.85,dockingHold)));
                for(double side:{-1.,1.})
                    glow(collar+rotation*glm::dvec3(.79,.94,side*1.12)/units,.21/units,tint);
            }
            // Keep separate lamps visible on approaching hulls, and let them
            // resolve into a dim point before the mesh disappears at distance.
            const double footprint=std::max(.36,std::min(range*lens/height,ship.radius*.12));
            const auto lamp=[&](glm::dvec3 local,double size,glm::vec3 colour) {
                glow(point(local),std::max(size*scale,footprint/units),colour);
            };
            const double wing=patrol?.85:survey?1.06:1.22, wingZ=patrol?.8:survey?0.:.69, wingY=cargo?-.20:0.;
            lamp({-wing,wingY,wingZ},.026,{2.6f,.23f,.15f});
            lamp({ wing,wingY,wingZ},.026,{.23f,2.1f,.74f});
            const double nose=cargo?-1.62:-1.72;
            lamp({0,cargo?.30:.16,patrol?-1.32:survey?-1.40:nose},.029,{2.45f,1.81f,1.22f});
            if (ship.strobe)
                lamp({0,cargo?.49:.38,0},.038,glm::vec3(2.1f,2.3f,2.6f)*
                     float(smoothstep(300.,900.,range)));
            if(signal>0.)
                lamp({0,.57,0},.062,glm::vec3(3.3f,.44f,.09f)*float(signal));
            for (double side : {-1.,1.}) {
                const auto engine=nozzle(side);
                const auto blue=glm::vec3(.35f,1.25f,2.35f)*float(.22+ship.thrust);
                lamp(engine,.032+.042*ship.thrust,blue);
                const auto plume=engine+glm::dvec3(0,0,ship.thrust*.95);
                const float plumeLight=signalStrength*float(smoothstep(1.25,3.,pixels));
                if(plumeLight>0.f)
                    addLine(exhaust,point(engine),point(plume),blue*plumeLight);
            }
        }
    }

    void observePlayerTrail(const Navigation& nav,const Address& system,double seconds,double units) {
        if(!instruments||instruments->warp!=WarpPhase::Off||instruments->shrinking||instruments->landed||
           instruments->docking.docked||instruments->hull<=0.||instruments->speed<.22){
            playerTrail.cut();
            return;
        }
        auto reference=nav.anchor;
        while(reference.kind()>Kind::Planet)reference=reference.parent();
        const auto pose=universe.relative(nav.anchor,reference,0.);
        const auto centre=pose.position+pose.orientation*(nav.cameraPosition()*units);
        const auto axes=pose.orientation*nav.orientation;
        const auto engine=[&](double side){return centre+axes*glm::dvec3(side*1.45,-.46,7.85);};
        const auto& history=playerTrail.points();
        if(!history.empty()){
            const auto& last=history.back();
            if(!system.ancestorOf(last.reference))playerTrail.cut();
            else {
                const double elapsed=std::max(0.,seconds-last.seconds);
                const auto old=universe.relative(last.reference,reference,0.);
                const auto midpoint=.5*(last.exhaust[0]+last.exhaust[1]);
                const double step=glm::distance(centre,old.position+old.orientation*midpoint);
                if(step>std::max(18.,instruments->speed*elapsed*2.4+12.))playerTrail.cut();
            }
        }
        playerTrail.nozzleWidth=.32;
        const float light=float(smoothstep(.22,9.,instruments->speed)*(.67+.33*instruments->engine));
        playerTrail.observe({reference,{engine(-1.),engine(1.)},seconds,light});
    }

    void renderShipTrails(const Navigation& nav) {
        if(!activity||!activity->enabled||!xalariaMap||probeRemote||exteriorHidden||
           (instruments&&instruments->shrinking))return;
        const double seconds=activity->seconds;
        auto system=nav.anchor;
        while(system.kind()>Kind::System)system=system.parent();
        if(system.kind()!=Kind::System){playerTrail.cut();return;}
        const double units=std::pow(10.,nav.logDistance);
        observePlayerTrail(nav,system,seconds,units);
        playerTrail.expire(seconds);
        for(auto& [id,trail]:trafficTrails)trail.expire(seconds);
        std::erase_if(trafficTrails,[](const auto& entry){return entry.second.points().empty();});

        struct TrailFrame {glm::dvec3 eye;glm::dquat axes;};
        struct TrailKnot {std::array<glm::dvec3,2> exhaust;double age;float intensity;bool begins;};
        std::map<Address,TrailFrame> frames;
        const double eyeScale=std::pow(10.,nav.logDistance-anchorFrames.back().logRadius);
        const double aspect=double(width)/height;
        const double pixel=2.*std::tan(fieldOfView*.5)/height;
        trailRibbons.vertices.clear();
        trailRibbons.indices.clear();
        const auto draw=[&](const ShipTrail& trail){
            if(trail.points().size()<2)return;
            std::vector<TrailKnot> knots;
            knots.reserve(trail.points().size());
            bool gap=true;
            for(const auto& point:trail.points()){
                if(!system.ancestorOf(point.reference)){gap=true;continue;}
                auto frame=frames.find(point.reference);
                if(frame==frames.end())
                    frame=frames.emplace(point.reference,TrailFrame{
                        universe.pointRelative(nav.anchor,eye*eyeScale,point.reference,0.),
                        poseFor(point.reference,nav).orientation}).first;
                const auto relative=[&](glm::dvec3 p){return frame->second.axes*(p-frame->second.eye)/units;};
                knots.push_back({{relative(point.exhaust[0]),relative(point.exhaust[1])},
                                 std::max(0.,seconds-point.seconds),point.intensity,gap||point.begins});
                gap=false;
            }
            constexpr std::array offsets{-3.,-.78,0.,.78,3.};
            constexpr std::array profile{0.f,.21f,1.f,.21f,0.f};
            for(size_t side=0;side<2;++side){
                const auto emit=[&](size_t i,glm::dvec3 direction){
                    const auto& knot=knots[i];
                    const auto point=knot.exhaust[side];
                    const double depth=-(cameraInverse*point).z;
                    const double distance=glm::length(point);
                    auto across=glm::cross(direction,-point);
                    const double span=glm::length(across);
                    across=span>1e-24?across/span:cameraQ*glm::dvec3(1,0,0);
                    const double size=std::max((trail.nozzleWidth+knot.age*.065)/units,
                                               std::max(depth,worldNearPlane)*pixel*.59);
                    const double width=std::min(size,std::max(1e-12,distance*.10));
                    const float light=knot.intensity*float((1.-smoothstep(.9,shipTrailLifetime,knot.age))*
                        (1.-smoothstep(18000.,60000.,distance*units))*
                        smoothstep(worldNearPlane,worldNearPlane+width*2.,depth));
                    for(size_t j=0;j<offsets.size();++j){
                        const auto p=point+across*(width*offsets[j]);
                        trailRibbons.vertices.push_back({float(p.x),float(p.y),float(p.z),
                                                         rgba(glm::vec3(light*profile[j]))});
                    }
                };
                for(size_t i=1;i<knots.size();++i){
                    if(knots[i].begins)continue;
                    const auto a=knots[i-1].exhaust[side],b=knots[i].exhaust[side];
                    const auto chord=b-a;
                    const double length=glm::length(chord);
                    if(length<1e-15||std::min(glm::length(a),glm::length(b))*units-length*units>60000.||
                       !LOD::visible(cameraInverse*((a+b)*.5),length*.5+10./units,aspect,fieldOfView))continue;
                    const auto previous=i>1&&!knots[i-1].begins?knots[i-2].exhaust[side]:a;
                    const auto next=i+1<knots.size()&&!knots[i+1].begins?knots[i+1].exhaust[side]:b;
                    const auto base=uint32_t(trailRibbons.vertices.size());
                    emit(i-1,b-previous);
                    emit(i,next-a);
                    for(uint32_t j=0;j<4;++j){
                        const auto k=base+j;
                        trailRibbons.indices.insert(trailRibbons.indices.end(),{k,k+5,k+1,k+1,k+5,k+6});
                    }
                }
            }
        };
        draw(playerTrail);
        for(const auto& [id,trail]:trafficTrails)draw(trail);
        stats.shipTrailTriangles=trailRibbons.indexCount()/3;
        if(!trailRibbons.vertices.empty()){
            const auto view=hyperion->shipTrailView();
            const auto rotationView=glm::mat4_cast(glm::quat(cameraInverse));
            const glm::mat4 trailProjection(projection);
            bgfx::setViewTransform(view,glm::value_ptr(rotationView),glm::value_ptr(trailProjection));
            transient(trailRibbons,view,false,true);
        }
    }

    void renderWormholeTraffic(const XalariaWormholeMouth& mouth,glm::dvec3 planetEye,
                               glm::dquat planetRotation,double units,ActivityBatches& batches,Mesh& exhaust) {
        const auto origin=planetRotation*(mouth.metres-planetEye);
        if (glm::length(origin)>trafficRadioRange+5000.) return;
        const auto axes=planetRotation*mouth.orientation;
        const XPlace port{uint32_t(mouth.reference.parent().id()),mouth.reference.id(),0,false};
        if(const auto station=xalariaEconomy->orbitalStationId(port))
            renderTraffic(*station,TrafficService::Wormhole,
                          sampleWormholeTraffic(mouth.radius,xalariaEconomy->trafficClock(*station,activity->seconds).first),
                          mouth.metres,mouth.orientation,planetEye,planetRotation,units,batches,exhaust);
        const auto buoys=wormholeBuoys(mouth.radius);
        for (size_t i=0;i<buoys.size();++i) {
            const auto relative=(origin+axes*buoys[i])/units;
            const auto view=cameraInverse*relative;
            if (-view.z<=worldNearPlane || !LOD::visible(view,18./units,double(width)/height,fieldOfView))
                continue;
            const double range=glm::length(relative);
            if (celestialOccluded(view/range,range*units,skyOccluders)) continue;
            const glm::vec3 tint=i<2 ? glm::vec3(1.28f,.58f,.18f) : glm::vec3(.24f,.88f,1.32f);
            const auto model=glm::translate(glm::dmat4(1),relative)*glm::mat4_cast(axes)*
                             glm::scale(glm::dmat4(1),glm::dvec3(13./units));
            batches[size_t(ActivityKind::Survey)][1].push_back({glm::mat4(model),glm::vec4(tint,1)});
            const double pulse=.34+.66*smoothstep(.1,1.,std::cos(activity->seconds*2.2-double(i)*.63));
            const auto lamp=relative+axes*glm::dvec3(0,5.5/units,0);
            trafficLights.push_back(billboardInstance(lamp,
                std::max(1.7/units,range*std::tan(fieldOfView*.5)*1.3/height),tint*float(3.8*pulse)));
        }
    }

    void renderSpaceJunk(StationKey key,glm::dvec3 origin,glm::dquat axes,
                         double units,ActivityBatches& batches) {
        for (const auto& piece : sampleSpaceJunk(key,activity->seconds)) {
            const auto delta=origin+axes*piece.position;
            const double range=glm::length(delta);
            const auto view=cameraInverse*(delta/units);
            const double radius=piece.radius/units;
            const double pixels=LOD::pixels(radius,range/units,height,fieldOfView);
            if (pixels<.90 || !LOD::visible(view,radius*1.6,double(width)/height,fieldOfView) ||
                celestialOccluded(view/(range/units),range,skyOccluders))
                continue;
            const auto model=glm::translate(glm::dmat4(1),delta/units)*
                             glm::mat4_cast(axes*piece.orientation)*glm::scale(glm::dmat4(1),glm::dvec3(radius));
            batches[spaceJunkFirstRole+size_t(piece.kind)][pixels>26.?2:1].push_back(
                {glm::mat4(model),{1.22f,1.22f,1.22f,1.f}});
            ++stats.activityVisible;
        }
    }

    void renderActivity(const std::vector<Node>& nodes, const Navigation& nav, RegionCache& cache) {
        activityContacts.clear();
        weaponTargets.clear();
        combatBodies.clear();
        activityPicks.clear();
        stats.activityBytes = craftBytes;
        renderSurfaceEnvironment(nodes, nav, cache);
        if (!activity || !activity->enabled ||
            materialInspectionView(nav.anchor.kind(), bool(instruments), instruments && instruments->shrinking))
            return;
        // Like the terrain pass, subtract the double-precision eye before
        // uploading floats. Derivative normals then remain stable near a hull.
        const auto rotationView = glm::mat4_cast(glm::quat(cameraInverse));
        const glm::mat4 actorProjection(projection);
        bgfx::setViewTransform(62, glm::value_ptr(rotationView), glm::value_ptr(actorProjection));
        bgfx::setViewRect(62, 0, 0, uint16_t(width), uint16_t(height));
        bgfx::setViewTransform(trafficLightView,glm::value_ptr(rotationView),glm::value_ptr(actorProjection));
        bgfx::setViewRect(trafficLightView,0,0,uint16_t(width),uint16_t(height));
        bgfx::setViewTransform(stationGlassViewId,glm::value_ptr(rotationView),glm::value_ptr(actorProjection));
        bgfx::setViewRect(stationGlassViewId,0,0,uint16_t(width),uint16_t(height));
        bgfx::setViewMode(stationGlassViewId,bgfx::ViewMode::Sequential);
        const auto* boardedStation=cockpitVisible && !probeRemote && shipInterior?shipInterior->station():nullptr;
        const auto boardedId=boardedStation && xalariaEconomy && xalariaEconomy->dockedPlace()
            ?xalariaEconomy->orbitalStationId(*xalariaEconomy->dockedPlace()):std::nullopt;
        Mesh exhaust;
        Mesh trafficExhaust;
        Mesh dockingLights;
        std::vector<ActivityContact> debrisContacts;
        const double units = std::pow(10., nav.logDistance);
        for (const auto& system : nodes) {
            if (system.address.kind() != Kind::System)
                continue;
            const auto planets = cache.catalog(system);
            if (!planets)
                continue;
            const auto* offence = weapons && npcCombat
                                      ? weapons->hunterOffence(system.address, activity->seconds)
                                      : nullptr;
            ActivityBatches batches;
            std::map<StationMeshKey,std::vector<Instance>> stationBatches;
            auto activityFrames=*planets;
            if((xalariaStories&&!xalariaStories->evidenceBodies(system.address).empty())||(xalariaCombat&&!xalariaEconomy->orbitalBodies(system,activity->seconds).empty()))activityFrames.push_back(system);
            for (const auto& planet : activityFrames) {
                const bool moon = isMoon(planet.address);
                const bool pinned = activity->selected && activity->selected->home == planet.address;
                const bool pinnedDebris = pinned && isOrbitalDebris(*activity->selected);
                std::vector<ActivityBody> bodies;
                if (xalariaEconomy)
                    bodies=xalariaEconomy->orbitalBodies(planet,activity->seconds);
                if(xalariaStories)
                    for(auto& evidence:xalariaStories->evidenceBodies(planet.address))bodies.push_back(std::move(evidence));
                if (moon && !pinnedDebris && !(xalariaEconomy && (pinned || !bodies.empty())) &&
                    !planet.address.ancestorOf(nav.anchor) &&
                    !(nav.selected && planet.address.ancestorOf(*nav.selected)))
                    continue;
                const auto pose = poseFor(planet.address, nav);
                const auto planetEye = universe.pointRelative(
                    nav.anchor, eye * std::pow(10., nav.logDistance - anchorFrames.back().logRadius),
                    planet.address, 0);
                const auto shipEye=probeRemote&&shipNavigation
                    ?universe.pointRelative(shipNavigation->anchor,shipNavigation->cameraPosition()*
                        std::pow(10.,shipNavigation->logDistance-universe.describe(shipNavigation->anchor).frame.logRadius),
                        planet.address,0):planetEye;
                const double localRange = glm::length(planetEye);
                if (xalariaEconomy && xalariaMap && xalariaMap->wormhole && xalariaMap->wormhole->reference==planet.address)
                    renderWormholeTraffic(*xalariaMap->wormhole,planetEye,pose.orientation,units,batches,trafficExhaust);
                const double planetRadius = std::pow(10., planet.frame.logRadius);
                const bool local = !moon && std::min(localRange, glm::length(shipEye)) <
                                               std::max(2e7, planetRadius * 6);
                const bool pinnedTraffic = pinned && !pinnedDebris;
                if (!xalariaEconomy && !moon) {
                    if (local || pinnedTraffic)
                        bodies = sampleActivity(planet, activity->seconds);
                    else {
                        bodies.push_back(sampleActivity(planet, 0, activity->seconds));
                        bodies.push_back(sampleActivity(planet, 1, activity->seconds));
                    }
                }
                if (!moon && economy && economy->network && economy->network->system == system.address &&
                    !local && !pinnedTraffic)
                    for (const auto& f : economy->flights)
                        if (f.id.home == planet.address)
                            bodies.push_back(sampleActivity(planet, f.id.slot, activity->seconds));
                if (economy && economy->network && economy->network->system == system.address)
                    for (auto& body : bodies)
                        if (body.kind == ActivityKind::Freighter) {
                            const auto it = std::find_if(economy->flights.begin(), economy->flights.end(),
                                                         [&](const auto& f) { return f.id == body.id; });
                            if (it != economy->flights.end())
                                body = sampleEconomicFreight(universe, *economy->network, *it,
                                                             activity->seconds);
                        }
                if (npcCombat && (local || pinnedTraffic))
                    bodies.push_back(sampleRaider(universe, planet.address, activity->seconds));
                if (npcCombat && !moon && (local || pinnedTraffic)) {
                    bool richLane = false, highValueLane = false, weatherHold = false;
                    if (economy && economy->network && economy->network->system == system.address) {
                        const auto& sites = economy->network->sites;
                        const auto servesPlanet = [&](unsigned site) {
                            return site < sites.size() && sites[site].id.home == planet.address;
                        };
                        for (const auto& f : economy->flights) {
                            if (f.kg <= 0)
                                continue;
                            if (f.phase == FreightPhase::Transit &&
                                (servesPlanet(f.source) || servesPlanet(f.destination))) {
                                const auto value = f.kg * goodInfo(f.good).baseCents;
                                richLane |= value >= 10000;
                                highValueLane |= value >= 90000;
                            }
                            weatherHold |= f.phase == FreightPhase::Holding && f.weatherHold &&
                                           servesPlanet(f.destination);
                        }
                    }
                    const bool pair = mix64(planet.seed) % 3 == 0;
                    const auto add = [&](uint32_t slot, bool scheduled) {
                        const ActivityId id{planet.address, slot};
                        if (scheduled || (pinnedTraffic && *activity->selected == id) ||
                            npcCombat->agents().contains(id))
                            bodies.push_back(sampleCombatActivity(universe, id, activity->seconds));
                    };
                    add(cargoRaiderSlot, richLane && !pair);
                    add(missileWingSlot, richLane && pair);
                    add(cutoffRaiderSlot, richLane && pair);
                    add(factionHunterSlot, offence && offence->id.home == planet.address);
                    const auto localRole = mix64(planet.seed ^ 0x52414944504f4f4cULL) % 4;
                    add(skirmisherSlot, localRole <= 1);
                    add(scavengerSlot, localRole == 2);
                    add(raiderScoutSlot, localRole == 3);
                    if (npcCombat->reinforcementActive(planet.address, activity->seconds))
                        bodies.push_back(sampleCombatActivity(universe, {planet.address, pursuitRaiderSlot},
                                                             activity->seconds));
                    add(siegeRaiderSlot, highValueLane && pair);
                    add(saboteurSlot, weatherHold);
                }
                if (economy)
                    for (auto& body : bodies)
                        if (body.kind == ActivityKind::Harbour)
                            annotateEconomicPort(body, *economy);
                if (planet.address.kind()==Kind::Planet&&!probeRemote && (!instruments || !instruments->shrinking)) {
                    if (localRange > planetRadius * 1.03 && localRange < planetRadius * 4.5) {
                        for (auto& body : sampleOrbitalDebris(planet, activity->seconds))
                            bodies.push_back(std::move(body));
                    } else if (pinnedDebris) {
                        bodies.push_back(sampleActivity(planet, activity->selected->slot, activity->seconds));
                    }
                }
                for (auto body : bodies) {
                    if(isCombatActor(body.id)||isConvoyActor(body.id)){
                        TrafficCraft craft{body.position,body.orientation,body.tint,body.kind==ActivityKind::Raider?ActivityKind::Patrol:body.kind,body.radius,
                            std::min(1.,glm::length(body.velocity)/140.),true,body.velocity,TrafficPhase::OnCircuit};
                        renderTraffic(trafficStation(body.id),TrafficService::Orbital,std::span<const TrafficCraft>(&craft,1),{},
                            {1,0,0,0},planetEye,pose.orientation,units,batches,trafficExhaust,&body);
                        continue;
                    }
                    const bool natural = isOrbitalDebris(body.id);
                    const bool evidence=isInvestigationEvidence(body.id);
                    const bool identifiedEvidence=evidence&&body.status.starts_with("EVIDENCE");
                    if (npcCombat && !natural) {
                        auto physical=body;
                        npcCombat->apply(physical);
                        if(weapons)weapons->apply(physical);
                        const auto range = glm::length(physical.position - shipEye);
                        if (range < 30000 && combatBodies.size() < 512)
                            combatBodies.push_back(body);
                        npcCombat->apply(body);
                    }
                    if (weapons && !natural)
                        weapons->apply(body);
                    ++stats.activityBodies;
                    const auto deltaSI = pose.orientation * (body.position - planetEye);
                    const double distance = glm::length(deltaSI);
                    if ((!natural||landableDebris(body)) && glm::length(body.position-shipEye) < 10000 * (instruments ? instruments->hullMetres / 12 : 1) + body.radius &&
                        weaponTargets.size() < 256) {
                        auto target = body;
                        // Position and velocity already include the wreck overlay; fire() is idempotent for
                        // disabled actors.
                        weaponTargets.push_back(target);
                    }
                    if (distance <= 1e-12)
                        continue;
                    const auto direction = cameraInverse * (deltaSI / distance);
                    const bool selected = activity->selected && *activity->selected == body.id;
                    const bool modular = xalariaEconomy && body.kind==ActivityKind::Harbour;
                    const bool attached = modular && boardedId && body.id==*boardedId;
                    const bool occulted = !attached && celestialOccluded(direction, distance, skyOccluders);
                    if (modular && distance<trafficRadioRange+18000.)
                        if (const auto berth=dockingBerth(body)) {
                            const auto key=xalariaEconomy->stationDesignKey(berth->id);
                            const auto schedule=xalariaEconomy->trafficClock(berth->id,activity->seconds).first;
                            const auto axes=pose.orientation*berth->orientation;
                            const auto origin=pose.orientation*(berth->metres-planetEye);
                            renderTraffic(berth->id,TrafficService::Orbital,sampleOrbitalTraffic(key,schedule),
                                          berth->metres,berth->orientation,planetEye,pose.orientation,units,batches,trafficExhaust);
                            if (distance<38000.)
                                renderSpaceJunk(key,origin,axes,units,batches);
                            renderTraffic(berth->id,TrafficService::Port,sampleStationTraffic(key,schedule),
                                          berth->metres,berth->orientation,planetEye,pose.orientation,units,batches,trafficExhaust);
                        }
                    if (body.kind != ActivityKind::Debris || identifiedEvidence || (natural && (!occulted || selected))) {
                        const auto velocity = cameraInverse * (pose.orientation * body.velocity);
                        auto& contacts = natural ? debrisContacts : activityContacts;
                        contacts.push_back(
                            {body, direction, distance,
                             instruments ? glm::length(velocity -
                                                       glm::inverse(instruments->gunAim * head.orientation) *
                                                           instruments->localVelocity)
                                         : glm::length(velocity),
                             selected, occulted});
                    }
                    // The outbound lane can remain in view after its harbour is behind the ship.
                    if (selected && !occulted && distance < 18000 && (!instruments || !instruments->shrinking))
                        if (const auto berth = dockingBerth(body)) {
                            const auto phase = instruments ? instruments->docking.phase : DockingPhase::None;
                            const auto lane = dockingLane(*berth, activity->seconds, phase);
                            if (manualDocking(phase)) {
                                const auto lamp = [&](glm::dvec3 local, double radius, glm::vec3 colour) {
                                    const auto relative = pose.orientation *
                                        ((berth->metres - planetEye) + berth->orientation * local) / units;
                                    const double depth = -(cameraInverse * relative).z;
                                    if (depth > worldNearPlane)
                                        addBillboard(eye + relative, std::max(radius / units, depth * .0024),
                                                     colour);
                                };
                                const auto guideColour = phase == DockingPhase::ManualCapture
                                    ? glm::vec3(.38,2.8,.95) : glm::vec3(.30,1.9,3.2);
                                const auto threshold = phase == DockingPhase::ManualCapture
                                    ? guideColour : glm::vec3(2.7,1.5,.17);
                                const double length = berth->laneLength(), width = berth->halfWidth();
                                for (const double z : {30.,86.,156.,260.,430.,680.,length}) {
                                    const auto spread = std::lerp(12.,width,z/length);
                                    const auto elevation = std::lerp(6.,width*.52,z/length);
                                    for (double side : {-1.,1.})
                                        lamp({side*spread,-elevation,z},z==length ? 4.5 : 1.25,guideColour);
                                }
                                for (double side : {-1.,1.})
                                    for (double level : {-1.,1.})
                                        lamp({side*11.,level*6.,2.2},.42,threshold);
                            }
                            stats.dockingGuideVertices += lane.vertices.size();
                            const auto base = uint32_t(dockingLights.vertices.size());
                            for (auto vertex : lane.vertices) {
                                const auto local = glm::dvec3(vertex.x, vertex.y, vertex.z);
                                const auto relative = pose.orientation *
                                    ((berth->metres - planetEye) + berth->orientation * local) / units;
                                vertex.x = float(relative.x);
                                vertex.y = float(relative.y);
                                vertex.z = float(relative.z);
                                dockingLights.vertices.push_back(vertex);
                            }
                            for (size_t i = 0; i < lane.indexCount(); ++i)
                                dockingLights.indices.push_back(base + lane.index(i));
                        }
                    const bool closeRock = landableDebris(body) && distance < body.radius * 2.2;
                    // A docked player can look at the near gantry with the station's centre behind them.
                    if ((!attached && !closeRock && direction.z >= 0) || occulted ||
                        (distance > 2e6 && body.kind != ActivityKind::Harbour && !selected))
                        continue;
                    const double angular = body.radius / distance;
                    if (!attached && !closeRock && !LOD::visible(direction * 500., std::max(.5, angular * (modular?900.:500.)), double(width) / height,
                                      fieldOfView))
                        continue;
                    const auto point = eye + deltaSI / units;
                    const auto shell = eye + cameraQ * direction * 500.;
                    const double pixels = angular * height / std::tan(fieldOfView * .5);
                    if (body.kind != ActivityKind::Debris || identifiedEvidence || (natural && (selected || pixels > 1.6)))
                        if (auto screen = project(shell))
                            activityPicks.emplace_back(body.id, *screen);
                    if (natural && pixels < .20 && !selected)
                        continue;
                    const auto role = evidence?evidenceFirstRole+(body.id.slot&3u):landableDebris(body) ? debrisSurfaceRole :
                        size_t(body.kind == ActivityKind::Raider ? ActivityKind::Patrol : body.kind);
                    const unsigned detail = attached || pixels > 48 ? 2 : pixels > 8 ? 1 : 0;
                    if (attached || ((closeRock || (pixels > 1.6 && -cameraSpace(point).z > worldNearPlane)) &&
                                     distance / units < 8000)) {
                        const Random shape{body.seed};
                        auto axes = natural ? glm::dvec3(1., landableDebris(body) ? 1. : .66 + .34 * shape.unit(40),
                                                                .54 + .46 * shape.unit(41))
                                                  : glm::dvec3(1.);
                        switch (raiderRole(body.id)) {
                        case RaiderRole::Skirmisher: axes = {1.20, .57, 1.30}; break;
                        case RaiderRole::Siege: axes = {1.12, 1.05, 1.35}; break;
                        case RaiderRole::Saboteur: axes = {.82, .62, 1.27}; break;
                        case RaiderRole::Scavenger: axes = {1.26, .81, 1.02}; break;
                        case RaiderRole::Scout: axes = {.81, .50, 1.18}; break;
                        case RaiderRole::Pursuit: axes = {1.18, .77, 1.41}; break;
                        default: break;
                        }
                        auto model = glm::translate(glm::dmat4(1), deltaSI / units) *
                                     glm::mat4_cast(pose.orientation * body.orientation) *
                                     glm::scale(glm::dmat4(1), axes * (body.radius / units));
                        const auto paint = body.kind == ActivityKind::Harbour ? glm::vec3(1) : body.tint;
                        const Instance instance{glm::mat4(model),glm::vec4(paint,1)};
                        if (modular) stationBatches[{xalariaEconomy->stationDesignKey(body.id),detail}].push_back(instance);
                        else batches[role][detail].push_back(instance);
                    } else {
                        const double footprint =
                            std::max(angular, 2 * std::tan(fieldOfView * .5) / height * .85);
                        addBillboard(shell, 500 * footprint,
                                     body.tint * (body.kind == ActivityKind::Debris ? .25f : .75f), true);
                    }
                    ++stats.activityVisible;
                    if (selected && landableDebris(body) && distance < 1400) {
                        const auto north = body.orientation * glm::dvec3(0, 1, 0);
                        const auto pad = debrisPad(body, glm::dot(planetEye - body.position, north) < 0 ? -1 : 1);
                        const auto right = glm::cross(pad.forward, pad.up);
                        const auto local = [&](double x, double z) {
                            return pose.orientation *
                                (pad.surface - planetEye + pad.up * .075 + right * x + pad.forward * z) / units;
                        };
                        const glm::vec3 tint(.26f, .94f, .85f);
                        for (double x : {-1., 1.})
                            for (double z : {-1., 1.}) {
                                addLine(dockingLights, local(x * 12, z * 12), local(x * 8, z * 12), tint);
                                addLine(dockingLights, local(x * 12, z * 12), local(x * 12, z * 8), tint);
                            }
                    }
                    if (distance < 150000 && body.kind != ActivityKind::Debris) {
                        // Hyperion drawFactionCraft's two engine traces, kept in
                        // actual body coordinates rather than screen particles.
                        const auto rotation = pose.orientation * body.orientation;
                        const double pulse = .65 + .08 * std::sin(activity->seconds * 7 + body.phase);
                        if (body.kind != ActivityKind::Harbour && glm::length(body.velocity) > 1)
                            for (int side : {-1, 1}) {
                                const auto engine = point + rotation * glm::dvec3(side * .44, 0, 1.16) *
                                                                (body.radius / units);
                                addLine(exhaust, engine,
                                        engine + rotation * glm::dvec3(0, 0, pulse) * (body.radius / units),
                                        body.tint * 1.3f);
                            }
                        if (modular) {
                            for (double side : {-1.,1.}) {
                                const auto lamp=point+rotation*glm::dvec3(side*.177,.157,1.62)*(body.radius/units);
                                addBillboard(lamp,std::max(1.8/units,distance/units*.00035),body.tint*.82f);
                            }
                        } else if (body.kind == ActivityKind::Harbour) {
                            for (int side : {-1, 1}) {
                                const auto lamp =
                                    point + rotation * glm::dvec3(side * .82, .15, 0) * (body.radius / units);
                                const double radius =
                                    std::max(body.radius * .025 / units, distance / units * .00065);
                                addBillboard(lamp, radius,
                                             body.tint *
                                                 float(.5 + .5 * std::sin(activity->seconds * 2 + side)));
                            }
                            // The animated cargo hook is the harbour's equipment.
                            const double travel = .5 + .5 * std::sin(activity->seconds * .55);
                            const auto base =
                                point + rotation * glm::dvec3(.45, .64, .18) * (body.radius / units);
                            const auto hook = base + rotation * glm::dvec3(0, -.08 - .14 * travel, 0) *
                                                         (body.radius / units);
                            addLine(exhaust, base, hook, body.tint);
                        }
                    }
                }
            }
            const auto drawActors=[&](const GPUMesh& mesh,const std::vector<Instance>& instances,size_t role,
                                      bool glass=false,bool stationHull=false) {
                const auto count = bgfx::getAvailInstanceDataBuffer(uint32_t(instances.size()), 80);
                if (!count || !bgfx::isValid(mesh.vertices)) return;
                bgfx::InstanceDataBuffer buffer;
                bgfx::allocInstanceDataBuffer(&buffer, count, 80);
                if (glass) {
                    const auto toBerth=glm::translate(glm::mat4(1),glm::vec3(dockingBerthOffset)) *
                                       glm::scale(glm::mat4(1),glm::vec3(1.f/orbitalStationRadius));
                    for (uint32_t i=0;i<count;++i) {
                        const Instance pane{instances[i].transform*toBerth,instances[i].color};
                        std::memcpy(buffer.data+i*sizeof(Instance),&pane,sizeof(pane));
                    }
                } else {
                    std::memcpy(buffer.data, instances.data(), count * sizeof(Instance));
                }
                bgfx::setVertexBuffer(0, mesh.vertices);
                bgfx::setIndexBuffer(mesh.indices);
                bgfx::setInstanceDataBuffer(&buffer);
                bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_GREATER | BGFX_STATE_MSAA |
                               (glass?BGFX_STATE_BLEND_ALPHA:BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z));
                {
                    const bool protectedPaint=role>=spaceJunkFirstRole || role==size_t(ActivityKind::Harbour) ||
                        (xalariaEconomy && (role==size_t(ActivityKind::Freighter) || role==size_t(ActivityKind::Ferry) ||
                                           role==size_t(ActivityKind::Patrol) || role==size_t(ActivityKind::Survey)));
                    const bool vessel=role==size_t(ActivityKind::Freighter) || role==size_t(ActivityKind::Ferry) ||
                                      role==size_t(ActivityKind::Patrol) || role==size_t(ActivityKind::Survey);
                    const float materialRole=stationHull?-2.f:vessel?-3.f:protectedPaint?-1.f:float(role);
                    Geometry source;
                    ScenePresentation presentation{
                        source, system.address, {.085, .82, .035, .01}, float(role) / 6, 1.f,
                        {protectedPaint?4.f:3.f,materialRole,float(units),0}};
                    presentation.environment={0,1,0,.45f};presentation.surface.x=.25f;
                    hyperion->bind(presentation, {}, glm::vec3(lightAt(system.address, nav)),
                                   glm::mat4(1), 0, activity->seconds);
                }
                if (glass) {
                    hyperion->bindCockpitGlazing(GlazingView::TunnelExterior);
                    bgfx::submit(stationGlassViewId,hyperion->cockpitGlazingProgram());
                } else {
                    bgfx::submit(62, hyperion->geometryProgram());
                }
                stats.activityTriangles += count * mesh.count / 3;
            };
            for (size_t role = 0; role < batches.size(); ++role)
                for (unsigned detail = 0; detail < 3; ++detail) {
                    const auto& instances=batches[role][detail];
                    if (instances.empty()) continue;
                    auto& mesh=craftMeshes[role][detail];
                    if (!bgfx::isValid(mesh.vertices)) {
                        const auto source=role>=evidenceFirstRole?investigationMesh(unsigned(role-evidenceFirstRole),detail):
                                          role==vesselCollarRole?xalariaVesselCollar():
                                          role==vesselCollarMountRole?xalariaVesselCollarMount():
                                          role>=spaceJunkFirstRole?spaceJunkMesh(SpaceJunkKind(role-spaceJunkFirstRole),detail):
                                          role==debrisSurfaceRole?landableDebrisMesh(detail):activityMesh(ActivityKind(role),detail);
                        mesh=upload(source);
                        craftBytes+=source.bytes();
                    }
                    drawActors(mesh,instances,role);
                }
            for (const auto& [key,instances] : stationBatches) {
                auto [entry,created]=stationMeshes.try_emplace(key);
                auto& station=entry->second;
                if (created) {
                    const auto plan=generateStation(key.first);
                    const auto source=modularStationHull(plan,key.second);
                    station.mesh=upload(source);
                    station.bytes=source.bytes();
                    if (key.second>0) {
                        const auto panes=stationGlazing(plan);
                        station.glass=upload(panes);
                        station.bytes+=panes.bytes();
                    }
                    craftBytes+=station.bytes;
                }
                station.lastFrame=frame;
                drawActors(station.mesh,instances,size_t(ActivityKind::Harbour),false,true);
                // Boarded glass uses the same pane mesh in the cabin pass, where it can refract the exterior.
                if ((!boardedStation || boardedStation->key!=key.first) && bgfx::isValid(station.glass.vertices))
                    drawActors(station.glass,instances,size_t(ActivityKind::Harbour),true);
            }
        }
        while (stationMeshes.size()>48) {
            const auto oldest=std::min_element(stationMeshes.begin(),stationMeshes.end(),[](const auto& a,const auto& b){
                return a.second.lastFrame < b.second.lastFrame;
            });
            if (oldest->second.lastFrame==frame) break;
            craftBytes-=oldest->second.bytes;
            destroy(oldest->second.mesh);
            destroy(oldest->second.glass);
            stationMeshes.erase(oldest);
        }
        std::sort(activityContacts.begin(), activityContacts.end(),
                  [](const auto& a, const auto& b) { return a.distance < b.distance; });
        if (activityContacts.size() > 80) {
            auto port = std::find_if(activityContacts.begin() + 80, activityContacts.end(),
                                     [](const auto& c) { return c.body.kind == ActivityKind::SurfacePort; });
            if (port != activityContacts.end())
                activityContacts[78] = *port;
            auto pinned = std::find_if(activityContacts.begin() + 80, activityContacts.end(),
                                       [](const auto& c) { return c.selected&&!isXalariaTraffic(c.body.id); });
            if (pinned != activityContacts.end())
                activityContacts[79] = *pinned;
            activityContacts.erase(std::remove_if(activityContacts.begin()+80,activityContacts.end(),[](const auto& c){
                return !isXalariaTraffic(c.body.id);
            }),activityContacts.end());
        }
        std::sort(debrisContacts.begin(), debrisContacts.end(), [](const auto& a, const auto& b) {
            return a.selected != b.selected ? a.selected : a.distance < b.distance;
        });
        if (debrisContacts.size() > orbitalDebrisCapacity)
            debrisContacts.resize(orbitalDebrisCapacity);
        for (auto& contact : debrisContacts)
            activityContacts.push_back(std::move(contact));
        if(xalariaRadio&&xalariaRadio->dockingTarget&&xalariaEconomy&&xalariaMap&&instruments&&instruments->docking.active){
            const auto vessel=xalariaTrafficContact(*xalariaEconomy,*xalariaRadio->dockingTarget,xalariaMap->wormhole,activity->seconds);
            if(vessel)
                if(const auto berth=xalariaVesselBerth(vessel->body)){
                    const auto rotation=poseFor(berth->id.home,nav).orientation;
                    const auto eyeLocal=universe.pointRelative(nav.anchor,
                        eye*std::pow(10.,nav.logDistance-anchorFrames.back().logRadius),berth->id.home,0.);
                    const double range=glm::length(berth->metres-eyeLocal);
                    const float fade=float(1.-smoothstep(1050.,1800.,range));
                    if(fade>0.f){
                        const auto& docking=instruments->docking;
                        auto lane=dockingLane(*berth,activity->seconds,docking.phase,docking.capture,fade);
                        const auto toScene=[&](glm::dvec3 local){
                            return rotation*(berth->metres-eyeLocal+berth->orientation*local)/units;
                        };
                        for(auto& v:lane.vertices){
                            const auto point=toScene({v.x,v.y,v.z});
                            v.x=float(point.x);v.y=float(point.y);v.z=float(point.z);
                        }
                        stats.dockingGuideVertices+=lane.vertices.size();
                        transient(lane,62,true,true);
                    }
                }
        }
        transient(exhaust, worldView(), true, true);
        transient(trafficExhaust,trafficLightView,true,true);
        transient(dockingLights, 62, true, true);
        stats.activityBytes = craftBytes;
    }
    void renderNode(const Node& node, const Navigation& nav, RegionCache& cache, unsigned maximum = 4) {
        if(exteriorHidden&&node.address.kind()<=Kind::Surface)return;
        auto pose = poseFor(node.address, nav);
        auto cp = cameraSpace(pose.position);
        double r = pose.scale;
        auto kind = node.address.kind();
        // A compact remnant can subtend a large disc while its enclosing
        // system exceeds the normal cell-size guard. Cull that immutable mesh
        // using its physical photosphere, retaining the original model scale.
        const bool compactStellar =
            node.stellar && kind == Kind::System && node.address == nav.anchor && r > 1e4;
        const double bounds = compactStellar ? node.stellarRadiusMetres / std::pow(10., nav.logDistance)
                              : kind == Kind::System && r > 1e-6 && r < 1e4
                                  ? universe.systemRadiusMetres(node) / std::pow(10., nav.logDistance)
                                  : r;
        double pixels = LOD::pixels(bounds, glm::length(cp), height, 1.0);
        auto& state = details[node.address];
        bool stellarProxy =
            kind == Kind::System && (node.address.ancestorOf(nav.anchor) || state.blend.current);
        if (stellarProxy && !instruments && cp.z < 0) {
            double physical = std::pow(10.0, std::log10(node.stellarRadiusMetres) - nav.logDistance) * 2.5;
            double radius = std::clamp(physical, -cp.z * .001, -cp.z * .035);
            float intensity = static_cast<float>(.35 + .65 * std::min(node.massSolar / 3, 1.0));
            if (LOD::visible(cp, radius, static_cast<double>(width) / height, fieldOfView))
                addBillboard(pose.position, radius, stellarRadiance(node) * intensity, false,
                             3.f + hyperionPhase(node.seed) * .25f);
        } else if (node.address == nav.anchor && pixels < 1.5 && cp.z < 0 && kind != Kind::Cosmic)
            addBillboard(pose.position, -cp.z * .0015, material(node.environment.mineral).color);
        if (kind != Kind::Cosmic && (r < 1e-8 || (r > 1e4 && !compactStellar)))
            return;
        const double materialScale = kind >= Kind::Rock && kind <= Kind::Grain
            ? r / std::max(1e-100, glm::length(eye)) : r;
        const bool inspect = materialInspectionView(nav.anchor.kind(), bool(instruments), instruments && instruments->shrinking);
        float opacity = node.address.ancestorOf(nav.anchor) && node.address != nav.anchor &&
                                (kind < Kind::Rock || inspect)
                            ? static_cast<float>(1 - smoothstep(3, 12, materialScale))
                            : 1.f;
        if (kind == Kind::Cosmic)
            opacity = 1;
        if (opacity < .005)
            return;
        if (kind != Kind::Cosmic &&
            (pixels < .7 || !LOD::visible(cp, bounds, double(width) / height, fieldOfView))) {
            if (kind == Kind::Planet)
                queuePlanetRing(node, pose, cp, opacity);
            return;
        }
        if (kind == Kind::Mountain && node.address != nav.anchor && pixels < 160)
            return;
        if (kind == Kind::Surface && node.address != nav.anchor && pixels < 100)
            return;
        state.level = screenDetail(kind, pixels, state.initialized ? state.level : 0);
        state.level = std::min(state.level, maximum);
        state.initialized = true;
        state.used = frame;
        unsigned viewKey =
            kind == Kind::Planet ? terrainView(glm::inverse(pose.orientation) * (eye - pose.position)) : 0;
        auto candidate = cache.request(node, state.level,
                                       kind == Kind::Cosmic         ? 8
                                       : node.address == nav.anchor ? 0
                                                                    : 4,
                                       viewKey);
        const bool streamedTerrain = terrainActive && streamedTerrainCovers(node.address, nav.anchor, false);
        // Retain the original node/LOD reference for identity and inspection;
        // streamed tiles provide the terrain draw without duplicate GPU buffers.
        state.blend.update(streamedTerrain || prepareGPU(candidate) ? candidate : nullptr, frameSeconds);
        if (state.blend.current) {
            ++stats.visibleObjects;
            if (isMoon(node.address))
                ++stats.moonWorlds;
            if (!streamedTerrain)
                drawBlend(state, node, nav, opacity);
            if (kind == Kind::Planet)
                queuePlanetRing(node, pose, cp, opacity);
        }
    }
    void renderSection(const Node& parent, const Navigation& nav, RegionCache& cache) {
        if(exteriorHidden)return;
        if (parent.address.kind() >= Kind::Rock &&
            !materialInspectionView(nav.anchor.kind(), bool(instruments), instruments && instruments->shrinking))
            return;
        if (parent.address.kind() < Kind::Planet || parent.address.kind() > Kind::Grain)
            return;
        if (terrainActive && parent.address.kind() <= Kind::Mountain)
            return;
        auto pose = poseFor(parent.address, nav);
        const double materialScale = parent.address.kind() >= Kind::Rock
            ? pose.scale / std::max(1e-100, glm::length(eye)) : pose.scale;
        float opacity = static_cast<float>(smoothstep(3, 12, materialScale));
        if (opacity < .005)
            return;
        // Quantisation only selects cached sampling windows. Every window queries
        // the same parent's density in its actual hierarchical reference frame.
        auto key = parent.address.str() + "|" + nav.anchor.str();
        auto& state = localSections[key];
        const bool microscopic = parent.address.kind() >= Kind::Rock;
        const double physicalLog = microscopic && glm::length(nav.cameraPosition()) > 0
            ? sectionViewScale(nav.logDistance, nav.cameraPosition()) : nav.logDistance;
        const int bucket = microscopic && state.initialized
            ? sectionViewBucket(physicalLog, state.sectionWindow) : sectionBucket(physicalLog);
        state.sectionWindow = bucket;
        state.level = microscopic ? sectionPresentationDetail(height, state.level) : 0;
        state.initialized = true;
        state.used = frame;
        auto candidate = cache.requestSection(parent, nav.anchor, bucket, state.level);
        state.blend.update(prepareGPU(candidate) ? candidate : nullptr, frameSeconds);
        if (state.blend.current) {
            drawBlend(state, parent, nav, opacity);
            ++stats.sections;
            sectionLabel = parent.label();
        }
    }
    void renderTerrain(const Node& planet, const Navigation& nav, RegionCache& cache) {
        if (!terrainActive)
            return;
        const auto transform = pointTransform(framesFor(planet.address), planet.address, anchorFrames,
                                              nav.anchor, nav.logDistance);
        const auto rotationView = glm::mat4_cast(glm::quat(cameraInverse));
        const glm::mat4 proj(projection);
        bgfx::setViewTransform(1, glm::value_ptr(rotationView), glm::value_ptr(proj));
        bgfx::setViewRect(1, 0, 0, uint16_t(width), uint16_t(height));
        const auto mapping = pointTransform(anchorFrames, nav.anchor, framesFor(planet.address),
                                            planet.address, planet.frame.logRadius);
        const double units = std::pow(10., nav.logDistance - anchorFrames.back().logRadius);
        glm::dmat4 pattern(1);
        for (unsigned i = 0; i < 3; ++i)
            pattern[i] = glm::dvec4(mapping.linear[i] * units, 0);
        pattern[3] = glm::dvec4(mapping(eye * units), 1);
        bool updateSun = false;
        if (landingShade && instruments && !instruments->shrinking &&
            instruments->altitude >= 0 && instruments->altitude < instruments->hullMetres / 12. * 38.) {
            const auto pose = poseFor(planet.address, nav);
            const double radiusMetres = std::pow(10., planet.frame.logRadius);
            const auto planetEye = glm::dvec3(pattern[3]) * radiusMetres;
            const auto direction = glm::normalize(planetEye);
            const double groundHeight = universe.terrainHeight(planet, direction);
            const double altitude = glm::length(planetEye) - radiusMetres - groundHeight;
            const double size = instruments->hullMetres / 12.;
            const double shade = 1 - smoothstep(size, size * 38, std::max(0., altitude));
            if (shade > 0 && size > 0) {
                const double units = std::pow(10., nav.logDistance);
                const auto point =
                    pose.orientation * (direction * (radiusMetres + groundHeight) - planetEye) / units;
                hyperion->landingContact(
                    planet.address, glm::vec3(point), glm::vec3(pose.orientation * direction),
                    float((3.2 * size + std::max(0., altitude) * .22) / units), float(shade * .35));
            }
        }
        SunShadowFrame sun;
        if (!exteriorHidden && sunShadows && instruments && !instruments->shrinking &&
            std::pow(10., nav.logDistance) > .01) {
            const auto pose = poseFor(planet.address, nav);
            const double radiusMetres = std::pow(10., planet.frame.logRadius);
            const auto planetEye = glm::dvec3(pattern[3]) * radiusMetres;
            const auto direction = glm::normalize(planetEye);
            const auto light = glm::inverse(pose.orientation) * lightAt(planet.address, nav);
            const double groundHeight = universe.terrainHeight(planet, direction);
            const double altitude = glm::length(planetEye) - radiusMetres - groundHeight;
            if (altitude < 60000 && glm::dot(direction, light) > .035) {
                // Reuse the existing single map. Its snapped, power-of-two span
                // follows approach altitude; no additional texture is allocated.
                terrainSunSpan = std::clamp(std::exp2(std::ceil(std::log2(std::max(2048., altitude * 8.)))),
                                            2048., 262144.);
                terrainSunAnchor = direction * (radiusMetres + groundHeight);
                sun = sunShadowFrame(terrainSunAnchor, light, pose.orientation, planetEye,
                                     std::pow(10., nav.logDistance), terrainSunSpan,
                                     bgfx::getCaps()->homogeneousDepth, bgfx::getCaps()->originBottomLeft);
                uint64_t key = descriptorFingerprint(planet);
                for (const auto& patch : terrain.patches())
                    key = mix64(key ^ encodeTile(patch.tile) ^ (uint64_t(patch.stitch) << 32));
                if(activity && economy)
                    if(const auto world=surfaceLife.find(planet.address))
                        for(const auto& city:world->settlements)
                            if(glm::length(city.landingPoint-planetEye)<city.radius+1200){
                                // Reuse the single shadow target; nearby working equipment
                                // receives bounded 4 Hz caster updates while the ship holds still.
                                key=mix64(key^uint64_t(activity->seconds*4));break;
                            }
                if (terrain.morphing())
                    key = mix64(key ^ frame);
                updateSun = hyperion->prepareSun(sun, key);
                terrainSunPlanet = planet.address;
            }
        }
        std::vector<Borehole> localOpenings;
        if (economy) {
            const auto metres=glm::dvec3(pattern[3])*std::pow(10.,planet.frame.logRadius);
            for(const auto& saved:economy->exploration.probes.holes)
                if(saved.planet==planet.address&&probeSeconds>=saved.impactTime()&&glm::length(saved.entry-metres)<2500){
                    auto b=saved;if(probeSeconds<b.blastTime())b.shaftRadius=1.3;localOpenings.push_back(b);
                }
        }
        for (const auto& patch : terrain.patches()) {
            auto displayed = patch.geometry;
            if (!localOpenings.empty()) {
                std::vector<Borehole> openings;
                const auto& positions = patch.geometry->precisePositions;
                if (!positions.empty()) {
                    glm::dvec3 middle{};
                    for (const auto& point : positions) middle += point/double(positions.size());
                    double span = 0;
                    for (const auto& point : positions) span = std::max(span,glm::length(point-middle));
                    for (const auto& saved : localOpenings)
                        if (glm::length(saved.entry/std::pow(10.,patch.geometry->logUnits)-middle) <
                                span+saved.shaftRadius*2/std::pow(10.,patch.geometry->logUnits)) {
                            auto b=saved;
                            openings.push_back(b);
                        }
                }
                if (!openings.empty())
                    if (auto cut = cache.requestExcavatedTerrain(patch.geometry,std::move(openings))) displayed=cut;
            }
            const auto& g = *displayed;
            if (displayed != patch.geometry) ++stats.cutTerrainTiles;
            if(exteriorHidden)continue; // Keep aperture jobs warm without drawing covered terrain.
            const auto vertexCount = uint32_t(g.solid.vertices.size());
            if (!vertexCount || !g.solid.indexCount()) continue;
            std::array<glm::dvec3,81> ordinaryPoints;
            std::vector<glm::dvec3> extraPoints(vertexCount==81?0:vertexCount);
            std::span<glm::dvec3> points=vertexCount==81?std::span<glm::dvec3>(ordinaryPoints)
                                                      :std::span<glm::dvec3>(extraPoints);
            glm::dvec3 centre{};
            for (unsigned i = 0; i < points.size(); ++i) {
                const auto position = displayed == patch.geometry ? terrain.position(patch,i) : g.precisePositions[i];
                points[i] = transform.linear * (position - transform.pivot) +
                            (transform.translation - eye);
                centre += points[i] / double(points.size());
            }
            double radius = 0;
            for (auto p : points)
                radius = std::max(radius, glm::length(p - centre));
            const bool visible =
                LOD::visible(cameraInverse * centre, radius, double(width) / height, fieldOfView);
            // Caster visibility is evaluated in the light frame, independently
            // of the cockpit view. Reuse exactly the current geomorphed vertices.
            bool caster = false;
            if (updateSun) {
                const auto q = glm::dvec3(sun.textureFromWorld * glm::dvec4(centre, 1));
                const double r = radius * std::pow(10., nav.logDistance) / sun.spanMetres;
                caster =
                    q.x + r > 0 && q.y + r > 0 && q.z + r > 0 && q.x - r < 1 && q.y - r < 1 && q.z - r < 1;
            }
            if (!visible && !caster)
                continue;
            const bool smoothTerrain = g.terrainNormals.size() == vertexCount;
            const uint32_t count = uint32_t(g.solid.indexCount());
            if ((smoothTerrain && bgfx::getAvailTransientVertexBuffer(vertexCount, normalLayout) < vertexCount) ||
                bgfx::getAvailTransientVertexBuffer(vertexCount, layout) < vertexCount ||
                bgfx::getAvailTransientIndexBuffer(count, g.solid.wideIndices()) < count ||
                bgfx::getAvailInstanceDataBuffer(1, 80) < 1) {
                ++stats.uploadDeferred;
                if (caster)
                    terrainSunComplete = false;
                continue;
            }
            bgfx::TransientVertexBuffer vertices, normals;
            bgfx::TransientIndexBuffer indices;
            bgfx::InstanceDataBuffer instances;
            bgfx::allocTransientVertexBuffer(&vertices, vertexCount, layout);
            bgfx::allocTransientIndexBuffer(&indices, count, g.solid.wideIndices());
            bgfx::allocInstanceDataBuffer(&instances, 1, 80);
            auto data = reinterpret_cast<Vertex*>(vertices.data);
            for (unsigned i = 0; i < points.size(); ++i)
                data[i] = {float(points[i].x), float(points[i].y), float(points[i].z),
                           smoothTerrain
                               ? (g.exposureColors.size() == vertexCount ? g.exposureColors[i] : g.terrainAlbedo[i])
                               : g.solid.vertices[i].color};
            if (smoothTerrain) {
                bgfx::allocTransientVertexBuffer(&normals, vertexCount, normalLayout);
                auto output = reinterpret_cast<ShadingNormal*>(normals.data);
                for (unsigned i = 0; i < vertexCount; ++i)
                    output[i] = packNormal(glm::vec3(
                        glm::normalize(glm::dmat3(transform.linear) * glm::dvec3(g.terrainNormals[i]))));
            }
            std::memcpy(indices.data, g.solid.indexData(), count * g.solid.indexWidth());
            const Instance identity{glm::mat4(1), glm::vec4(1)};
            std::memcpy(instances.data, &identity, sizeof(identity));
            if (caster) {
                bgfx::setVertexBuffer(0, &vertices);
                bgfx::setIndexBuffer(&indices);
                bgfx::setInstanceDataBuffer(&instances);
                bgfx::setState(BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS);
                bgfx::submit(70, hyperion->sunProgram());
            }
            stats.uploadBytes +=
                vertexCount * sizeof(Vertex) + count * g.solid.indexWidth() +
                (smoothTerrain ? vertexCount * sizeof(ShadingNormal) : 0);
            if (!visible)
                continue;
            auto presentation = extractPresentation(planet, g, mode);
            const auto light = lightAt(planet.address, nav);
            presentation.planet[4] = glm::vec4(glm::vec3(glm::inverse(poseFor(planet.address, nav).orientation)*light),0);
            glm::vec4 cavity{};
            if (materialInspectionView(nav.anchor.kind(), bool(instruments), instruments && instruments->shrinking))
                cavity = glm::vec4(glm::vec3(-eye), float(std::max(1.3, glm::length(eye) * 1.1)));
            glm::mat4 micro(0), meso(0);
            float relief = 0, mesoRelief = 0;
            {
                const double radiusMetres = std::pow(10., planet.frame.logRadius);
                micro = periodicMaterialFrame(pattern, radiusMetres, 8, 64);
                meso = periodicNoiseFrame(pattern, radiusMetres, 256, 4096);
                relief = float(.36 / std::pow(10., nav.logDistance));
                mesoRelief = float(8. / std::pow(10., nav.logDistance));
            }
            hyperion->bind(presentation, {}, glm::vec3(light), glm::mat4(pattern),
                           smoothTerrain ? -2.f : -1.f, visualTime, cavity, micro, relief, meso, mesoRelief);
            bgfx::setVertexBuffer(0, &vertices);
            if (smoothTerrain)
                bgfx::setVertexBuffer(1, &normals);
            bgfx::setIndexBuffer(&indices);
            bgfx::setInstanceDataBuffer(&instances);
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
                           BGFX_STATE_DEPTH_TEST_GREATER | BGFX_STATE_MSAA);
            bgfx::submit(1, terrainCoverageDebug ? hyperion->coverageProgram()
                                                 : hyperion->geometryProgram(smoothTerrain));
            ++stats.terrainTiles;
            stats.vertices += vertexCount;
            stats.triangles += count / 3;
        }
        if (updateSun)
            hyperion->completeSun(terrainSunComplete);
        if (terrain.morphing())
            ++stats.lodTransitions;
    }
    bool enclosedCavernView(const Navigation& nav,RegionCache& cache) {
        enclosedBore=0;
        if(!probeOptimisation||!economy||specimen||nav.anchor.kind()>Kind::Surface)return false;
        if(!probeRemote&&(!instruments||instruments->shrinking))return false;
        if(nav.selected&&nav.selected->kind()>=Kind::Rock)return false;
        const double units=std::pow(10.,nav.logDistance);
        for(const auto& b:economy->exploration.probes.holes){
            if(probeSeconds<b.impactTime())continue;
            if(economy->network&&b.planet.parent()!=economy->network->system)continue;
            const auto planet=universe.describe(b.planet);const auto pose=poseFor(b.planet,nav);
            const auto planetEye=glm::inverse(pose.orientation)*(eye-pose.position)*units;
            if(glm::length(planetEye-b.entry)>b.finalDepth()+b.roomRadius()+40)continue;
            const bool complete=probeSeconds>=b.blastTime();
            const auto geometry=cache.requestBore(planet,b,complete,
                complete?b.finalDepth():probeOperation(b,probeSeconds).reached);
            if(!geometry||geometry->solid.vertices.size()<boreSectors||!prepareGPU(geometry))continue;
            double radius=b.shaftRadius;
            for(unsigned k=0;k<boreSectors;++k){const auto& v=geometry->solid.vertices[k];
                radius=std::max(radius,glm::length(glm::dvec3(v.x,v.y,v.z)));}
            if(boreEnclosesView(b,probeSeconds,planetEye,glm::inverse(pose.orientation)*(cameraQ*glm::dvec3(0,0,-1)),
                                fieldOfView,double(width)/height,radius+.5,worldNearPlane*units)){
                enclosedBore=b.id;return true;
            }
        }
        return false;
    }
    void renderBores(const Navigation& nav, RegionCache& cache) {
        if (!economy) return;
        const auto rotation = glm::mat4_cast(glm::quat(cameraInverse));
        const glm::mat4 projectionMatrix(projection);
        bgfx::setViewTransform(1,glm::value_ptr(rotation),glm::value_ptr(projectionMatrix));
        bgfx::setViewRect(1,0,0,uint16_t(width),uint16_t(height));
        std::array<std::pair<const Borehole*,double>,maxBoreholes> ranked{};
        size_t count=0;
        for(const auto& b:economy->exploration.probes.holes){
            if(economy->network&&b.planet.parent()!=economy->network->system)continue;
            const auto planet=universe.describe(b.planet);
            const auto point=universe.pointRelative(b.planet,b.entry/std::pow(10.,planet.frame.logRadius),nav.anchor,nav.logDistance);
            double distance=glm::length(point-eye)*std::pow(10.,nav.logDistance);
            if(probeSeconds<b.impactTime()){
                const auto sample=probePose(b,probeSeconds);
                const auto head=universe.pointRelative(b.planet,sample.position/std::pow(10.,planet.frame.logRadius),nav.anchor,nav.logDistance);
                distance=std::min(distance,glm::length(head-eye)*std::pow(10.,nav.logDistance));
            }
            // The enclosing mesh must survive the eight-cave draw limit,
            // even if many other entrances are nearer than its surface rim.
            if(b.id==enclosedBore)distance=-1;
            if(distance<2500&&count<ranked.size())ranked[count++]={&b,distance};
        }
        std::sort(ranked.begin(),ranked.begin()+count,[](const auto& a,const auto& b){return a.second<b.second;});
        unsigned visible = 0;
        for (size_t index=0;index<std::min(size_t(8),count);++index) {
            const auto& b=*ranked[index].first;
            if (economy->network && b.planet.parent()!=economy->network->system) continue;
            const auto planet = universe.describe(b.planet);
            const auto pose = poseFor(b.planet,nav);
            const auto point = universe.pointRelative(b.planet,b.entry/std::pow(10.,planet.frame.logRadius),
                                                       nav.anchor,nav.logDistance);
            const double scale = std::pow(10.,-nav.logDistance);
            if (ranked[index].second > 2500 || visible >= 8) continue;
            ++visible;
            const auto state=probePose(b,probeSeconds);
            auto pre = cache.requestBore(planet,b,false,probeOperation(b,probeSeconds).reached);
            auto open = cache.requestBore(planet,b,true);
            const auto visual=probeVisual(b,probeSeconds,material(planet.environment.mineral).density);
            const auto q = pose.orientation*tangentFrame(glm::normalize(b.entry));
            const auto* feed=economy->network?latestProbe(economy->exploration.probes,economy->network->system):nullptr;
            if(probeRemote&&feed&&feed->id==b.id){
                if(!bgfx::isValid(probeHousing.vertices)){
                    const auto rig=probeRig();
                    probeHousing=upload(rig.housing);probeCutter=upload(rig.cutter);probeLamps=upload(rig.lamps);
                    probeRoller=upload(rig.roller);probeDrive=upload(rig.drive);probeActuator=upload(rig.actuator);probeHotTeeth=upload(rig.hotTeeth);
                    auto polished=rig.actuator;for(auto& v:polished.vertices)v.color=rgba({.46f,.56f,.64f});
                    probeRod=upload(polished);
                    probeRigBytes=rig.housing.bytes()+rig.cutter.bytes()+rig.lamps.bytes()+rig.roller.bytes()+rig.drive.bytes()+rig.actuator.bytes()+rig.hotTeeth.bytes()+polished.bytes();
                }
                const auto lens=universe.pointRelative(b.planet,state.position/std::pow(10.,planet.frame.logRadius),nav.anchor,nav.logDistance);
                const auto base=glm::translate(glm::mat4(1),glm::vec3(lens-eye))*
                    glm::mat4_cast(glm::quat(pose.orientation*state.attitude))*glm::scale(glm::mat4(1),glm::vec3(float(scale)));
                auto part=[&](const GPUMesh& mesh,const glm::mat4& model,bool lamp,glm::vec3 tint=glm::vec3(1),bool polished=false){
                    if(!bgfx::isValid(mesh.vertices))return;
                    if(!lamp&&!polished){
                        // Large fixed-finish plates retain authored face shading.
                        // Only rollers need the more expensive moving specular
                        // response; both paths share the Hyperion HDR display.
                        bgfx::setTransform(glm::value_ptr(model));bgfx::setVertexBuffer(0,mesh.vertices);bgfx::setIndexBuffer(mesh.indices);
                        bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A|BGFX_STATE_WRITE_Z|BGFX_STATE_DEPTH_TEST_GREATER|BGFX_STATE_MSAA);
                        bgfx::submit(1,colorProgram);stats.probeRigTriangles+=mesh.count/3;return;
                    }
                    if(!bgfx::getAvailInstanceDataBuffer(1,80))return;
                    bgfx::InstanceDataBuffer instance;bgfx::allocInstanceDataBuffer(&instance,1,80);
                    const Instance item{model,glm::vec4(tint,1)};std::memcpy(instance.data,&item,sizeof(item));
                    Geometry dummy;
                    const ScenePresentation metal{dummy,b.planet,{.48f,.58f,0,0},hyperionPhase(b.id),1,{3,0,0,0}};
                    hyperion->bind(metal,{},glm::vec3(cameraQ*glm::dvec3(-.3,.5,1)),glm::mat4(1),lamp?2.f:0.f,probeSeconds);
                    hyperion->localLamp();bgfx::setVertexBuffer(0,mesh.vertices);bgfx::setIndexBuffer(mesh.indices);
                    bgfx::setInstanceDataBuffer(&instance);
                    bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A|BGFX_STATE_WRITE_Z|BGFX_STATE_DEPTH_TEST_GREATER|BGFX_STATE_MSAA);
                    bgfx::submit(1,hyperion->geometryProgram());stats.probeRigTriangles+=mesh.count/3;
                };
                part(probeHousing,base,false);part(probeLamps,base,true);
                const auto mechanism=probeMechanism(visual);
                part(probeCutter,base*glm::mat4(mechanism.head),false);
                part(probeDrive,base*glm::mat4(mechanism.drive),false);
                if(visual.heat>.02)part(probeHotTeeth,base*glm::mat4(mechanism.head),true,glm::vec3(float(visual.heat)));
                for(const auto& roller:mechanism.rollers)part(probeRoller,base*glm::mat4(roller),false,{1,1,1},true);
                for(unsigned piston=0;piston<2;++piston){
                    part(probeActuator,base*glm::mat4(mechanism.barrels[piston]),false,{.42f,.51f,.62f});
                    part(probeRod,base*glm::mat4(mechanism.rods[piston]),false);
                }
            }
            if(!probeRemote&&(state.phase==ProbePhase::Flight||state.phase==ProbePhase::Beacon)){
                const auto metres=state.phase==ProbePhase::Flight?state.position+state.forward*2.4:state.position;
                const auto head=universe.pointRelative(b.planet,metres/std::pow(10.,planet.frame.logRadius),nav.anchor,nav.logDistance);
                const auto forward=pose.orientation*state.forward;
                if(glm::length(head-eye)/scale>2){
                    Mesh pod;
                    appendSphere(pod,head-eye,.24*scale,{.35f,.65f,.82f},5,8);
                    appendSphere(pod,head-eye-forward*(.55*scale),.18*scale,{.22f,.32f,.48f},4,8);
                    transient(pod,1,false,false,true,true);
                    addBillboard(head,.16*scale,{.35f,.85f,1.4f});
                    if(state.phase==ProbePhase::Flight)
                        for(unsigned k=0;k<12;++k){const double length=std::min(double(k+1),state.progress*glm::length(b.entry-b.launch));
                            addBillboard(head-forward*(length*scale),(.12+k*.012)*scale,
                                glm::vec3(.3f,.65f,1.2f)*float(std::exp(-k*.35)));}
                }
            }
            if (probeSeconds < b.impactTime()) continue;
            const bool complete = probeSeconds >= b.blastTime();
            const auto geometry = complete ? open : pre;
            if (!geometry || !prepareGPU(geometry)) continue;
            auto& uploaded = gpu.at(geometry.get()); uploaded.lastUsed = frame;
            const auto progress = complete ? 1. : std::max(.01,state.progress);
            const auto model = glm::translate(glm::mat4(1),glm::vec3(point-eye))*
                glm::mat4_cast(glm::quat(q))*glm::scale(glm::mat4(1),glm::vec3(float(scale)));
            // Keep the work-light stream in the geometry's existing GPU budget.
            // A stationary beacon reuses it; real camera/light changes update it.
            const auto localEye=glm::inverse(q)*(eye-point)/scale;
            const auto localForward=probeRemote&&feed&&feed->id==b.id
                ?glm::inverse(tangentFrame(glm::normalize(b.entry)))*state.forward
                :glm::inverse(q)*(cameraQ*glm::dvec3(0,0,-1));
            const auto nv=uint32_t(geometry->solid.vertices.size());
            const auto wear=cavernWear(universe,b,economy->exploration);
            auto lightingBore=b;lightingBore.depth=b.finalDepth()*progress;lightingBore.chamberRadius=b.roomRadius();
            lightingBore.controls.clear();
            if(probeOptimisation){
                const auto lighting=probeLighting(lightingBore,visual,localEye,localForward,complete);
                if(!bgfx::isValid(uploaded.probeLight)){
                    uploaded.probeLight=bgfx::createDynamicVertexBuffer(nv,layout);
                    if(!bgfx::isValid(uploaded.probeLight))throw std::runtime_error("Probe work-light allocation failed");
                    // The generic upload's vertex buffer is no longer needed:
                    // replace it, instead of retaining a second GPU copy.
                    if(bgfx::isValid(uploaded.solid.vertices))bgfx::destroy(uploaded.solid.vertices);
                    uploaded.solid.vertices=BGFX_INVALID_HANDLE;
                }
                if(!uploaded.probeLightKey||!(*uploaded.probeLightKey==lighting)||uploaded.miningWearKey!=wear.key){
                    probeLightScratch.resize(nv);
                    for(unsigned n=0;n<nv;++n){const auto& v=geometry->solid.vertices[n];
                        const double y=complete?double(v.y):std::max(double(v.y),-lightingBore.depth);
                        const auto albedo=glm::vec3(v.color&255,(v.color>>8)&255,(v.color>>16)&255)/255.f;
                        const auto surface=complete&&b.geologyVersion?wear.shade({v.x,y,v.z},albedo):albedo;
                        probeLightScratch[n]={v.x,float(y),v.z,rgba(lighting.shade({v.x,y,v.z},surface))};
                    }
                    bgfx::update(uploaded.probeLight,0,bgfx::copy(probeLightScratch.data(),nv*sizeof(Vertex)));
                    uploaded.probeLightKey=lighting;uploaded.miningWearKey=wear.key;stats.uploadBytes+=nv*sizeof(Vertex);++stats.probeLightUploads;
                }
                bgfx::setVertexBuffer(0,uploaded.probeLight);
            }else{
                if(bgfx::getAvailTransientVertexBuffer(nv,layout)<nv)continue;
                bgfx::TransientVertexBuffer lit;bgfx::allocTransientVertexBuffer(&lit,nv,layout);
                auto vertices=reinterpret_cast<Vertex*>(lit.data);
                for(unsigned n=0;n<nv;++n){const auto& v=geometry->solid.vertices[n];
                    const double y=complete?double(v.y):std::max(double(v.y),-lightingBore.depth);
                    const auto albedo=glm::vec3(v.color&255,(v.color>>8)&255,(v.color>>16)&255)/255.f;
                    const auto surface=complete&&b.geologyVersion?wear.shade({v.x,y,v.z},albedo):albedo;
                    vertices[n]={v.x,float(y),v.z,rgba(probeWallLight(lightingBore,visual,{v.x,y,v.z},localEye,localForward,surface,complete))};
                }
                bgfx::setVertexBuffer(0,&lit);stats.uploadBytes+=nv*sizeof(Vertex);++stats.probeLightUploads;
            }
            bgfx::setTransform(glm::value_ptr(model));bgfx::setIndexBuffer(uploaded.solid.indices);
            bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A|BGFX_STATE_WRITE_Z|BGFX_STATE_DEPTH_TEST_GREATER|BGFX_STATE_MSAA);
            bgfx::submit(1,colorProgram);stats.probeLitVertices+=nv;
            stats.vertices += geometry->solid.vertices.size(); stats.triangles += geometry->solid.indexCount()/3;
            ++stats.cavernMeshes;++stats.visibleObjects;stats.cavernTriangles+=geometry->solid.indexCount()/3;
            if(complete&&b.geologyVersion){
                if(wear.minedKg)stats.miningWearVertices+=nv;
                auto tailings=cavernTailings(b,wear);stats.miningTailings+=tailings.vertices.size()/4;
                const auto lighting=probeLighting(lightingBore,visual,localEye,localForward,true);
                for(auto& v:tailings.vertices){const auto albedo=glm::vec3(v.color&255,(v.color>>8)&255,(v.color>>16)&255)/255.f;
                    v.color=rgba(lighting.shade({v.x,v.y,v.z},albedo));const auto p=point-eye+q*glm::dvec3(v.x,v.y,v.z)*scale;v.x=float(p.x);v.y=float(p.y);v.z=float(p.z);}
                bgfx::setTransform(glm::value_ptr(glm::mat4(1)));transient(tailings,1,false,false,true,true);
                auto fixtures=excavationFixtures(lightingBore);
                stats.excavationFixtureVertices+=fixtures.vertices.size();
                for(auto& v:fixtures.vertices){const auto albedo=glm::vec3(v.color&255,(v.color>>8)&255,(v.color>>16)&255)/255.f;
                    v.color=rgba(lighting.shade({v.x,v.y,v.z},albedo));const auto p=point-eye+q*glm::dvec3(v.x,v.y,v.z)*scale;
                    v.x=float(p.x);v.y=float(p.y);v.z=float(p.z);}
                transient(fixtures,1,false,false,true,true);
            }
            if (visual.flash > .001) {
                const auto centre=point-pose.orientation*glm::normalize(b.entry)*(b.finalDepth()*scale);
                addBillboard(centre,(.3+3*std::max(0.,visual.blastAge))*scale,glm::vec3(3,1.1,.2)*float(visual.flash));
            }
            stats.probeFlash=std::max(stats.probeFlash,visual.flash);
            auto motes=probeMotes(b,visual,probeSeconds,material(planet.environment.mineral).color);
            if(complete&&b.geologyVersion) {
                Mesh cracks;
                const glm::dvec3 centre(0,-lightingBore.finalDepth(),0);
                for(const auto& face:cavernFinds(lightingBore)) {
                    const auto& state=lightingBore.excavation[face.index];stats.looseOreKg+=state.looseKg;
                    const bool active=excavationWork.bore==b.id&&excavationWork.face==face.index;
                    const double strain=excavationStrain(lightingBore,face,active?excavationWork.progress:0,active&&excavationWork.careful);
                    if(strain>.02)for(const auto& crack:iceCracks(lightingBore,{face,excavationProfile(lightingBore,face).weakness})) {
                        if(strain<crack.threshold)continue;
                        const auto radial=glm::normalize((crack.from+crack.to)*.5-centre);
                        const auto across=glm::normalize(glm::cross(radial,crack.to-crack.from))*(.008+.035*strain);
                        const auto first=uint32_t(cracks.vertices.size());
                        for(const auto end:{crack.from,crack.to})for(int side=-1;side<=1;++side) {
                            const auto p=point-eye+q*(end+across*double(side))*scale;
                            const auto colour=side==0?glm::vec3(.009f,.006f,.009f):material(face.mineral).color*float(.65+.3*strain);
                            cracks.vertices.push_back({float(p.x),float(p.y),float(p.z),rgba(colour)});
                        }
                        cracks.indices.insert(cracks.indices.end(),{first,first+3,first+1,first+1,first+3,first+4,
                            first+1,first+4,first+2,first+2,first+4,first+5});++stats.excavationCracks;
                    }
                    const auto debris=excavationMotes(lightingBore,face,strain,probeSeconds);
                    stats.excavationParticles+=debris.count;
                    for(size_t k=0;k<debris.count&&motes.count<motes.items.size();++k)motes.items[motes.count++]=debris.items[k];
                }
                bgfx::setTransform(glm::value_ptr(glm::mat4(1)));transient(cracks,1,false,false,true,true);
            }
            if (complete) {
                const auto fault = iceFault(lightingBore);
                if (fault && glm::length(localEye - fault->face.local) < lightingBore.chamberRadius * 2 + 4) {
                    const bool active = iceStress.bore == b.id;
                    const bool falling = active && iceStress.falling();
                    const double strain = active ? iceStress.load : 0.;
                    Mesh cracks;
                    cracks.vertices.reserve(144);
                    cracks.indices.reserve(288);
                    const glm::dvec3 roomCentre(0, -lightingBore.depth, 0);
                    const float opening = falling ? 1.f : float(strain);
                    const auto rim = glm::mix(glm::vec3(.20f, .39f, .48f), glm::vec3(.72f, .91f, 1.f), opening);
                    const auto shadow = rgba({.035f, .075f, .11f});
                    for (const auto& crack : iceCracks(lightingBore, *fault)) {
                        if (!falling && strain < crack.threshold)
                            continue;
                        const auto radial = glm::normalize((crack.from + crack.to) * .5 - roomCentre);
                        const auto across = glm::normalize(glm::cross(radial, crack.to - crack.from)) * (.014 + .042 * opening);
                        const auto first = uint32_t(cracks.vertices.size());
                        for (const auto end : {crack.from, crack.to}) {
                            for (int side = -1; side <= 1; ++side) {
                                const auto p = point - eye + q * (end + across * double(side)) * scale;
                                const auto colour = side == 0 ? shadow : rgba(rim * (side < 0 ? .6f : 1.f));
                                cracks.vertices.push_back({float(p.x), float(p.y), float(p.z), colour});
                            }
                        }
                        cracks.indices.insert(cracks.indices.end(), {first, first+3, first+1, first+1, first+3, first+4,
                                                                     first+1, first+4, first+2, first+2, first+4, first+5});
                    }
                    bgfx::setTransform(glm::value_ptr(glm::mat4(1)));
                    transient(cracks, 1, false, false);
                    if (falling) {
                        const auto chips = iceFallMotes(lightingBore, *fault, iceStress.fallAge);
                        for (size_t i = 0; i < chips.count && motes.count < motes.items.size(); ++i)
                            motes.items[motes.count++] = chips.items[i];
                    }
                }
            }
            if(complete&&b.geologyVersion&&weapons){
                for(const auto& shot:weapons->shots())if(shot.utility&&!shot.enemy&&shot.age<.12&&shot.reference==b.planet){
                    const auto local=glm::inverse(tangentFrame(glm::normalize(b.entry)))*(shot.to-b.entry);
                    const auto faces=cavernFinds(b);const CavernFind* closest=nullptr;double distance=4;
                    for(const auto& f:faces){const auto d=glm::length(local-(f.local+f.inward*1.6));if(d<distance){closest=&f;distance=d;}}
                    if(closest){const auto cutting=cavernMiningMotes(b,*closest,probeSeconds);stats.miningParticles+=cutting.count;
                        for(size_t n=0;n<cutting.count&&motes.count<motes.items.size();++n)motes.items[motes.count++]=cutting.items[n];break;}
                }
            }
            Mesh localFragments,localDust,localSparks;
            auto& fragments=probeOptimisation?probeFragments:localFragments;
            auto& dust=probeOptimisation?probeDust:localDust;
            auto& sparks=probeOptimisation?probeSparks:localSparks;
            for(auto* mesh:{&fragments,&dust,&sparks}){mesh->vertices.clear();mesh->indices.clear();}
            if(probeOptimisation){
                fragments.vertices.reserve(128);fragments.indices.reserve(384);
                dust.vertices.reserve(960);dust.indices.reserve(2304);
                sparks.vertices.reserve(24);sparks.indices.reserve(24);
            }
            static const auto circle=[] {std::array<glm::dvec3,9> points{};
                for(unsigned k=0;k<points.size();++k){const double a=k*6.283185307179586/8;points[k]={std::cos(a),std::sin(a),0};}return points;}();
            for(size_t k=0;k<motes.count;++k){const auto& mote=motes.items[k];
                const auto centre=point-eye+q*mote.position*scale;
                if(mote.spark){
                    addBillboard(centre+eye,mote.radius*scale,mote.colour*float(mote.opacity));
                    const auto tail=centre+q*glm::dvec3(-mote.position.z*.035,-.10,mote.position.x*.035)*scale;
                    addLine(sparks,centre,tail,mote.colour*float(mote.opacity*.7));
                }else if(mote.fragment){
                    const auto size=mote.radius*scale;const auto start=uint32_t(fragments.vertices.size());
                    const auto colour=mote.colour*float(.22+visual.flash*.8);
                    for(const auto corner:{glm::dvec3(1,0,0),glm::dvec3(-.5,.8,0),glm::dvec3(-.5,-.5,.7),glm::dvec3(0,0,-.7)}){
                        const auto p=centre+q*(corner*mote.stretch)*size;fragments.vertices.push_back({float(p.x),float(p.y),float(p.z),rgba(colour,float(mote.opacity))});}
                    fragments.indices.insert(fragments.indices.end(),{start,start+1,start+2,start,start+3,start+1,start,start+2,start+3,start+1,start+3,start+2});
                }else{
                    const auto start=uint32_t(dust.vertices.size());
                    const auto tint=glm::vec3(.12f,.16f,.19f)+mote.colour*.12f+glm::vec3(1,.4,.1)*float(visual.flash*.4);
                    dust.vertices.push_back({float(centre.x),float(centre.y),float(centre.z),rgba(tint,mote.opacity)});
                    for(unsigned side=0;side<9;++side){
                        const auto p=centre+cameraQ*circle[side]*(mote.radius*scale);
                        dust.vertices.push_back({float(p.x),float(p.y),float(p.z),rgba(tint,0)});
                        if(side)dust.indices.insert(dust.indices.end(),{start,start+side,start+side+1});}
                    ++stats.probeDustParticles;
                }
            }
            transient(fragments,1,false,false,true,true,true);
            transient(sparks,1,true,true,true);
            bgfx::setViewTransform(64,glm::value_ptr(rotation),glm::value_ptr(projectionMatrix));
            bgfx::setViewRect(64,0,0,uint16_t(width),uint16_t(height));
            // Soft alpha dust is drawn after opaque geometry, with depth
            // testing retained so the cloud cannot shine through solid rock.
            transient(dust,64,false,false,true,false,true);
        }
    }
    void prune() {
        std::erase_if(details, [&](const auto& state) { return frame > state.second.used + 2; });
        std::erase_if(localSections, [&](const auto& state) { return frame > state.second.used + 2; });
        while (true) {
            auto victim = gpu.end();
            for (auto it = gpu.begin(); it != gpu.end(); ++it)
                if (it->second.source.expired() || frame > it->second.lastUsed + 120 ||
                    gpuBytes > 32 * 1024 * 1024) {
                    if (victim == gpu.end() || it->second.lastUsed < victim->second.lastUsed)
                        victim = it;
                }
            if (victim == gpu.end())
                break;
            gpuBytes -= victim->second.bytes;
            destroy(victim->second.solid);
            destroy(victim->second.lines);
            if(bgfx::isValid(victim->second.probeLight))bgfx::destroy(victim->second.probeLight);
            if (victim->second.normalWork)
                victim->second.normalWork->cancel.store(true);
            if (bgfx::isValid(victim->second.normals))
                bgfx::destroy(victim->second.normals);
            if (bgfx::isValid(victim->second.instances))
                bgfx::destroy(victim->second.instances);
            gpu.erase(victim);
        }
    }
};
Renderer::Renderer(SDL_Window* window, const Universe& universe, bool benchmark)
    : impl_(std::make_unique<Impl>(window, universe)) {
    auto& p = *impl_;
    bgfx::Init init;
    init.callback = &p.callbacks;
    init.fallback = false;
    auto props = SDL_GetWindowProperties(window);
#if defined(__APPLE__)
    init.type = bgfx::RendererType::Metal;
    init.swapChain.nwh = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
#elif defined(_WIN32)
    init.type = bgfx::RendererType::Direct3D11;
    init.swapChain.nwh = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#else
    init.type = bgfx::RendererType::Vulkan;
    init.swapChain.nwh = reinterpret_cast<void*>(
        static_cast<uintptr_t>(SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0)));
    init.swapChain.ndt = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
    if (!init.swapChain.nwh)
        throw std::runtime_error("This build uses X11. Start with SDL_VIDEODRIVER=x11 on Wayland.");
#endif
    SDL_GetWindowSizeInPixels(window, &p.width, &p.height);
    init.swapChain.width = p.width;
    init.swapChain.height = p.height;
    init.swapChain.flags = BGFX_SWAP_CHAIN_MSAA_X4;
    // Reversed-Z needs floating-point storage in Original mode too. A fixed
    // 24-bit default swap-chain depth buffer would retain the far-world bug.
    init.swapChain.formatDepthStencil = bgfx::TextureFormat::D32F;
    p.resetFlags = BGFX_RESET_MAXANISOTROPY | (benchmark ? 0 : BGFX_RESET_VSYNC);
    init.reset = p.resetFlags;
    init.limits.maxTransientVbSize = 8 * 1024 * 1024;
    init.limits.maxTransientIbSize = 3 * 1024 * 1024;
    if (!init.swapChain.nwh)
        throw std::runtime_error("SDL did not provide a native window handle");
    if (!bgfx::init(init))
        throw std::runtime_error("bgfx could not initialize a graphics device");
    // Shadow casters are submitted during extraction but execute before colour
    // draws. Materials sample this frame's map using their actual positions.
    std::array<bgfx::ViewId, cabinViewId+4> order{};
    order[0] = 70;
    size_t next = 1;
    for (bgfx::ViewId id = 0; id < order.size(); id++)
        if (id != 70)
            order[next++] = id;
    bgfx::setViewOrder(0, uint16_t(order.size()), order.data());

    p.initialized = true;
    p.swapChain = init.swapChain;
    p.layout.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
        .end();
    p.normalLayout.begin().add(bgfx::Attrib::Normal, 2, bgfx::AttribType::Int16, true).end();
    p.colorProgram = bgfx::createProgram(
        bgfx::createEmbeddedShader(embedded, bgfx::getRendererType(), "vs_debugdraw_lines"),
        bgfx::createEmbeddedShader(embedded, bgfx::getRendererType(), "fs_debugdraw_lines"), true);
    if (!bgfx::isValid(p.colorProgram))
        throw std::runtime_error("bgfx shader program creation failed");
    for (unsigned i = 0; i < 4; ++i) {
        Mesh sphere;
        appendSphere(sphere, {}, 1, {1, 1, 1},
                     i == 3   ? 24
                     : i == 2 ? 12
                     : i == 1 ? 8
                              : 4,
                     i == 3   ? 40
                     : i == 2 ? 20
                     : i == 1 ? 12
                              : 6);
        p.atomSpheres[i] = p.upload(sphere);
    }
    Mesh quad;
    quad.vertices = {{0, 0, 0, rgba({1, 1, 1})},
                     {-1, -1, 0, rgba({0, 0, 0})},
                     {1, -1, 0, rgba({0, 0, 0})},
                     {1, 1, 0, rgba({0, 0, 0})},
                     {-1, 1, 0, rgba({0, 0, 0})}};
    quad.indices = {0, 1, 2, 0, 2, 3, 0, 3, 4, 0, 4, 1};
    for (auto& vertex : quad.vertices)
        vertex.color = rgba({1, 1, 1});
    p.hyperionBillboardMesh = p.upload(quad);
    Mesh annulus;
    constexpr uint32_t ringSegments = 128;
    for (uint32_t i = 0; i < ringSegments; ++i) {
        const double angle = 2. * std::numbers::pi * i / ringSegments;
        const auto x = float(std::cos(angle)), z = float(std::sin(angle));
        const uint32_t a = 2 * i, b = 2 * ((i + 1) % ringSegments);
        annulus.vertices.push_back({x, 0, z, 0xffffffff});
        annulus.vertices.push_back({x, 1, z, 0xffffffff});
        annulus.indices.insert(annulus.indices.end(), {a, b, b + 1, a, b + 1, a + 1});
    }
    annulus.compact();
    p.ringMesh = p.upload(annulus);
    for (size_t i = 1; i < quad.vertices.size(); ++i) {
        quad.vertices[i].x *= float(pointLightSupport);
        quad.vertices[i].y *= float(pointLightSupport);
        quad.vertices[i].color = rgba({0, 0, 0});
    }
    p.pointLightBillboardMesh = p.upload(quad);
    p.hyperion = std::make_unique<HyperionRenderer>();
    p.instanceLayout.begin();
    for (unsigned i = 0; i < 5; ++i)
        p.instanceLayout.add(static_cast<bgfx::Attrib::Enum>(bgfx::Attrib::TexCoord0 + i), 4,
                             bgfx::AttribType::Float);
    p.instanceLayout.end();
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, UINT32_C(0x03060cff), 0.0f, uint8_t(0));
    bgfx::setViewMode(0, bgfx::ViewMode::Default);
    bgfx::setViewMode(1, bgfx::ViewMode::Sequential);
    bgfx::setDebug(BGFX_DEBUG_TEXT);
    p.billboards.reserve(40000);
}
Renderer::~Renderer() = default;
void Renderer::setMode(RenderMode mode) {
    if (mode != RenderMode::Hyperion)
        throw std::invalid_argument("Only Hyperion rendering is available");
}
RenderMode Renderer::mode() const {
    return impl_->mode;
}
void Renderer::freezeVisualTime(double seconds) {
    impl_->fixedVisualTime = seconds;
}
void Renderer::setCockpit(const FlightInstruments* instruments) {
    if (instruments)
        impl_->instruments = *instruments;
    else {
        impl_->instruments.reset();
        impl_->cockpitMotion.reset();
        impl_->head = {};
    }
}
void Renderer::setCockpitLook(glm::dvec2 yawPitch) {
    impl_->cockpitLook = yawPitch;
}
void Renderer::setShipInterior(const ShipInterior* interior) {
    impl_->shipInterior = interior;
    if (interior)
        impl_->hyperion->prepareDeckCrew();
}
void Renderer::beginSeatTransition(bool standing) {
    impl_->seatTransition.begin(impl_->head,standing);
}
bool Renderer::seatTransitionActive() const {
    return impl_->seatTransition.active();
}
void Renderer::setCockpitValidation(bool enabled) {
    impl_->validateCockpit = enabled;
}
void Renderer::setCockpitMotionEnabled(bool enabled) {
    impl_->cockpitMotionEnabled = enabled;
    if (!enabled) {
        impl_->cockpitMotion.reset();
        impl_->head = {};
    }
}
bool Renderer::cockpitMotionEnabled() const {
    return impl_->cockpitMotionEnabled;
}
CockpitMotionPose Renderer::cockpitMotionPose() const {
    return impl_->head;
}
void Renderer::cockpitImpulse(CockpitImpulse type, double strength, glm::dvec3 direction) {
    if (impl_->cockpitMotionEnabled && impl_->instruments)
        impl_->cockpitMotion.impulse(type, strength, direction);
}
void Renderer::toggleCockpitHelp() {
    impl_->cockpitHelp = !impl_->cockpitHelp;
}
void Renderer::closeCockpitHelp(){impl_->cockpitHelp=false;}
void Renderer::setXalariaMap(const XalariaMapView* map){impl_->xalariaMap=map;}
void Renderer::setXalariaEconomy(const XalariaEconomy* economy){impl_->xalariaEconomy=economy;}
void Renderer::setXalariaCombat(const XalariaCombat* combat){impl_->xalariaCombat=combat;}
void Renderer::setXalariaRadio(const XalariaRadioView* radio){impl_->xalariaRadio=radio;}
void Renderer::setXalariaStories(const XalariaStories* stories,const StoryContext& context,std::string_view next) {
    impl_->xalariaStories=stories;
    impl_->storyContext=context;
    impl_->storyRouteAction=next;
}
void Renderer::setXalariaDistress(std::optional<ActivityId> vessel){impl_->distressVessel=std::move(vessel);}
void Renderer::setXalariaShield(const ShieldReadout& shield){impl_->playerShield=shield;}
void Renderer::cutPlayerTrail(){impl_->playerTrail.cut();}
std::shared_ptr<const Geometry> Renderer::galaxySamples() const {
    return impl_->skyGeometry;
}
std::span<const CelestialContact> Renderer::celestialContacts() const {
    return impl_->contacts;
}
std::span<const ActivityContact> Renderer::activityContacts() const {
    return impl_->activityContacts;
}
std::span<const ProspectSite> Renderer::prospects() const {
    return impl_->prospectSites ? std::span<const ProspectSite>(*impl_->prospectSites)
                                : std::span<const ProspectSite>{};
}
void Renderer::setSpecimen(std::optional<Address> source) {
    impl_->specimen = std::move(source);
}
void Renderer::setProbeView(bool remote, double seconds) {
    impl_->probeRemote = remote;
    impl_->probeSeconds = seconds;
}
void Renderer::setIceStress(const IceStress& stress) { impl_->iceStress = stress; }
void Renderer::setExcavationWork(const ExcavationWork& work) { impl_->excavationWork = work; }
void Renderer::setProbeOptimisation(bool enabled) { impl_->probeOptimisation=enabled; }
void Renderer::setShipNavigation(const Navigation* nav) { impl_->shipNavigation=nav; }
std::shared_ptr<const SurfaceWorld> Renderer::surfaceWorld(const Address& planet) const {
    return impl_->surfaceLife.find(planet);
}
uint64_t Renderer::terrainGeometryDigest() const {
    uint64_t digest = 0;
    for (const auto& patch : impl_->terrain.patches())
        digest = mix64(digest ^ encodeTile(patch.tile) ^ patch.stitch ^ geometryFingerprint(*patch.geometry));
    return digest;
}
CockpitViewport Renderer::cockpitViewport() const {
    return impl_->hudViewport;
}
void Renderer::setEconomy(std::shared_ptr<const EconomySnapshot> economy, const MarketView* market) {
    impl_->economy = std::move(economy);
    impl_->market = market;
}
void Renderer::setNpcCombat(const NpcCombat* combat) {
    impl_->npcCombat = combat;
}
void Renderer::setCrew(const CrewView* crew) {
    impl_->crew = crew;
}
void Renderer::setExpeditionPlan(const ExpeditionPlan* plan) { impl_->expeditionPlan=plan; }
std::span<const ActivityBody> Renderer::combatBodies() const {
    return impl_->combatBodies;
}
void Renderer::setWeapons(const WeaponSystem* weapons) {
    impl_->weapons = weapons;
}
std::span<const ActivityBody> Renderer::weaponTargets() const {
    return impl_->weaponTargets;
}
void Renderer::setActivity(const ActivityState* state) {
    impl_->activity = state;
}
bool Renderer::contactsToggleHit(double x,double y) const {
    const auto& p=*impl_;if(!p.cockpitVisible)return false;
    const auto point=p.hudViewport.toCanvas({x,y});
    return std::any_of(p.cockpitContactHits.begin(),p.cockpitContactHits.end(),[&](const auto& hit){return hit.catalogue&&hit.bounds.contains(point);});
}
bool Renderer::crewMinimapHit(double x,double y) const {
    const auto& p=*impl_;
    if(!p.cockpitVisible||!p.crew)return false;
    const auto panel=crewMinimap(*p.crew,p.hudViewport.aspect()).panel;
    if(!panel.contains(p.hudViewport.toCanvas({x,y})))return false;
    // Only the card actually submitted this frame is interactive. Modal,
    // remote-camera and hidden-HUD views cannot activate an invisible map.
    return std::any_of(p.cockpitHitRegions.begin(),p.cockpitHitRegions.end(),[&](const auto& r){
        return r.x==panel.x&&r.y==panel.y&&r.w==panel.w&&r.h==panel.h;});
}
bool Renderer::engineeringHit(double x,double y) const {
    const auto& p=*impl_;if(!p.cockpitVisible||!p.crew)return false;
    const auto panel=EngineeringLayout(p.hudViewport.aspect()).panel;
    return panel.contains(p.hudViewport.toCanvas({x,y}))&&std::any_of(p.cockpitHitRegions.begin(),p.cockpitHitRegions.end(),[&](const auto& r){
        return r.x==panel.x&&r.y==panel.y&&r.w==panel.w&&r.h==panel.h;});
}
std::optional<ActivityId> Renderer::pickActivity(double x, double y) const {
    const auto& p = *impl_;
    if (p.cockpitVisible && (p.cockpitHelp || (p.instruments&&p.instruments->discovery&&p.instruments->discovery->open) || (p.instruments&&p.instruments->vessel&&p.instruments->vessel->open) || (p.expeditionPlan&&p.expeditionPlan->open) || (p.market && p.market->open) || (p.crew && p.crew->open) ||
                             p.instruments->phase == "MINING PROBE CAMERA"))
        return {};
    if (!p.activity || !p.activity->enabled)
        return {};
    if(p.cockpitVisible){const auto point=p.hudViewport.toCanvas({x,y});
        for(const auto& hit:p.cockpitContactHits)if(hit.bounds.contains(point))return hit.activity;
    }
    if (p.cockpitVisible &&
        std::any_of(p.cockpitHitRegions.begin(), p.cockpitHitRegions.end(),
                    [&](const auto& r) { return r.contains(p.hudViewport.toCanvas({x, y})); }))
        return {};
    double best = 18 * 18;
    std::optional<ActivityId> selected;
    for (const auto& [id, point] : p.activityPicks) {
        const double distance = glm::dot(point - glm::dvec2(x, y), point - glm::dvec2(x, y));
        if (distance < best) {
            best = distance;
            selected = id;
        }
    }
    return selected;
}
void Renderer::setSceneReflections(bool enabled){impl_->hyperion->setReflections(enabled);}
void Renderer::setSpaceGlass(bool enabled){impl_->hyperion->setSpaceGlass(enabled);}
void Renderer::setLensEffects(bool enabled){impl_->hyperion->setLens(enabled);}
void Renderer::setSunShadows(bool enabled) {
    impl_->sunShadows = enabled;
}
void Renderer::setPolarLights(bool enabled) {
    impl_->polarLights = enabled;
}
void Renderer::setStellarOptics(bool enabled) {
    impl_->stellarOptics = enabled;
}
void Renderer::setPointLightFiltering(bool enabled) {
    impl_->pointLightFiltering = enabled;
}
void Renderer::setLandingShade(bool enabled) {
    impl_->landingShade = enabled;
}
void Renderer::setPlanetRings(bool enabled) {
    impl_->planetRings = enabled;
}
void Renderer::setWeatherEffects(bool enabled) {
    impl_->weatherEffects = enabled;
}
void Renderer::setCloudVolumes(bool enabled) {
    impl_->cloudVolumes = enabled;
}
void Renderer::setTerrainCoverageDebug(bool enabled) {
    impl_->terrainCoverageDebug = enabled;
}
std::shared_ptr<const Geometry> Renderer::submittedGeometry(const Address& address) const {
    auto found = impl_->details.find(address);
    return found == impl_->details.end() ? nullptr : found->second.blend.current;
}
void Renderer::resize(int w, int h) {
    if (w <= 0 || h <= 0)
        return;
    auto& p = *impl_;
    p.width = w;
    p.height = h;
    p.swapChain.width = w;
    p.swapChain.height = h;
    bgfx::reset(p.resetFlags, &p.swapChain);
}
FrameStats Renderer::render(const Navigation& nav, RegionCache& cache, bool overlay, double fps,
                            size_t markers, std::string_view notice, bool looking) {
    auto start = std::chrono::steady_clock::now();
    auto& p = *impl_;
    ++p.frame;
    p.visibleRings.clear();
    if (p.skyGeometry && !p.skyGeometry->address.ancestorOf(nav.anchor))
        p.skyGeometry.reset();
    p.frameSeconds = std::clamp(std::chrono::duration<double>(start - p.previousTime).count(), 0.0, .1);
    p.previousTime = start;
    p.visualTime = p.fixedVisualTime >= 0 ? p.fixedVisualTime : p.visualTime + p.frameSeconds;
    p.updateEconomicLighting(p.frameSeconds);
    p.sectionLabel.clear();
    p.stats = {};
    p.exteriorHidden=false;
    p.surfaceLife.beginFrame();
    p.weatherReport.clear();
    p.weatherDetail.clear();
    p.weatherSeverity = 0;
    p.weatherPriority = 1e100;
    p.staticUploadBytes = 0;
    p.picks.clear();
    p.billboards.clear();
    p.trafficLights.clear();
    p.shieldFields.vertices.clear();
    p.shieldFields.indices.clear();
    std::erase_if(p.shieldFades,[&](const auto& entry){return p.frame-entry.second.seen>90;});
    p.cosmicBillboards.clear();
    p.pointLightBillboards.clear();
    p.stellarBillboards.clear();
    p.frameFrames.clear();
    p.framePoses.clear();
    p.cockpitVisible = p.instruments.has_value() && overlay;
    const bool walking = p.shipInterior && p.shipInterior->walking() && p.cockpitVisible && !p.probeRemote;
    const bool seatMoving = p.shipInterior && p.seatTransition.active() && p.cockpitVisible && !p.probeRemote;
    const auto* boarding = p.shipInterior ? p.shipInterior->station() : nullptr;
    const double shipOpening=p.shipInterior?p.shipInterior->shipDoorOpening():0.;
    const double bridgeOpening=p.shipInterior?p.shipInterior->bridgeDoorOpening():0.;
    const auto boardedVessel=boarding&&boarding->key.vessel&&p.xalariaEconomy&&p.xalariaMap&&p.activity&&
        p.xalariaRadio&&p.xalariaRadio->dockingTarget?
        xalariaTrafficContact(*p.xalariaEconomy,*p.xalariaRadio->dockingTarget,p.xalariaMap->wormhole,
                              p.activity->seconds):std::nullopt;
    const bool prize=boardedVessel&&p.xalariaCombat&&p.xalariaCombat->shieldsLowered(boardedVessel->body.id);
    if(boardedVessel){
        const auto parcel=xalariaVesselParcel(*p.xalariaEconomy,*boardedVessel);
        if(!p.boardingCharter||p.boardingCharter->id!=parcel.id){
            p.boardingCharter=parcel;
            p.renderedFreightDispatch.reset();
        }
    }else if (boarding && p.xalariaEconomy && p.xalariaEconomy->dockedPlace()) {
        const auto& port=*p.xalariaEconomy->dockedPlace();
        const auto day=p.xalariaEconomy->day();
        if (!p.boardingCharter || p.boardingCharter->origin!=port || p.boardingCharterDay!=day) {
            p.boardingCharter=p.xalariaEconomy->offer(port);
            p.boardingCharterDay=day;
            p.renderedFreightDispatch.reset();
        }
    } else {
        p.boardingCharter.reset();
        p.renderedFreightDispatch.reset();
    }
    const auto& loadingCharter=p.boardingCharter;
    const bool freightClaimed=loadingCharter && loadingCharter->units>0 &&
                              p.xalariaEconomy->charterClaimed(loadingCharter->id);
    if (!walking) {
        p.deckRoomLabel={};
        p.deckPromptLabel={};
    }
    if (p.cockpitMotionEnabled && !walking && !seatMoving && !p.probeRemote && p.cockpitVisible && !p.cockpitHelp && !(p.market && p.market->open)) {
        const auto& t = *p.instruments;
        p.cockpitMotion.update({t.localAcceleration, t.localAngularVelocity, t.contactImpulse, t.hullMetres,
                                t.hull, t.engine, t.contactSequence, t.landed},
                               p.fixedVisualTime >= 0 ? 1. / 60 : p.frameSeconds);
        p.head = p.cockpitMotion.pose();
    } else {
        p.cockpitMotion.reset();
        p.head = {};
    }
    if (walking) {
        p.head.orientation = p.shipInterior->orientation();
        p.head.translation = p.shipInterior->eye();
    } else if (p.cockpitVisible && !p.cockpitHelp && !p.probeRemote && p.cockpitLook != glm::dvec2(0)) {
        const auto yaw = glm::angleAxis(-p.cockpitLook.x, glm::dvec3(0, 1, 0));
        const auto pitch = glm::angleAxis(-p.cockpitLook.y, glm::dvec3(1, 0, 0));
        p.head.orientation = yaw * pitch * p.head.orientation;
    }
    if (seatMoving)
        p.head = p.seatTransition.pose(p.head);
    p.stats.cockpitMotionDegrees = glm::length(p.head.angles) * 180 / std::numbers::pi;
    p.stats.cockpitMotionMetres = glm::length(p.head.translation);
    p.hudViewport = p.cockpitViewport();
    p.cockpitHitRegions.clear();
    p.cockpitContactHits.clear();
    p.atomView = p.instruments ? 3 : 2;
    p.contacts.clear();
    p.lifeSelection = nav.selected;
    p.skyOccluders.clear();
    auto nodes = p.universe.lineage(nav.anchor);
    p.anchorFrames.clear();
    for (const auto& node : nodes) {
        p.anchorFrames.push_back(node.frame);
        p.frameFrames.emplace(node.address, p.anchorFrames);
    }
    if (p.specimen) {
        std::erase_if(nodes, [&](const Node& node) { return !p.specimen->ancestorOf(node.address); });
        if (nodes.empty()) throw std::runtime_error("Specimen view escaped its source lineage");
    }
    const Node* scopeStar=nullptr;
    if(p.cockpitVisible&&p.xalariaMap&&p.xalariaMap->observingStar&&!p.xalariaMap->open&&!p.probeRemote&&!walking&&!seatMoving){
        const auto node=std::find_if(nodes.begin(),nodes.end(),[](const Node& n){return n.address.kind()==Kind::System;});
        if(node!=nodes.end()&&p.xalariaMap->catalog->star(node->address.id()).surveyStar)
            scopeStar=&*node;
    }
    cache.beginFrame();
    // Queue the actual destination before it enters the ordinary camera LOD.
    // Existing async generation, cache eviction and upload budgets still apply.
    if (p.instruments && p.instruments->warpDestination && !p.specimen && !p.probeRemote) {
        const auto target = p.universe.describe(*p.instruments->warpDestination);
        cache.request(target, 0);
        if (target.address.kind() == Kind::Planet)
            cache.catalog(p.universe.describe(target.address.parent()));
    }
    const auto stationAim = p.instruments ? p.instruments->gunAim : glm::dquat(1, 0, 0, 0);
    p.cameraQ = nav.orientation * stationAim * p.head.orientation;
    p.cameraInverse = glm::inverse(p.cameraQ);
    p.eye = nav.cameraPosition();
    if (walking || seatMoving)
        p.eye += nav.orientation * p.head.translation *
                 (p.instruments->hullMetres / 12. / std::pow(10.,nav.logDistance));
    if (p.instruments && p.instruments->playerGun < maxWeaponMounts)
        p.eye += nav.orientation * glm::dvec3(weaponMountLateralMetres(p.instruments->playerGun), 0, 0) *
                 (p.instruments->hullMetres / 12 / std::pow(10., nav.logDistance));
    p.prospectSites.reset();
    std::optional<Node> prospectPlanet;
    const bool inspectingMaterial = materialInspectionView(nav.anchor.kind(), bool(p.instruments),
                                                         p.instruments && p.instruments->shrinking);
    if (!p.specimen && !p.probeRemote && !inspectingMaterial && (!p.instruments || !p.instruments->shrinking)) {
        for (const auto& node : nodes)
            if (node.address.kind() == Kind::Planet && hasSolidSurface(node))
                prospectPlanet = node;
        if (!prospectPlanet && nav.selected && nav.selected->kind() == Kind::Planet) {
            const auto selected = p.universe.describe(*nav.selected);
            if (hasSolidSurface(selected))
                prospectPlanet = selected;
        }
        if (prospectPlanet) {
            const auto planetEye = p.universe.pointRelative(
                nav.anchor, p.eye * std::pow(10., nav.logDistance - p.anchorFrames.back().logRadius),
                prospectPlanet->address, 0);
            auto region = tileAt(glm::normalize(planetEye), 7);
            auto depleted = [&](Tile t) {
                if (!p.economy)
                    return false;
                const auto key = prospectPlanet->address.child(Kind::Surface, encodeTile(t));
                for (const auto& pool : p.economy->exploration.prospects)
                    if (pool.address == key)
                        return pool.extracted == pool.capacity;
                return false;
            };
            if (depleted(region)) {
                std::optional<Tile> next;
                double score = -2;
                for (unsigned edge = 0; edge < 4; ++edge) {
                    auto candidate = region;
                    for (unsigned step = 0; step < 3; ++step) {
                        candidate = terrainNeighbor(candidate, edge);
                        const auto proximity = glm::dot(tileDirection(candidate), glm::normalize(planetEye));
                        if (!depleted(candidate) && proximity > score) {
                            score = proximity;
                            next = candidate;
                        }
                    }
                }
                if (next)
                    region = *next;
            }
            p.prospectSites = cache.prospects(*prospectPlanet, region);
        }
    }
    p.view = glm::mat4_cast(p.cameraInverse) * glm::translate(glm::dmat4(1), -p.eye);
    const double fov = p.instruments ? 70. * std::numbers::pi / 180. : 1.;
    p.fieldOfView = fov;
    p.projection =
        worldProjection(fov, static_cast<double>(p.width) / p.height, bgfx::getCaps()->homogeneousDepth);
    p.exteriorHidden=p.enclosedCavernView(nav,cache);
    p.stats.hiddenExterior=p.exteriorHidden;
    glm::mat4 v(p.view), proj(p.projection);
    const bool opticalDepth =
        (!p.instruments || p.instruments->altitude >= 0) && !inspectingMaterial &&
        std::any_of(
            nodes.begin(), nodes.end(),
            [](const Node& n) {
                return n.address.kind() == Kind::Planet && planetClimate(n).pressureBar > .015;
            });
    const auto look = p.xalariaMap ? p.xalariaMap->catalog->region(p.xalariaMap->current).look
                                   : XalariaLook::CharredGlass;
    const glm::vec3 targetLook{look == XalariaLook::VerdantFog ? 1.f : 0.f,
                               look == XalariaLook::TidalBlue ? 1.f : 0.f,
                               look == XalariaLook::AcidEtch ? 1.f : 0.f};
    const float fadeStep = float(p.frameSeconds);
    const auto lookDelta = targetLook - p.regionalLook;
    const float lookDistance = glm::length(lookDelta);
    if (p.frame == 1 || lookDistance <= fadeStep)
        p.regionalLook = targetLook;
    else
        p.regionalLook += lookDelta * (fadeStep / lookDistance);
    p.hyperion->setRegionalLook(p.regionalLook, p.visualTime, nav.logDistance,
                               !p.exteriorHidden && !p.specimen && !inspectingMaterial);
    p.hyperion->begin(p.mode, p.width, p.height, bool(p.instruments) || opticalDepth, bool(p.instruments));
    const Node* opticalPlanet = nullptr;
    double opticalDistance = 1e100;
    if (!inspectingMaterial && (!p.instruments || !p.instruments->shrinking)) {
        for (const auto& planet : nodes) {
            if (planet.address.kind() != Kind::Planet || !hasSolidSurface(planet))
                continue;
            const auto climate = planetClimate(planet);
            const auto pose = p.poseFor(planet.address, nav);
            const double radius = std::pow(10., planet.frame.logRadius),
                         units = std::pow(10., nav.logDistance);
            const auto localEye = glm::inverse(pose.orientation) * (p.eye - pose.position) * units / radius;
            const double distance = (glm::length(localEye) - 1) * radius;
            if (glm::length(localEye) > .99 && glm::length(localEye) < 8 && distance < opticalDistance &&
                climate.pressureBar * 288 / std::max(60., climate.temperatureK) > .01) {
                opticalPlanet = &planet;
                opticalDistance = distance;
            }
        }
        if (opticalPlanet) {
            const auto pose = p.poseFor(opticalPlanet->address, nav);
            const double radius = std::pow(10., opticalPlanet->frame.logRadius),
                         units = std::pow(10., nav.logDistance);
            const auto localEye = glm::inverse(pose.orientation) * (p.eye - pose.position) * units / radius;
            glm::dmat4 mapping(glm::mat3_cast(glm::inverse(pose.orientation)) * (units / radius));
            mapping[3] = glm::dvec4(localEye, 1);
            p.hyperion->prepareWeather(*opticalPlanet, mapping,
                                       p.activity ? p.activity->seconds : p.visualTime, p.frameSeconds,
                                       p.cloudVolumes, p.fixedVisualTime >= 0);
            const double seconds = p.activity ? p.activity->seconds : p.visualTime;
            const auto climate = planetClimate(*opticalPlanet);
            p.hyperion->localWeather(
                climate, sampleWeather(climate, localEye, (glm::length(localEye) - 1) * radius, seconds),
                seconds, p.weatherEffects);
        }
    }
    p.staticUploadBytes += p.hyperion->materialUploadBytes();
    p.stats.uploadBytes += p.hyperion->materialUploadBytes();
    bgfx::setViewMode(0, bgfx::ViewMode::Sequential);
    bgfx::setViewTransform(0, glm::value_ptr(v), glm::value_ptr(proj));
    bgfx::setViewRect(0, 0, 0, static_cast<uint16_t>(p.width), static_cast<uint16_t>(p.height));
    p.stellarExposure = 1;
    {
        auto sky = extractSpectralSky(p.universe, nav);
        if (p.specimen || p.exteriorHidden) sky.strength = 0;
        sky.cameraToField = sky.cameraToField * stationAim * p.head.orientation;
        for (const auto& planet : nodes)
            if (planet.address.kind() == Kind::Planet && hasSolidSurface(planet)) {
                const auto climate = planetClimate(planet);
                const auto pose = p.poseFor(planet.address, nav);
                const double radius = std::pow(10., planet.frame.logRadius - nav.logDistance);
                const auto localEye = glm::inverse(pose.orientation) * (p.eye - pose.position) / radius;
                const auto light = glm::inverse(pose.orientation) * p.lightAt(planet.address, nav);
                if (glm::length(localEye) > .99) {
                    const float transmission = daylightSkyTransmission(
                        climate.pressureBar,
                        (glm::length(localEye) - 1) * std::pow(10., planet.frame.logRadius),
                        climate.scaleHeightMetres, glm::dot(glm::normalize(localEye), light));
                    sky.strength *= transmission;
                    p.stellarExposure *= transmission;
                }
            }
        p.hyperion->sky(sky, p.visualTime, float(p.width) / p.height, float(std::tan(fov * .5)));
    }
    if (p.instruments) {
        bgfx::setViewTransform(2, glm::value_ptr(v), glm::value_ptr(proj));
        bgfx::setViewRect(2, 0, 0, uint16_t(p.width), uint16_t(p.height));
        bgfx::setViewMode(2, bgfx::ViewMode::Default);
    }
    std::optional<Node> lifeSurvey;
    double surveyPriority = 1e300;
    for (const auto& system : nodes)
        if (system.address.kind() == Kind::System)
            if (auto planets = cache.catalog(system)) {
                p.contacts = extractCelestialContacts(p.universe, nav, system, *planets,
                                                      stationAim * p.head.orientation);
                for (const auto& planet : *planets) {
                    auto contact = std::find_if(p.contacts.begin(), p.contacts.end(),
                                                [&](const auto& c) { return c.address == planet.address; });
                    if (contact == p.contacts.end())
                        continue;
                    if (!hasSolidSurface(planet)) {
                        contact->life = LifeDetection::None;
                    } else if (auto summary = p.surfaceLife.summary(planet.address)) {
                        contact->population = summary->population;
                        contact->surfacePorts = summary->ports;
                        contact->openPorts = summary->openPorts;
                        contact->life = summary->population ? LifeDetection::Detected : LifeDetection::None;
                    } else if (p.instruments && !p.instruments->shrinking) {
                        contact->life = LifeDetection::Scanning;
                        const double priority = contact->selected ? 0 : contact->distanceMetres;
                        if (priority < surveyPriority) {
                            lifeSurvey = planet;
                            surveyPriority = priority;
                        }
                    }
                }
            }
    std::sort(p.contacts.begin(), p.contacts.end(),
              [](const auto& a, const auto& b) { return a.distanceMetres < b.distanceMetres; });
    const double pixelAngle = 2 * std::tan(fov * .5) / p.height;
    for (const auto& contact : p.contacts)
        if (contact.angularRadius > pixelAngle * .125)
            p.skyOccluders.push_back(contact);
    StellarImage localStar;
    if (!p.exteriorHidden && p.instruments && !p.instruments->shrinking && (p.stellarOptics || boarding)) {
        localStar=extractStellarImage(p.universe,nav,p.contacts,stationAim*p.head.orientation);
        if (p.stellarOptics)
            p.hyperion->stellar(localStar,float(p.width)/p.height,float(std::tan(fov*.5)),p.visualTime);
    }
    p.stats.celestialContacts = p.contacts.size();
    p.stats.lifeWorlds = std::count_if(p.contacts.begin(), p.contacts.end(), hasSurfaceLife);
    p.stats.lifeScanning = std::count_if(p.contacts.begin(), p.contacts.end(),
                                         [](const auto& c) { return c.life == LifeDetection::Scanning; });
    bgfx::touch(0);
    std::optional<Node> terrainPlanet;
    for (const auto& node : nodes)
        if (node.address.kind() == Kind::Planet && hasSolidSurface(node))
            terrainPlanet = node;
    if (terrainPlanet) {
        const auto planetEye = p.universe.pointRelative(
            nav.anchor, p.eye * std::pow(10., nav.logDistance - nodes.back().frame.logRadius),
            terrainPlanet->address, terrainPlanet->frame.logRadius);
        p.terrain.update(*terrainPlanet, planetEye, p.height, p.frameSeconds, cache);
    } else
        p.terrain.clear();
    p.terrainActive = terrainPlanet && p.terrain.ready();
    p.terrainSunPlanet.reset();
    p.terrainSunComplete = true;
    std::set<Address> rendered;
    // Request the current focus first. Ancestors remain in the same spatial view
    // while the camera crosses the (possibly very large) physical scale gap.
    p.renderNode(nodes.back(), nav, cache);
    rendered.insert(nav.anchor);
    for (auto it = nodes.rbegin() + 1; it != nodes.rend(); ++it) {
        p.renderNode(*it, nav, cache);
        rendered.insert(it->address);
    }
    if (p.instruments && p.instruments->warpDestination && !p.specimen && !p.exteriorHidden) {
        const auto& destination = *p.instruments->warpDestination;
        if (!rendered.contains(destination)) {
            p.renderNode(p.universe.describe(destination), nav, cache);
            rendered.insert(destination);
        }
    }
    for (const auto& node : nodes)
        if (node.address.kind() == Kind::Galaxy)
            p.stellarSky(node, nav, cache);
    for (const auto& contact : p.contacts) {
        if(p.exteriorHidden)continue;
        // The disc can leave the viewport or pass behind another body while
        // part of its ring is still in view.
        const auto direction = contact.localDirection;
        if (contact.address.kind() == Kind::Planet && !rendered.contains(contact.address)) {
            bool visibleRing = false;
            if (p.planetRings && p.universe.xalaria()) {
                const auto& source = p.universe.xalaria()->world(contact.address);
                const double units = std::pow(10., nav.logDistance);
                const double ringRadius = source.radius * source.ringOuter / units;
                const double distance = contact.distanceMetres / units;
                visibleRing = source.ringOuter > source.ringInner &&
                    LOD::pixels(ringRadius, distance, p.height, fov) >= .7 &&
                    LOD::visible(direction * distance, ringRadius, double(p.width) / p.height, fov);
            }
            if (visibleRing || (contact.angularRadius >= .008 && direction.z < 0 && !contact.occluded)) {
                p.renderNode(p.universe.describe(contact.address), nav, cache);
                rendered.insert(contact.address);
            }
        }
        if (direction.z >= 0 || contact.occluded)
            continue;
        const auto point = p.eye + p.cameraQ * direction * 500.;
        if (auto screen = p.project(point)) {
            p.picks.push_back({contact.address, *screen, contact.distanceMetres});
            // Large resolved planets keep their generated mesh. Unresolved
            // planets and the actual sun retain a legible angular light proxy
            // even when their physical distance exceeds the local depth range.
            const auto detail = p.details.find(contact.address);
            const bool resolved = detail != p.details.end() && detail->second.used == p.frame &&
                                  detail->second.blend.current;
            if (!resolved || contact.address.kind() == Kind::System) {
                const double pixelAngle = 2 * std::tan(fov * .5) / p.height;
                const bool star = contact.address.kind() == Kind::System;
                const double angular = std::clamp(contact.angularRadius * (star ? 2.5 : 1.),
                                                  pixelAngle * (star ? 2.5 : 1.2), .12);
                p.addBillboard(point, angular * 500., contact.color * (star ? 1.f : .65f), true);
                ++p.stats.celestialLights;
            }
        }
    }
    std::vector<Node> rootNodes;
    const Address anchorRoot{nav.anchor.cell, {}};
    const auto anchorRootEye = p.universe.pointRelative(
        nav.anchor, p.eye * std::pow(10., nav.logDistance - nodes.back().frame.logRadius), anchorRoot,
        rootCellLogMetres);
    const auto [root, rootEye] = canonicalRoot(anchorRoot, anchorRootEye);
    for (const auto& address : p.specimen ? std::vector<Address>{} : rootNeighborhood(root)) {
        auto node = p.universe.describe(address);
        p.galaxySky(node, nav, cache,
                    celestialCellWeight(cellDisplacement(address.cell, root.cell), rootEye));
        if (!rendered.contains(address)) {
            p.renderNode(node, nav, cache, 2);
            rendered.insert(address);
        }
        if (!p.exteriorHidden)
            if (auto boundary = cache.requestCosmicBoundary(node))
                p.drawGeometry(boundary, node, nav, 1);
        if (auto it = p.details.find(address); it != p.details.end() && it->second.blend.current)
            ++p.stats.rootCells;
        const auto centre = cellDisplacement(address.cell, root.cell);
        if (glm::length(centre - rootEye) < 1.8)
            rootNodes.push_back(std::move(node));
    }
    if (terrainPlanet)
        p.renderTerrain(*terrainPlanet, nav, cache);
    const auto probeStarted=std::chrono::steady_clock::now();
    p.renderBores(nav,cache);
    p.stats.probeCpuMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-probeStarted).count();
    p.prospectMarkers.vertices.clear();
    p.prospectMarkers.indices.clear();
    if(p.economy&&!p.probeRemote&&!p.specimen)
        for(const auto& b:p.economy->exploration.probes.holes)
            if(p.probeSeconds>=b.blastTime()&&(!p.economy->network||b.planet.parent()==p.economy->network->system)) {
                const auto planet=p.universe.describe(b.planet);
                const auto point=p.universe.pointRelative(b.planet,b.entry/std::pow(10.,planet.frame.logRadius),nav.anchor,nav.logDistance);
                if(glm::length(point-p.eye)*std::pow(10.,nav.logDistance)>2500)continue;
                if(auto screen=p.project(point);screen&&screen->x>p.width*.2&&screen->x<p.width*.8&&screen->y>p.height*.18&&screen->y<p.height*.7)
                    for(unsigned k=0;k<24;++k){const double a=k*6.283185307179586/24,c=(k+1)*6.283185307179586/24;
                        addLine(p.prospectMarkers,{screen->x+std::cos(a)*12,screen->y+std::sin(a)*12,0},
                                                  {screen->x+std::cos(c)*12,screen->y+std::sin(c)*12,0},{1.f,.66f,.28f});}
            }
    if (prospectPlanet) {
        const auto planet = *prospectPlanet;
        const auto radius = std::pow(10., planet.frame.logRadius);
        const auto planetEye = p.universe.pointRelative(
            nav.anchor, p.eye * std::pow(10., nav.logDistance - p.anchorFrames.back().logRadius),
            planet.address, 0);
        if (p.prospectSites) {
            p.stats.prospectSites = p.prospectSites->size();
            std::array<const ProspectSite*, maxRegionalProspects> ranked{};
            size_t count = 0;
            for (const auto& site : *p.prospectSites)
                ranked[count++] = &site;
            std::sort(ranked.begin(), ranked.begin() + count, [&](auto a, auto b) {
                return glm::length(a->metres - planetEye) < glm::length(b->metres - planetEye);
            });
            unsigned shown = 0, rocks = 0;
            for (size_t index = 0; index < count; ++index) {
                const auto& site = *ranked[index];
                const auto distance = glm::length(site.metres - planetEye);
                const bool selected =
                    nav.selected && (*nav.selected == site.surface || site.surface.ancestorOf(*nav.selected));
                const auto point = p.universe.pointRelative(planet.address, site.outcropMetres / radius,
                                                            nav.anchor, nav.logDistance);
                if (glm::dot(site.direction, planetEye - site.metres) > 0 && (selected || shown < 4))
                    if (auto screen = p.project(point);
                        screen && screen->x > p.width * .20 && screen->x < p.width * .80 &&
                        screen->y > p.height * .18 && screen->y < p.height * .70) {
                        const double x = screen->x, y = screen->y, s = selected ? 13 : 7;
                        const auto color = selected    ? glm::vec3(.55, .97, .78)
                                           : site.easy ? glm::vec3(.35, .78, .92)
                                                       : glm::vec3(.96, .62, .27);
                        for (unsigned i = 0; i < 4; ++i) {
                            const double a = i * 1.5707963267948966, b = (i + 1) * 1.5707963267948966;
                            addLine(p.prospectMarkers, {x + std::cos(a) * s, y + std::sin(a) * s, 0},
                                    {x + std::cos(b) * s, y + std::sin(b) * s, 0}, color);
                        }
                        p.picks.push_back({site.surface, *screen, distance});
                        ++shown;
                        ++p.stats.prospectMarkers;
                    }
                if (distance < 2200 && rocks < 12) {
                    for (const auto& node : site.rocks) {
                        auto frames = site.parentFrames;
                        frames.push_back(node.frame);
                        p.frameFrames.try_emplace(node.address, std::move(frames));
                        p.renderNode(node, nav, cache, 3);
                        if (auto screen = p.project(p.poseFor(node.address, nav).position))
                            p.picks.push_back({node.address, *screen, distance});
                        ++rocks;
                        ++p.stats.prospectRocks;
                    }
                }
            }
        }
    }
    if (p.economy && p.economy->network && (!p.market || !p.market->open))
        if (const auto* seam = activeSeam(p.economy->exploration, p.economy->network->system)) {
            auto beacon = [&](glm::dvec3 point, const Address& pick, glm::vec3 color, bool entry) {
                if (auto screen = p.project(point);
                    screen && screen->x > p.width * .20 && screen->x < p.width * .80 &&
                    screen->y > p.height * .18 && screen->y < p.height * .70) {
                    const double x = screen->x, y = screen->y, s = entry ? 10 : 16;
                    for (unsigned k = 0; k < 4; ++k) {
                        const double a = k * 1.5707963267948966 + (entry ? .7853981633974483 : 0);
                        const double b = a + 1.5707963267948966;
                        addLine(p.prospectMarkers, {x + std::cos(a) * s, y + std::sin(a) * s, 0},
                                {x + std::cos(b) * s, y + std::sin(b) * s, 0}, color);
                    }
                    if (!entry) {
                        addLine(p.prospectMarkers, {x - 5, y, 0}, {x + 5, y, 0}, color);
                        addLine(p.prospectMarkers, {x, y - 5, 0}, {x, y + 5, 0}, color);
                        if (pick != nav.anchor)
                            p.picks.push_back({pick, *screen, glm::length(point - p.eye)});
                    }
                }
            };
            const auto planet = p.universe.describe(seam->pool.parent());
            const auto entry = p.universe.pointRelative(planet.address,
                seam->entryMetres / std::pow(10., planet.frame.logRadius), nav.anchor, nav.logDistance);
            beacon(entry, seam->source, {.35f, .85f, 1.f}, true);
            if (seam->marked && seam->material) {
                const auto pose = p.poseFor(*seam->material, nav);
                beacon(pose.position, seam->source, {1.f, .75f, .25f}, false);
            }
        }
    Mesh locators;
    auto candidates = [&](const Node& parent) {
        if(p.exteriorHidden&&parent.address.kind()<Kind::Surface)return;
        auto parentPose = p.poseFor(parent.address, nav);
        auto pc = p.cameraSpace(parentPose.position);
        double parentPixels = LOD::pixels(parentPose.scale, glm::length(pc), p.height, 1);
        const bool catalogVisible = parentPixels >= 25 && parentPose.scale <= 40;
        const bool selectedChild = nav.selected && nav.selected->parent() == parent.address;
        if (!catalogVisible && !selectedChild)
            return;
        auto catalog = catalogVisible ? cache.catalog(parent) : nullptr;
        const auto parentFrames = p.framesFor(parent.address);
        auto present = [&](const Node& n) {
            const auto& a = n.address;
            if (p.instruments) {
                auto contact = std::find_if(p.contacts.begin(), p.contacts.end(),
                                            [&](const CelestialContact& c) { return c.address == a; });
                if (contact != p.contacts.end() && contact->occluded)
                    return;
            }
            auto childFrames = parentFrames;
            childFrames.push_back(n.frame);
            p.frameFrames.try_emplace(a, std::move(childFrames));
            auto pose = p.poseFor(a, nav);
            auto cp = p.cameraSpace(pose.position);
            auto screen = p.project(pose.position);
            if (!screen || cp.z >= 0)
                return;
            bool isSelected = nav.selected && a == *nav.selected;
            if (a == nav.anchor)
                return;
            p.picks.push_back({a, *screen, -cp.z});
            if (isSelected || n.address.kind() == Kind::Planet || n.address.kind() >= Kind::Surface) {
                double rr = -cp.z * .0025;
                p.addBillboard(pose.position, rr,
                               n.address.kind() == Kind::System ? stellarRadiance(n)
                                                                : material(n.environment.mineral).color);
            }
            if (isSelected) {
                double x = screen->x, y = screen->y;
                glm::vec3 col(.31, .66, .74);
                for (int corner = 0; corner < 4; ++corner) {
                    double sx = (corner & 1) ? 1 : -1, sy = (corner & 2) ? 1 : -1;
                    addLine(locators, {x + sx * 10, y + sy * 10, 0}, {x + sx * 17, y + sy * 10, 0}, col);
                    addLine(locators, {x + sx * 10, y + sy * 10, 0}, {x + sx * 10, y + sy * 17, 0}, col);
                }
            }
            // Regional survey markers sit on the terrain already in view.
            // Request a separate inspection mesh only when one is selected.
            if (!isSelected && a.kind() == Kind::Surface && decodeTile(a.id()).level == 7)
                return;
            if (!rendered.contains(a)) {
                p.renderNode(n, nav, cache);
                rendered.insert(a);
            }
        };
        if (catalog)
            for (const auto& n : *catalog)
                present(n);
        // Automatic flight may discover any canonical surface tile or any
        // displayed star ID, beyond the deliberately small locator catalog.
        if (nav.selected && nav.selected->parent() == parent.address &&
            (!catalog || std::none_of(catalog->begin(), catalog->end(),
                                      [&](const auto& n) { return n.address == *nav.selected; })))
            present(p.universe.describe(*nav.selected));
    };
    if (nodes.size() > 1)
        p.renderSection(nodes[nodes.size() - 2], nav, cache);
    candidates(nodes.back());
    if (nodes.size() > 1)
        candidates(nodes[nodes.size() - 2]);
    for (const auto& node : rootNodes)
        if (node.address != root)
            candidates(node);
    p.drawPlanetRings(nav);
    p.renderActivity(nodes, nav, cache);
    p.renderShipTrails(nav);
    if(p.playerShield.pulse>0.&&p.cockpitVisible&&!p.exteriorHidden&&!p.probeRemote){
        const double scale=p.instruments->hullMetres/12./std::pow(10.,nav.logDistance);
        const auto centre=(nav.cameraPosition()-p.eye)+nav.orientation*glm::dvec3(0,0,1.6*scale);
        p.shieldEnvelope(centre,nav.orientation,glm::dvec3(3.45,2.70,6.2)*scale,p.playerShield,0.f,
                         .83f*float(p.playerShield.pulse));
    }
    p.transient(p.shieldFields,p.trafficLightView,false,true);
    p.drawWormhole(nav);
    p.renderLanding(nav, terrainPlanet);
    // Nearby visible worlds claim the two existing workers first. Surveying
    // the remaining system never evicts a close city's geometry to keep a
    // distant world's full layout resident.
    if (lifeSurvey && p.frame % 8 == 0)
        p.surfaceLife.survey(*lifeSurvey);
    p.surfaceLife.finishFrame();
    const auto surfaceStats = p.surfaceLife.stats();
    if (!surfaceStats.error.empty())
        throw std::runtime_error("Surface generation: " + surfaceStats.error);
    p.stats.surfaceBytes = surfaceStats.bytes;
    p.stats.surfacePending = surfaceStats.pending;
    p.stats.surfaceGpuBytes = p.surfaceMeshBytes;
    auto drawBillboards = [&](const std::vector<Instance>& instances, bool cosmic, bool stellar = false,
                              bool compact = false, std::optional<bgfx::ViewId> localView = std::nullopt) {
        uint32_t count = bgfx::getAvailInstanceDataBuffer(static_cast<uint32_t>(instances.size()), 80);
        if (count) {
            bgfx::InstanceDataBuffer buffer;
            bgfx::allocInstanceDataBuffer(&buffer, count, 80);
            std::memcpy(buffer.data, instances.data(), count * sizeof(Instance));
            const auto& mesh = compact ? p.pointLightBillboardMesh : p.hyperionBillboardMesh;
            bgfx::setVertexBuffer(0, mesh.vertices);
            bgfx::setIndexBuffer(mesh.indices);
            bgfx::setInstanceDataBuffer(&buffer);
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_MSAA | BGFX_STATE_DEPTH_TEST_GEQUAL |
                           BGFX_STATE_BLEND_ADD);
            {
                // Use the same focus-local reference at every magnification.
                if (cosmic) {
                    const auto q = p.universe.relative(nav.anchor, root, nav.logDistance).orientation;
                    glm::dmat4 pattern = glm::mat4_cast(q);
                    for (unsigned i = 0; i < 3; ++i)
                        pattern[i] /= 500.;
                    pattern[3] = glm::dvec4(-(q * p.eye) / 500., 1);
                    p.hyperion->bindEmissions(p.universe.describe(Address{}), glm::mat4(pattern),
                                              p.visualTime);
                } else {
                    const auto& node = nodes.back();
                    auto focusPose = p.poseFor(nav.anchor, nav);
                    auto focusModel = glm::translate(glm::mat4(1), glm::vec3(focusPose.position)) *
                                      glm::mat4_cast(glm::quat(focusPose.orientation)) *
                                      glm::scale(glm::mat4(1), glm::vec3(float(focusPose.scale)));
                    p.hyperion->bindEmissions(node, glm::inverse(focusModel), p.visualTime);
                }
            }
            bgfx::submit(localView.value_or(cosmic && p.instruments ? 0 : p.worldView()),
                         stellar ? p.hyperion->coronaProgram() : p.hyperion->glowProgram());
        }
    };
    drawBillboards(p.pointLightBillboards, true, false, true);
    drawBillboards(p.cosmicBillboards, true);
    drawBillboards(p.billboards, false);
    drawBillboards(p.stellarBillboards, false, true);
    drawBillboards(p.trafficLights,false,false,false,p.trafficLightView);
    const bool canShowWarp=p.instruments && !p.probeRemote && !p.specimen && !p.exteriorHidden && !scopeStar;
    const bool engaged=canShowWarp && (p.instruments->warp==WarpPhase::Route || p.instruments->warp==WarpPhase::Manual);
    if (engaged)
        p.warpOpticalDestination = p.instruments->warpDestination;
    const auto warp=p.warpOptics.update(engaged,p.warpOpticalDestination.has_value(),
        engaged?p.instruments->speed:0.,engaged?warpTunnelAperture(p.instruments->warpReveal,
            p.instruments->warpAngularRadius,fov):.21,p.frameSeconds);
    if (canShowWarp && (warp.light>.001 || warp.closure>.001)) {
        // Camera-relative coordinates keep the streak field stable across reference frames.
        Mesh streaks;
        p.warpClock = std::fmod(p.warpClock + p.frameSeconds * (.42 + warp.light * 2.55), 1.);
        const auto velocity = p.cameraInverse * (nav.orientation * p.instruments->localVelocity);
        const double aspect = double(p.width) / p.height;
        const double tanFov = std::tan(fov * .5);
        const double forward = -velocity.z;
        auto desiredFocus = forward > std::max(1e-12, .25 * glm::length(velocity))
            ? glm::clamp(glm::dvec2(velocity.x / (forward * tanFov * aspect),
                                    velocity.y / (forward * tanFov)), glm::dvec2(-.8), glm::dvec2(.8))
            : glm::dvec2(0);
        const auto& travel = *p.instruments;
        const auto destination = p.warpOpticalDestination ? p.cameraInverse *
            (p.universe.relative(*p.warpOpticalDestination, nav.anchor, nav.logDistance).position - p.eye)
            : glm::dvec3(0);
        if (p.warpOpticalDestination && destination.z < -1e-12) {
            const auto centre = glm::clamp(glm::dvec2(destination.x / (-destination.z * tanFov * aspect),
                                                     destination.y / (-destination.z * tanFov)),
                                           glm::dvec2(-.8), glm::dvec2(.8));
            desiredFocus = glm::mix(desiredFocus, centre, engaged ? travel.warpReveal : 1.);
        }
        p.warpFocus = glm::mix(p.warpFocus,desiredFocus,-std::expm1(-p.frameSeconds*6.));
        const auto focus = p.warpFocus;
        const auto world = [&](glm::dvec2 screen) {
            return p.cameraQ * glm::dvec3(screen.x * aspect * tanFov, screen.y * tanFov, -1) * 500.;
        };
        constexpr double turn = 2. * std::numbers::pi;
        const double twist = p.visualTime * .48;
        const auto polar = [&](double radius, double angle) {
            return glm::dvec2(radius * std::cos(angle) / aspect, radius * std::sin(angle));
        };
        const auto embers = [](double phase) {
            const float heat=float(.5+.5*std::sin(phase));
            const auto ember=glm::mix(glm::vec3(.32f,.018f,.006f),glm::vec3(1.f,.48f,.018f),heat*heat);
            return glm::mix(ember,glm::vec3(1.f,.88f,.28f),std::pow(heat,12.f)*.70f);
        };
        // Soft ribbons behind the brighter speed streaks, lit before bloom.
        Mesh ribbons;
        constexpr unsigned lanes = 5;
        const auto strip = [&](unsigned samples, auto section) {
            constexpr std::array offsets{-1., -.27, 0., .27, 1.};
            constexpr std::array strengths{0.f, .38f, 1.f, .38f, 0.f};
            const auto first = uint32_t(ribbons.vertices.size());
            for (unsigned j = 0; j <= samples; ++j) {
                const auto [centre, edge, colour] = section(double(j) / samples);
                for (unsigned lane = 0; lane < lanes; ++lane) {
                    const auto point = world(centre + edge * offsets[lane]);
                    ribbons.vertices.push_back(
                        {float(point.x), float(point.y), float(point.z), rgba(colour * strengths[lane])});
                    if (j && lane) {
                        const auto end = first + j * lanes + lane;
                        ribbons.indices.insert(ribbons.indices.end(),
                            {end - lanes - 1, end - lanes, end - 1, end - lanes, end, end - 1});
                    }
                }
            }
        };

        // The opening stays on the destination as the field falls away.
        constexpr auto warpPass=warpFieldViewId;
        const auto rotationView=glm::mat4_cast(glm::quat(p.cameraInverse));
        bgfx::setViewMode(warpPass, bgfx::ViewMode::Sequential);
        bgfx::setViewRect(warpPass, 0, 0, uint16_t(p.width), uint16_t(p.height));
        bgfx::setViewTransform(warpPass,glm::value_ptr(rotationView),glm::value_ptr(proj));
        const double outerRadius = std::hypot(aspect * (1. + std::abs(focus.x)),
                                              1. + std::abs(focus.y)) + .28;
        const double aperture=std::lerp(outerRadius,warp.bore,warp.closure);
        if (warp.light>.001 && aperture<outerRadius-.001) {
            Mesh wall;
            constexpr unsigned sectors = 80, slices = 24, row = sectors + 1;
            wall.vertices.reserve((slices + 1) * row);
            wall.indices.reserve(slices * sectors * 6);
            const double ratio = outerRadius / aperture;
            const auto pigment = glm::mix(embers(twist), glm::vec3(.46f, .075f, .012f), .5f);
            for (unsigned slice = 0; slice <= slices; ++slice) {
                const double t = double(slice) / slices;
                const double radius = aperture * std::pow(ratio, t);
                const float opacity = float(warp.light * .82 * smoothstep(0., .24, t));
                const auto colour = glm::vec3(.006f, .002f, .001f) + pigment * float(.08 + .12 * t);
                for (unsigned j = 0; j <= sectors; ++j) {
                    const double angle = j * turn / sectors;
                    const auto position = world(focus + polar(radius, angle));
                    wall.vertices.push_back({float(position.x), float(position.y), float(position.z),
                                             rgba(colour, opacity)});
                    if (slice && j) {
                        const auto end = uint32_t(wall.vertices.size() - 1);
                        wall.indices.insert(wall.indices.end(),
                                           {end - row - 1, end - row, end - 1, end - row, end, end - 1});
                    }
                }
            }
            p.transient(wall,warpPass,false,false,false,false,true);
        }

        for (unsigned i = 0; i < 220; ++i) {
            const Random random{mix64(0x574152505354524bULL + i)};
            const double angle = random.unit(0) * turn;
            const double age = std::fmod(p.warpClock + random.unit(1), 1.);
            const double radius = (.14 + age * age * 3.15) * (.67 + .55 * random.unit(2));
            const double span=(.035+(i%3==0?1.28:.69)*age*age)*warp.light;
            const double inner = aperture * 1.08;
            if (radius <= inner)
                continue;
            const auto head = focus + polar(radius, angle);
            const auto tail = focus + polar(std::max(inner, radius - span), angle);
            const float fade=float(warp.light*smoothstep(.04,.22,age)*(1.-smoothstep(.78,1.,age))*
                                   smoothstep(inner,inner+.16,radius));
            const auto colour = glm::mix(embers(angle + twist),glm::vec3(1,.82f,.36f),
                                         float(.08+random.unit(3)*.42));
            addLine(streaks, world(tail), world(glm::mix(tail, head, .72)), colour * (fade * .38f));
            addLine(streaks, world(glm::mix(tail, head, .72)), world(head), colour * fade);
            if (i % 4 == 0) {
                strip(2, [&](double t) {
                    return std::tuple{glm::mix(tail, head, t), polar(.008 + .012 * age, angle + turn * .25),
                                      colour * (fade * float(t) * .60f)};
                });
            }
        }

        p.transient(ribbons,warpPass,false,true,false);
        p.transient(streaks,warpPass,true,true,false);
    } else {
        p.warpFocus = {};
        if(!engaged && warp.light<=.001 && warp.closure<=.001)
            p.warpOpticalDestination.reset();
    }
    const double normalTrails=1.-smoothstep(.02,.24,warp.light);
    if (p.instruments && normalTrails>.001 && !p.instruments->shrinking &&
        !p.instruments->landed && p.instruments->speed > 24) {
        // Hyperion drawMotion's bounded, world-grid-anchored navigation streaks.
        // These are optical cues, never additional stars or collision objects.
        Mesh trails;
        const double unitMetres = std::pow(10., nav.logDistance);
        const double driveScale = std::max(1., p.instruments->driveSpeed / 450.);
        const double spacing = 180. * driveScale / unitMetres;
        const auto grid = glm::floor(p.eye / spacing);
        const auto velocity = nav.orientation * p.instruments->localVelocity;
        const auto direction = glm::normalize(velocity);
        const auto forward = nav.orientation * glm::dvec3(0, 0, -1);
        const double strength=normalTrails*smoothstep(24.,95.,p.instruments->speed)*
                              std::clamp(p.instruments->speed/(450.*driveScale),0.,1.);
        for (int x = -2; x <= 2; x++)
            for (int y = -2; y <= 2; y++)
                for (int z = -2; z <= 2; z++) {
                    auto cell = grid + glm::dvec3(x, y, z);
                    double h =
                        std::sin(glm::dot(glm::mod(cell, glm::dvec3(4096)), glm::dvec3(127.1, 311.7, 74.7))) *
                        43758.5453;
                    h -= std::floor(h);
                    const auto point =
                        (cell + glm::dvec3(h, std::fmod(h * 17, 1.), std::fmod(h * 31, 1.))) * spacing;
                    const double distance = glm::length(point - p.eye);
                    if (distance < spacing / 6 || distance > spacing * 2 ||
                        glm::dot(point - p.eye, forward) < spacing / 9)
                        continue;
                    const auto length = (driveScale + p.instruments->speed * .028) / unitMetres;
                    addLine(trails, point, point - direction * length,
                            glm::vec3(.28, .78, .91) *
                                float((1 - distance / (spacing * 2)) * strength * .36));
                }
        p.transient(trails, p.worldView(), true, true);
    }
    if(!p.xalariaCombat)p.explosionImpulses.clear();
    else std::erase_if(p.explosionImpulses,[&](const auto& id){
        const auto events=p.xalariaCombat->explosions();
        return std::none_of(events.begin(),events.end(),[&](const auto& event){return event.source==id;});
    });
    if ((p.weapons && (!p.weapons->shots().empty() || !p.weapons->missiles().empty() || !p.weapons->decoys().empty())) ||
        (p.xalariaCombat&&!p.xalariaCombat->explosions().empty()) ||
        (p.npcCombat && !p.npcCombat->shots().empty())) {
        Mesh beams, weaponGlow, miningChips, miningDust, explosionGlow, explosionGas;
        Mesh* glowTarget=&weaponGlow;
        struct ExplosionVapour {glm::dvec3 point;double radius;glm::vec3 colour;float opacity;};
        std::vector<ExplosionVapour> explosionVapour;
        const auto rotation = glm::mat4_cast(glm::quat(p.cameraInverse));
        const glm::mat4 projection(p.projection);
        bgfx::setViewTransform(63, glm::value_ptr(rotation), glm::value_ptr(projection));
        bgfx::setViewRect(63, 0, 0, uint16_t(p.width), uint16_t(p.height));
        const double units = std::pow(10., nav.logDistance);
        const auto cameraRight = p.cameraQ * glm::dvec3(1, 0, 0);
        const auto cameraUp = p.cameraQ * glm::dvec3(0, 1, 0);
        const auto screenWidth = [&](glm::dvec3 point) {
            return 2 * std::tan(fov * .5) * std::max(worldNearPlane, -p.cameraSpace(point + p.eye).z) /
                   p.height;
        };
        const auto vertex = [&](glm::dvec3 point, glm::vec3 colour) {
            const float coverage=glowTarget==&explosionGlow?std::clamp(std::max({colour.r,colour.g,colour.b})*1.25f,0.f,.98f):1.f;
            const auto radiance=glowTarget==&explosionGlow?colour*2.8f:colour;
            glowTarget->vertices.push_back({float(point.x),float(point.y),float(point.z),rgba(radiance,coverage)});
        };
        const auto ribbon = [&](glm::dvec3 from, glm::dvec3 to, double startWidth, double endWidth,
                                glm::vec3 startColour, glm::vec3 endColour) {
            auto side = glm::cross(to - from, from + to);
            const double length = glm::length(side);
            side = length > 1e-20 ? side / length : cameraRight;
            const auto first = uint32_t(glowTarget->vertices.size());
            for (int edge = -1; edge <= 1; ++edge) {
                vertex(from + side * (startWidth * edge), edge ? glm::vec3(0) : startColour);
                vertex(to + side * (endWidth * edge), edge ? glm::vec3(0) : endColour);
            }
            glowTarget->indices.insert(glowTarget->indices.end(),
                                      {first, first + 1, first + 2, first + 1, first + 3, first + 2,
                                       first + 2, first + 3, first + 4, first + 3, first + 5, first + 4});
        };
        const auto flash = [&](glm::dvec3 point, double radius, glm::vec3 colour) {
            const auto centre = uint32_t(glowTarget->vertices.size());
            vertex(point, colour);
            constexpr unsigned sides = 10;
            for (unsigned j = 0; j <= sides; ++j) {
                const double angle = j * 2 * std::numbers::pi / sides;
                vertex(point + radius * (cameraRight * std::cos(angle) + cameraUp * std::sin(angle)),
                       glm::vec3(0));
                if (j)
                    glowTarget->indices.insert(glowTarget->indices.end(), {centre, centre + j, centre + j + 1});
            }
        };
        glowTarget=&explosionGlow;
        if(p.xalariaCombat)for(const auto& explosion:p.xalariaCombat->explosions()){
            const double sourceUnits=std::pow(10.,p.universe.describe(explosion.source.home).frame.logRadius);
            const auto centre=p.universe.pointRelative(explosion.source.home,
                (explosion.position+explosion.velocity*explosion.age)/sourceUnits,nav.anchor,nav.logDistance)-p.eye;
            const double distance=glm::length(centre),metres=distance*units;
            if(distance<1e-12||metres>60000.)continue;
            const auto axes=p.poseFor(explosion.source.home,nav).orientation*explosion.orientation;
            const auto localDirection=p.cameraInverse*(centre/distance);
            if(celestialOccluded(localDirection,metres,p.skyOccluders,nullptr,
                                 std::atan2(explosion.radius*9.6,metres)))continue;
            const auto image=sampleShipExplosion(explosion);
            if(explosion.age<.25&&metres<2500.&&p.explosionImpulses.size()<maxShipExplosions&&
               std::find(p.explosionImpulses.begin(),p.explosionImpulses.end(),explosion.source)==p.explosionImpulses.end()){
                p.explosionImpulses.push_back(explosion.source);
                if(p.cockpitMotionEnabled)p.cockpitMotion.impulse(CockpitImpulse::Impact,
                    std::clamp(explosion.radius*5./std::max(100.,metres),.025,.65),
                    glm::inverse(nav.orientation)*(centre/distance));
            }
            // Smooth, overlapping incandescent volumes, not flat polygon stars.
            const auto fireball=[&](glm::dvec3 point,double radius,glm::vec3 colour,float energy){
                if(radius<=0.||energy<=.001f)return;
                constexpr unsigned sides=20;
                const auto first=uint32_t(glowTarget->vertices.size());
                vertex(point,glm::mix(colour,image.coreColour,.38f)*energy);
                for(unsigned ring=0;ring<2;++ring)for(unsigned i=0;i<=sides;++i){
                    const double angle=double(i)*2.*std::numbers::pi/sides;
                    const double extent=radius*(ring?1.:.34);
                    vertex(point+extent*(cameraRight*std::cos(angle)+cameraUp*std::sin(angle)),
                        ring?glm::vec3(0):colour*(energy*.52f));
                    if(i&&ring==0)glowTarget->indices.insert(glowTarget->indices.end(),{first,first+i,first+i+1});
                    if(i&&ring==1){const auto inner=first+i,outer=first+1+(sides+1)+i;
                        glowTarget->indices.insert(glowTarget->indices.end(),{inner,outer-1,outer,inner,outer,inner+1});}
                }
            };
            fireball(centre,image.haloRadius/units,image.haloColour,image.flash*.29f+image.afterglow*.15f);
            for(size_t i=0;i<image.count;++i){
                const auto& mote=image.particles[i];
                const auto point=centre+axes*(mote.offset/units);
                const auto colour=mote.colour*mote.opacity;
                if(mote.kind==ExplosionParticleKind::Vapour){
                    explosionVapour.push_back({point,mote.radius/units,mote.colour,mote.opacity});
                }else if(mote.kind==ExplosionParticleKind::Fire){
                    fireball(point,mote.radius/units,mote.colour,mote.opacity);
                }else{
                    const auto velocity=axes*mote.velocity;
                    const auto tail=point-velocity*(.10/units);
                    const double width=std::max(mote.radius/units,screenWidth(point)*.65);
                    const float hot=float(1.-smoothstep(.6,7.,explosion.age));
                    ribbon(tail,point,width*.18,width*.45,glm::vec3(0),colour*hot);
                    if(mote.kind==ExplosionParticleKind::Ember){
                        flash(point,width*2.,colour*(hot*.7f));
                    }else{
                        const auto spin=glm::angleAxis(mote.spin*explosion.age,mote.axis);
                        constexpr std::array corners{glm::dvec3(1.5,0,0),glm::dvec3(-.6,.6,0),
                            glm::dvec3(-.5,-.5,.45),glm::dvec3(-.4,0,-.65)};
                        const auto first=uint32_t(miningChips.vertices.size());
                        for(unsigned j=0;j<corners.size();++j){
                            const auto v=point+axes*(spin*corners[j])*(mote.radius/units);
                            const auto shade=glm::vec3(.035,.023,.019)+mote.colour*(hot*(j==0?.46f:.09f));
                            miningChips.vertices.push_back({float(v.x),float(v.y),float(v.z),rgba(shade,mote.opacity)});
                        }
                        miningChips.indices.insert(miningChips.indices.end(),{first,first+1,first+2,first,first+3,first+1,
                            first,first+2,first+3,first+1,first+3,first+2});
                    }
                }
            }
            fireball(centre,image.coreRadius/units,image.coreColour,image.flash*.95f);
            // A thin spherical shock front expands in world space. Its grazing
            // rim is brightest; it never rotates to face the cockpit.
            if(image.shock>.001f){
                constexpr unsigned rows=12,columns=40;
                const auto first=uint32_t(glowTarget->vertices.size());
                for(unsigned y=0;y<=rows;++y)for(unsigned x=0;x<=columns;++x){
                    const double latitude=std::numbers::pi*double(y)/rows,longitude=2.*std::numbers::pi*double(x)/columns;
                    const auto normal=axes*glm::dvec3(std::sin(latitude)*std::cos(longitude),std::cos(latitude),
                                                     std::sin(latitude)*std::sin(longitude));
                    const auto point=centre+normal*(image.shockRadius/units);
                    const auto toward=glm::length(point)>1e-12?glm::normalize(-point):normal;
                    const double rim=std::pow(std::max(0.,1.-std::abs(glm::dot(normal,toward))),7.);
                    const float ripple=float(.78+.22*std::sin(longitude*7.+latitude*11.+explosion.age*4.));
                    vertex(point,image.shockColour*float(rim)*image.shock*ripple);
                    if(y&&x){const auto n=first+y*(columns+1)+x;
                        glowTarget->indices.insert(glowTarget->indices.end(),{n,n-1,n-columns-1,n-1,n-columns-2,n-columns-1});}
                }
            }
            p.stats.particles+=image.count;
        }
        // Brown released oxidizer and charcoal soot absorb light; adding their
        // RGB values to the glow buffer would turn them into luminous orange.
        std::sort(explosionVapour.begin(),explosionVapour.end(),[](const auto& a,const auto& b){
            return glm::dot(a.point,a.point)>glm::dot(b.point,b.point);
        });
        for(const auto& cloud:explosionVapour){
            constexpr unsigned sides=20;
            const auto first=uint32_t(explosionGas.vertices.size());
            const auto add=[&](glm::dvec3 point,float opacity){
                explosionGas.vertices.push_back({float(point.x),float(point.y),float(point.z),rgba(cloud.colour*opacity,opacity)});
            };
            add(cloud.point,cloud.opacity);
            for(unsigned ring=0;ring<2;++ring)for(unsigned i=0;i<=sides;++i){
                const double angle=double(i)*2.*std::numbers::pi/sides;
                add(cloud.point+cloud.radius*(ring?1.:.45)*(cameraRight*std::cos(angle)+cameraUp*std::sin(angle)),
                    ring?0.f:cloud.opacity*.65f);
                if(i&&ring==0)explosionGas.indices.insert(explosionGas.indices.end(),{first,first+i,first+i+1});
                if(i&&ring==1){const auto inner=first+i,outer=first+1+(sides+1)+i;
                    explosionGas.indices.insert(explosionGas.indices.end(),{inner,outer-1,outer,inner,outer,inner+1});}
            }
        }
        glowTarget=&weaponGlow;
        auto drawShot = [&](const WeaponShot& shot) {
            const auto source = p.universe.describe(shot.reference);
            const double sourceUnits = std::pow(10., source.frame.logRadius);
            const auto a = p.universe.pointRelative(shot.reference, shot.from / sourceUnits, nav.anchor,
                                                    nav.logDistance) -
                           p.eye;
            const auto b =
                p.universe.pointRelative(shot.reference, shot.to / sourceUnits, nav.anchor, nav.logDistance) -
                p.eye;
            const bool missile = shot.level == missileLauncher;
            const bool ion = shot.level == ionDisruptor;
            const auto colour = shot.enemy ? glm::vec3(1, .22, .12) : weaponSpec(shot.level).colour;
            const float pulse = float(1 - smoothstep(0, .16, shot.age));
            const auto ray = b - a;
            const double length = glm::length(ray);
            if (length < 1e-20)
                return;
            const auto axis = ray / length;
            const double metres = shot.scale / units;
            if (shot.utility) {
                const auto right = cameraRight * (metres * .1);
                addLine(beams, a, b, colour * pulse);
                addLine(beams, a + right, b + right, glm::vec3(pulse));
                addLine(beams, a - right, b - right, colour * pulse);
            } else if (pulse > 0 && !(missile && shot.hit)) {
                const auto width = [&](glm::dvec3 point) {
                    return std::clamp((ion ? .8 : .38) * metres, screenWidth(point) * 1.4,
                                      screenWidth(point) * 7.);
                };
                ribbon(a, b, width(a), width(b), colour * pulse, colour * (pulse * .8f));
                ribbon(a, b, width(a) * .31, width(b) * .31,
                       glm::mix(colour, glm::vec3(1), .75f) * pulse,
                       glm::mix(colour, glm::vec3(1), .75f) * pulse);
                flash(a, width(a) * (missile ? 5. : 3.2), colour * pulse);
            }
            if(shot.utility&&p.stats.miningParticles==0&&glm::length(shot.from-shot.to)>.01){
                const auto target=p.universe.describe(nav.selected.value_or(nav.anchor));
                auto mineral=target.environment.mineral;
                if(p.economy)if(const auto* seam=markedSeam(p.economy->exploration,target.address))
                    mineral=seam->resource==0?Mineral::Ice:seam->resource==1?Mineral::Iron:seam->resource==3?Mineral::Graphite:mineral==Mineral::Olivine?Mineral::Olivine:Mineral::Quartz;
                const auto normal=glm::normalize(shot.from-shot.to);
                const auto down=shot.reference.kind()==Kind::Planet&&glm::length(shot.to)>1e-8?-glm::normalize(shot.to):glm::dvec3(0,-1,0);
                const auto motes=miningImpactMotes(shot.to+normal*.1,normal,down,seedOf(target.address,p.universe.seed()),mineral,p.probeSeconds);
                const auto orientation=p.poseFor(shot.reference,nav).orientation;
                for(size_t i=0;i<motes.count;++i){const auto& m=motes.items[i];
                    const auto centre=p.universe.pointRelative(shot.reference,m.position/sourceUnits,nav.anchor,nav.logDistance)-p.eye;
                    if(m.spark){p.addBillboard(centre+p.eye,m.radius/units,m.colour*float(m.opacity));continue;}
                    auto& mesh=m.fragment?miningChips:miningDust;const auto first=uint32_t(mesh.vertices.size());
                    if(m.fragment){for(const auto corner:{glm::dvec3(1,0,0),glm::dvec3(-.6,.8,0),glm::dvec3(-.4,-.5,.7),glm::dvec3(0,0,-.7)}){
                            const auto point=centre+orientation*(corner*m.stretch)*(m.radius/units);mesh.vertices.push_back({float(point.x),float(point.y),float(point.z),rgba(m.colour*.45f,m.opacity)});}
                        mesh.indices.insert(mesh.indices.end(),{first,first+1,first+2,first,first+3,first+1,first,first+2,first+3,first+1,first+3,first+2});
                    }else{mesh.vertices.push_back({float(centre.x),float(centre.y),float(centre.z),rgba(m.colour*.35f,m.opacity)});
                        for(unsigned side=0;side<=8;++side){const double angle=side*2*std::numbers::pi/8;
                            const auto point=centre+p.cameraQ*glm::dvec3(std::cos(angle),std::sin(angle),0)*(m.radius/units);
                            mesh.vertices.push_back({float(point.x),float(point.y),float(point.z),rgba(m.colour*.35f,0)});
                            if(side)mesh.indices.insert(mesh.indices.end(),{first,first+side,first+side+1});}}
                }
                p.stats.miningParticles+=motes.count;
            }
            if (shot.hit && !shot.utility) {
                const bool subsystem = shot.component != ShipSystem::Hull;
                const float age = float(std::clamp(shot.age / weaponShotLifetime(shot), 0., 1.));
                const float fade = (1 - age) * (1 - age);
                const auto spark = missile || subsystem ? glm::vec3(1, .65f, .20f) : colour;
                const auto point = b - axis * (.28 * metres);
                const double pixel = screenWidth(point);
                const double radius = std::clamp((missile ? 34. : shot.disabled ? 22. : ion ? 12. : 7.) * metres,
                                                 7 * pixel, 80 * pixel);
                flash(point, radius * (.27 + age * .85), spark * fade);
                flash(point, radius * .22, glm::mix(spark, glm::vec3(1), .8f) * (fade * fade));
                constexpr double golden = 2.399963229728653;
                const unsigned sparks = missile || shot.disabled ? 20 : 12;
                for (unsigned j = 0; j < sparks; ++j) {
                    const double angle = golden * j;
                    const auto direction = cameraRight * std::cos(angle) + cameraUp * std::sin(angle);
                    const double travel = radius * (.2 + age) * (.50 + .50 * double((j * 13) % 17) / 16.);
                    const auto tip = point + direction * travel - axis * (travel * .13);
                    addLine(beams, tip - direction * (radius * .12 * (1 - age)), tip, spark * fade);
                }
                if (ion || missile || shot.disabled) {
                    const double ring = radius * (.22 + .9 * age);
                    constexpr unsigned sides = 24;
                    for (unsigned j = 0; j < sides; ++j) {
                        const double angle = j * 2 * std::numbers::pi / sides;
                        const double next = (j + 1) * 2 * std::numbers::pi / sides;
                        addLine(beams, point + ring * (cameraRight * std::cos(angle) + cameraUp * std::sin(angle)),
                                point + ring * (cameraRight * std::cos(next) + cameraUp * std::sin(next)),
                                colour * (fade * .65f));
                    }
                }
            }
        };
        if (p.weapons) {
            for (const auto& shot : p.weapons->shots())
                drawShot(shot);
            for (const auto& missile : p.weapons->missiles()) {
                const auto source = p.universe.describe(missile.reference);
                const auto tip = p.universe.pointRelative(
                    missile.reference, missile.position / std::pow(10., source.frame.logRadius),
                    nav.anchor, nav.logDistance) - p.eye;
                const auto axis = p.poseFor(missile.reference, nav).orientation * missile.direction;
                const auto colour = missile.target ? glm::vec3(.35f, .78f, 1) : glm::vec3(1, .27f, .10f);
                const auto tail = tip - axis * (std::min(78., 8. + missile.age * 360.) / units);
                const double width = std::max(.65 / units, screenWidth(tip) * 2.1);
                ribbon(tail, tip, width * 2.8, width, glm::vec3(0), colour);
                ribbon(glm::mix(tail, tip, .55), tip, width, width * .4, glm::vec3(0), glm::vec3(1));
                flash(tip, width * 2.3, colour);
            }
        }
        if(p.weapons)for(const auto& cloud:p.weapons->decoys()){
            const auto source=p.universe.describe(cloud.reference);
            const auto tip=p.universe.pointRelative(cloud.reference,cloud.position/std::pow(10.,source.frame.logRadius),nav.anchor,nav.logDistance)-p.eye;
            const auto velocity=p.poseFor(cloud.reference,nav).orientation*cloud.velocity;
            const auto tail=tip-velocity*(.14/units);
            const float fade=float(std::clamp((6-cloud.age)/2.,0.,1.));
            const auto colour=glm::mix(glm::vec3(.25,.85,1),glm::vec3(1,.58,.18),float((cloud.id%3)/2.))*fade;
            const double width=std::max(.8/units,screenWidth(tip)*2.4);
            ribbon(tail,tip,width*2,width,glm::vec3(0),colour);flash(tip,width*3.5,colour);
        }
        if (p.npcCombat)
            for (const auto& shot : p.npcCombat->shots())
                drawShot(shot);
        if(!explosionGlow.vertices.empty()||!explosionGas.vertices.empty()){
            const auto view=p.hyperion->shipBlastView();
            for(unsigned pass=0;pass<2;++pass)
                bgfx::setViewTransform(bgfx::ViewId(view+pass),glm::value_ptr(rotation),glm::value_ptr(projection));
            const uint64_t state=BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A|BGFX_STATE_DEPTH_TEST_GEQUAL|BGFX_STATE_MSAA;
            p.transient(explosionGlow,view,state|BGFX_STATE_BLEND_FUNC_SEPARATE(
                BGFX_STATE_BLEND_ONE,BGFX_STATE_BLEND_ONE,BGFX_STATE_BLEND_ONE,BGFX_STATE_BLEND_INV_SRC_ALPHA));
            p.transient(explosionGas,bgfx::ViewId(view+1),state|
                BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE,BGFX_STATE_BLEND_INV_SRC_ALPHA));
        }
        p.transient(weaponGlow, 63, false, true);
        p.transient(beams, 63, true, true);
        p.transient(miningChips,63,false,false,true,true,true);
        bgfx::setViewTransform(64,glm::value_ptr(rotation),glm::value_ptr(projection));bgfx::setViewRect(64,0,0,uint16_t(p.width),uint16_t(p.height));
        p.transient(miningDust,64,false,false,true,false,true);
    }
    if (opticalPlanet && (p.instruments || opticalDepth)) {
        const auto& planet = *opticalPlanet;
        const auto pose = p.poseFor(planet.address, nav);
        const double radiusMetres = std::pow(10., planet.frame.logRadius);
        const double radius = std::pow(10., planet.frame.logRadius - nav.logDistance);
        const auto localEye = glm::inverse(pose.orientation) * (p.eye - pose.position) / radius;
        const auto rotation = glm::mat4_cast(glm::quat(glm::inverse(pose.orientation) * p.cameraQ));
        const auto light = glm::normalize(glm::inverse(pose.orientation) * p.lightAt(planet.address, nav));
        const auto climate = planetClimate(planet);
        const double height = std::clamp(climate.scaleHeightMetres / radiusMetres, 1e-5, .02);
        const float strength = atmosphericOpticalStrength(
            climate.pressureBar, climate.temperatureK, (glm::length(localEye) - 1.) * radiusMetres,
            radiusMetres, height * radiusMetres, glm::dot(glm::normalize(localEye), light));
        p.hyperion->atmosphere(glm::vec3(localEye), glm::vec3(light), rotation, float(radius), float(height),
                               strength, float(p.width) / p.height, float(std::tan(fov * .5)),
                               hyperionPhase(planet.planetSeed ? planet.planetSeed : planet.seed),
                               float(radiusMetres / 1000.),
                               p.polarLights ? extractPolarLight(planet) : PolarLight{}, p.visualTime);
        ++p.stats.atmospheres;
    }
    const float focusTarget=boarding && p.cockpitVisible && !p.probeRemote ? 1.f : 0.f;
    const float focusStep=float(p.frameSeconds/.24);
    p.dockedFocus+=std::clamp(focusTarget-p.dockedFocus,-focusStep,focusStep);
    p.hyperion->resolveExterior(std::max(p.dockedFocus,float(smoothstep(.06,.82,warp.closure))));
    const bool cabinEffects = !scopeStar && p.instruments && overlay && !p.probeRemote && p.xalariaMap &&
                              p.hyperion->beginCabin(float(std::tan(fov*.5)),
                                  boarding?float(10.-boarding->freightBay().floor.left):13.f);
    if(scopeStar){
        p.hyperion->showStellarScope(*scopeStar,p.visualTime);
    }else if (!cabinEffects) {
        p.hyperion->presentExterior();
        bgfx::setViewFrameBuffer(cabinViewId,BGFX_INVALID_HANDLE);
        bgfx::setViewClear(cabinViewId,BGFX_CLEAR_DEPTH,0,1.f,0);
    }
    if (!scopeStar && p.instruments && overlay && !p.probeRemote) {
        const double aspect = double(p.width) / p.height;
        const bool xalaria = p.xalariaMap != nullptr;
        if (!bgfx::isValid(p.cabin.vertices) || p.cabinIsXalaria != xalaria || (!xalaria && p.cabinAspect != aspect)) {
            destroy(p.cabin);
            p.cabin = p.upload(xalaria ? xalariaCockpitMesh() : cockpitMesh(aspect));
            destroy(p.cabinGlazing);
            destroy(p.cabinYoke);
            destroy(p.cabinThrottle);
            destroy(p.cabinHatch);
            destroy(p.shipDeckMesh);
            destroy(p.dockingDeckMesh);
            destroy(p.dockingGlassMesh);
            destroy(p.airlockDoorMesh);
            destroy(p.airlockSillMesh);
            destroy(p.airlockIndicatorMesh);
            destroy(p.charterCrateMesh);
            p.renderedBoardingStation.reset();
            p.renderedStationSigns.clear();
            p.renderedStationSurvey.reset();
            p.renderedFreightDispatch.reset();
            if (xalaria) {
                p.cabinGlazing = p.upload(xalariaCockpitGlazing());
                p.cabinYoke = p.upload(xalariaCockpitYoke());
                p.cabinThrottle = p.upload(xalariaCockpitThrottle());
                p.cabinHatch = p.upload(xalariaCockpitHatch());
                p.shipDeckMesh = p.upload(xalariaShipDeck());
                p.airlockDoorMesh = p.upload(xalariaAirlockDoor());
                p.airlockSillMesh = p.upload(xalariaAirlockSill());
                p.airlockIndicatorMesh = p.upload(xalariaAirlockIndicators());
            }
            p.cabinAspect = aspect;
            p.cabinIsXalaria = xalaria;
        }
        // Seated head motion stays local. Walking also offsets the exterior
        // camera, so the view through the windows follows the player's position.
        const auto cabinView = glm::mat4_cast(glm::quat(glm::inverse(p.head.orientation))) *
                               glm::translate(glm::mat4(1), -glm::vec3(p.head.translation));
        const float cabinFar=boarding?float(10.-boarding->freightBay().floor.left):13.f;
        const auto cabinProjection = bgfx::getCaps()->homogeneousDepth
                                         ? glm::perspectiveRH_NO(float(fov), float(aspect), .025f, cabinFar)
                                         : glm::perspectiveRH_ZO(float(fov), float(aspect), .025f, cabinFar);
        bgfx::setViewRect(cabinViewId, 0, 0, uint16_t(p.width), uint16_t(p.height));
        bgfx::setViewMode(cabinViewId, bgfx::ViewMode::Sequential);
        bgfx::setViewTransform(cabinViewId, glm::value_ptr(cabinView), glm::value_ptr(cabinProjection));
        const Geometry source;
        const ScenePresentation presentation{source, nav.anchor, {.09f,.82f,.20f,0},
            hyperionPhase(p.universe.seed()), 1.f, {5.f,xalaria ? 1.f : 0.f,0,cabinEffects ? 1.f : 0.f}};
        const auto intoCabin=glm::inverse(nav.orientation*stationAim);
        auto sun=intoCabin*p.lightAt(nav.anchor,nav);
        const auto cabinEye = xalaria ? glm::vec3(p.head.translation) : glm::vec3(0);
        const auto annexLighting=boarding?xalariaStationAnnexLighting(*boarding):StationAnnexLighting{};
        auto windows=boarding?xalariaStationWindows(*boarding):StationWindowLighting{};
        const std::array<glm::vec4,2> doors{{
            {stationDeck::shipDoor,stationDeck::bridgeDoor,stationDeck::centre,stationDeck::doorHalf},
            {shipOpening,bridgeOpening,stationDeck::doorTop,stationDeck::doorTravel}
        }};
        if (boarding) {
            if (localStar.angularRadius>0.) {
                sun=p.head.orientation*localStar.direction;
                const double visible=stellarDiscVisibility(localStar.direction,localStar.angularRadius,
                                                           localStar.distanceMetres,p.contacts);
                const float peak=std::max({localStar.color.r,localStar.color.g,localStar.color.b,.001f});
                const double exposure=std::clamp(std::pow(localStar.angularRadius/.00465,.42),.21,2.2);
                windows[4]={localStar.color/peak*float(visible*exposure*1.28),float(localStar.angularRadius)};
            }
            if (p.xalariaMap && p.xalariaMap->wormhole) {
                const auto& mouth=*p.xalariaMap->wormhole;
                const auto delta=p.wormholePosition(mouth,nav)-nav.cameraPosition();
                const double distance=glm::length(delta)*std::pow(10.,nav.logDistance);
                if (distance>mouth.radius) {
                    const auto direction=glm::normalize(delta);
                    const double angular=std::atan(mouth.radius/distance);
                    if (!celestialOccluded(p.cameraInverse*direction,distance,p.contacts,nullptr,angular))
                        windows[5]={intoCabin*direction,float(angular)};
                }
            }
        }
        glm::vec3 sourceColour(1.f);
        float sourcePower=1.f,sourceRadius=.00465f;
        if(localStar.angularRadius>0.){
            const auto visible=stellarDiscVisibility(localStar.direction,localStar.angularRadius,
                                                     localStar.distanceMetres,p.contacts);
            const auto peak=std::max({localStar.color.r,localStar.color.g,localStar.color.b,.001f});
            sourceColour=localStar.color/peak;
            sourcePower=float(visible*std::clamp(std::pow(localStar.angularRadius/.00465,.35),.35,2.));
            sourceRadius=float(localStar.angularRadius);
        }
        p.hyperion->cabinSun(glm::inverse(cabinView),glm::vec3(sun),sourceColour,sourcePower,sourceRadius);
        const auto drawCabin = [&](const GPUMesh& part,const glm::mat4& model,std::optional<GlazingView> glazing={},
                                   float clipAbove=0.f,glm::vec3 tint=glm::vec3(1)) {
            if (!bgfx::isValid(part.vertices)) return;
            const bool instanced = bgfx::getAvailInstanceDataBuffer(1, 80) > 0;
            if (glazing && !instanced) return;
            bgfx::setVertexBuffer(0, part.vertices);
            bgfx::setIndexBuffer(part.indices);
            constexpr uint64_t coverageBlend = BGFX_STATE_BLEND_FUNC_SEPARATE(
                BGFX_STATE_BLEND_SRC_ALPHA,BGFX_STATE_BLEND_INV_SRC_ALPHA,
                BGFX_STATE_BLEND_ONE,BGFX_STATE_BLEND_INV_SRC_ALPHA);
            bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA |
                           (cabinEffects ? BGFX_STATE_WRITE_A : 0) |
                           (glazing ? (cabinEffects ? coverageBlend : BGFX_STATE_BLEND_ALPHA) : BGFX_STATE_WRITE_Z));
            if (instanced) {
                bgfx::InstanceDataBuffer instance;
                bgfx::allocInstanceDataBuffer(&instance, 1, 80);
                const Instance posed{model,glm::vec4(tint,1)};
                std::memcpy(instance.data, &posed, sizeof(posed));
                auto material=presentation;
                material.domain.z=clipAbove;
                p.hyperion->bind(material, cabinEye, glm::vec3(sun), glm::inverse(model), 0, p.visualTime);
                bgfx::setInstanceDataBuffer(&instance);
                p.hyperion->bindStationLighting(windows,annexLighting,doors);
                if (glazing)
                    p.hyperion->bindCockpitGlazing(*glazing);
                bgfx::submit(cabinViewId,glazing ? p.hyperion->cockpitGlazingProgram() : p.hyperion->cockpitProgram());
                p.stats.uploadBytes += 80;
            } else {
                bgfx::setTransform(glm::value_ptr(model));
                bgfx::submit(cabinViewId, p.colorProgram);
            }
        };
        drawCabin(p.cabin, glm::mat4(1));
        if (xalaria) {
            const auto controls = xalariaCockpitControlPoses(*p.instruments);
            const float door=float(p.shipInterior ? p.shipInterior->hatchOpening() : 0.);
            const float slide=.47f*float(smoothstep(0.,1.,door));
            const glm::mat4 identity(1);
            drawCabin(p.shipDeckMesh, identity);
            const auto drawAirlockDoor=[&](double x,double opening) {
                const auto threshold=glm::translate(identity,glm::vec3(x,0,0));
                drawCabin(p.airlockSillMesh,threshold);
                const auto tint=glm::mix(glm::vec3(1.f,.58f,.19f),glm::vec3(.28f,1.f,.53f),
                                         float(smoothstep(stationDeck::doorClear,1.,opening)))*(boarding?1.f:.37f);
                drawCabin(p.airlockIndicatorMesh,threshold,std::nullopt,0.f,tint);
                if (opening >= 1.) return;
                // The leaf retracts into its header and stays below the glass.
                drawCabin(p.airlockDoorMesh,glm::translate(identity,glm::vec3(x,stationDeck::doorTravel*opening,0)),
                          std::nullopt,float(stationDeck::doorTop));
            };
            drawAirlockDoor(stationDeck::shipDoor,shipOpening);
            if (boarding) {
                const auto visualKey=std::pair(boarding->key,look);
                if (p.renderedBoardingStation!=visualKey) {
                    destroy(p.dockingDeckMesh);
                    destroy(p.dockingGlassMesh);
                    destroy(p.charterCrateMesh);
                    p.dockingDeckMesh=p.upload(xalariaDockingDeck(*boarding,look));
                    p.dockingGlassMesh=p.upload(stationGlazing(*boarding));
                    p.charterCrateMesh=p.upload(xalariaCharterCrate(*boarding));
                    p.renderedBoardingStation=visualKey;
                    p.renderedStationSigns=xalariaStationSigns(*boarding);
                    p.renderedFreightDispatch.reset();
                    p.renderedStationSurvey.reset();
                    if (p.xalariaMap && p.xalariaMap->catalog)
                        for (const auto& annex : boarding->annexes)
                            if (annex.room.kind==StationModuleKind::Analysis)
                                p.renderedStationSurvey=xalariaStationSurveyDisplay(annex,*p.xalariaMap);
                }
                drawCabin(p.dockingDeckMesh,identity);
                drawAirlockDoor(stationDeck::bridgeDoor,bridgeOpening);
                if (loadingCharter && loadingCharter->units && !freightClaimed)
                    drawCabin(p.charterCrateMesh,identity);
            }
            drawCabin(p.cabinHatch, glm::translate(identity,glm::vec3(slide,0,0)));
            drawCabin(p.cabinHatch, glm::translate(identity,glm::vec3(-slide,0,0)) *
                                  glm::scale(identity,glm::vec3(-1,1,1)));
            drawCabin(p.cabinYoke, controls.yoke);
            drawCabin(p.cabinThrottle, controls.throttle);
            if (walking || seatMoving)
                p.hyperion->drawDeckCrew(p.shipInterior->crew(),p.head.translation,cabinViewId);
            if(boardedVessel&&(walking||seatMoving)){
                DeckCrewPose officer;
                officer.position={boarding->freightPad.left-.47,stationDeck::centre+1.18};
                officer.yaw=std::numbers::pi*.5;
                officer.rim=glm::mix(stationAccent(boarding->key.role),glm::vec3(.69f,.90f,1.f),.38f);
                officer.taskPhase=std::fmod(p.visualTime*.74,2.*std::numbers::pi);
                p.hyperion->drawDeckCrew(std::span(&officer,1),p.head.translation,cabinViewId);
            }
            const auto drawDisplay = [&](const CabinDisplay& display,bool opaque=false,bool plaque=false) {
                p.transient(display.content.panels,cabinViewId,
                            BGFX_STATE_WRITE_RGB | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA |
                                (opaque && cabinEffects ? BGFX_STATE_WRITE_A : 0) |
                                (plaque ? BGFX_STATE_WRITE_Z : 0),
                            &display.model);
                float pixelsPerUnit=plaque?.001f:0.f;
                const auto panelProjection=cabinProjection*cabinView*display.model;
                const auto centre=panelProjection*glm::vec4(float(display.size.x)*.5f,float(display.size.y)*.5f,0,1);
                const auto down=centre+panelProjection[1];
                if (centre.w>.025f && down.w>.025f) {
                    const auto size=glm::vec2(p.width,p.height)*.5f;
                    const auto point=glm::vec2(centre)/centre.w;
                    const auto vertical=(glm::vec2(down)/down.w-point)*size;
                    pixelsPerUnit=std::max(.001f,glm::length(vertical));
                    if (plaque) {
                        const auto right=centre+panelProjection[0];
                        if (right.w>.025f) {
                            const auto horizontal=(glm::vec2(right)/right.w-point)*size;
                            const float area=std::abs(horizontal.x*vertical.y-horizontal.y*vertical.x);
                            // A plaque seen edge-on has too little width for legible glyphs.
                            pixelsPerUnit=std::max(.001f,area/std::max(pixelsPerUnit,glm::length(horizontal)));
                        }
                    }
                }
                p.hyperion->drawAtlasText(display.content.atlasGlyphs,cabinViewId,&display.model,pixelsPerUnit);
            };
            const auto drawDirectionalSign=[&](const CabinDisplay& display) {
                const auto offset=cabinEye-glm::vec3(display.model[3]);
                if (glm::dot(offset,glm::vec3(display.model[2]))>0.f)
                    drawDisplay(display,true,true);
            };
            for (const auto& sign : xalariaShipSigns())
                drawDirectionalSign(sign);
            const auto& boardingSigns=xalariaAirlockSigns();
            const size_t signCount=boarding?boardingSigns.size():1;
            for (size_t i=0;i<signCount;++i)
                drawDirectionalSign(boardingSigns[i]);
            if (boarding)
                for (const auto& sign : p.renderedStationSigns)
                    drawDirectionalSign(sign);
            if (boarding)
                drawCabin(p.dockingGlassMesh,identity,GlazingView::TunnelInterior);
            drawCabin(p.cabinGlazing,identity,GlazingView::Cabin);
            const auto fuel = p.xalariaEconomy ? p.xalariaEconomy->fuel() : 0.;
            const auto capacity = p.xalariaEconomy ? p.xalariaEconomy->fuelCapacity() : 0.;
            for (const auto& display : xalariaCockpitDisplays(*p.instruments,fuel,capacity))
                drawDisplay(display);
            if ((walking || seatMoving) && p.xalariaEconomy)
                for (const auto& display : xalariaDeckDisplays(*p.instruments,*p.xalariaMap,*p.xalariaEconomy))
                    drawDisplay(display);
            if(prize)drawDisplay(xalariaPrizeDisplay(*boarding,*boardedVessel,*p.xalariaCombat));
            else if (loadingCharter) {
                if (!p.renderedFreightDispatch || p.renderedFreightClaimed!=freightClaimed) {
                    p.renderedFreightDispatch=xalariaLoadingBayDisplay(*boarding,*p.xalariaEconomy,*loadingCharter,freightClaimed,
                        boardedVessel?std::string_view(boardedVessel->body.name):std::string_view{});
                    p.renderedFreightClaimed=freightClaimed;
                }
                drawDisplay(*p.renderedFreightDispatch);
            }
            if (boarding && p.renderedStationSurvey)
                drawDisplay(*p.renderedStationSurvey,true);
        }
    }
    if (cabinEffects)
        p.hyperion->finishCabin();

    if (p.instruments && overlay) {
        const auto& viewport = p.hudViewport;
        const double aspect = viewport.aspect();
        auto cockpit = *p.instruments;
        if(cockpit.phase!="CAVERN EXPLORATION") {
            cockpit.weather = p.weatherReport;
            cockpit.weatherDetail = p.weatherDetail;
            cockpit.weatherSeverity = p.weatherSeverity;
        }
        if(cockpit.environmentalHeat>.15||cockpit.electricalCharge>.15||cockpit.surfaceScanQuality<.8)
            cockpit.weatherDetail="HEAT "+std::to_string(int(cockpit.environmentalHeat*100))+"% / CHARGE "+
                std::to_string(int(cockpit.electricalCharge*100))+"% / SCAN "+std::to_string(int(cockpit.surfaceScanQuality*100))+"%";
        MicroView micro;
        if(cockpit.shrinking){const auto current=p.details.find(nav.anchor);
            const auto geometry=current==p.details.end()?std::shared_ptr<const Geometry>{}:current->second.blend.current;
            micro=extractMicroView(p.universe,nav,cockpit.hullMetres,geometry.get());}
        const bool strategyOpen=p.xalariaCombat&&p.xalariaCombat->strategy().open;
        const bool convoysOpen=!strategyOpen&&p.xalariaCombat&&p.xalariaCombat->convoys().open;
        const bool combatOpen=!strategyOpen&&!convoysOpen&&p.xalariaCombat&&p.xalariaCombat->open;
        const bool storiesOpen = !strategyOpen&&!convoysOpen&&!combatOpen&&p.xalariaStories && p.xalariaStories->open;
        const bool economyOpen = !strategyOpen&&!convoysOpen&&!combatOpen&&!storiesOpen && p.xalariaEconomy && p.xalariaEconomy->view.open;
        const bool atlasOpen = !strategyOpen&&!convoysOpen&&!combatOpen&&!storiesOpen && !economyOpen && p.xalariaMap && p.xalariaMap->open;
        const bool menuOpen = strategyOpen||convoysOpen||combatOpen||atlasOpen || economyOpen || storiesOpen;
        const bool flightHud = p.xalariaMap && !walking && !seatMoving && !menuOpen && !p.cockpitHelp && !scopeStar;
        if (p.xalariaMap) {
            const double seconds = p.frameSeconds;
            // Precision rebases change the local axes even when the ship is flying straight.
            // Keep the HUD in system axes across terrain tiles and planetary frames.
            const auto system = std::find_if(nodes.begin(), nodes.end(), [](const Node& node) {
                return node.address.kind() == Kind::System;
            });
            const auto& reference = system == nodes.end() ? nav.anchor : system->address;
            const auto view = glm::conjugate(p.poseFor(reference, nav).orientation) * p.cameraQ;
            const bool sameReference = p.previousHudReference == reference;
            const auto delta = glm::normalize(glm::conjugate(p.previousHudView) * view);
            const double turn = 2.*std::atan2(glm::length(glm::dvec3(delta.x,delta.y,delta.z)),std::abs(delta.w));
            const double turnRate = std::max(turn/std::max(.001,seconds),glm::length(cockpit.localAngularVelocity));
            p.flightHud.update(seconds, turnRate, flightHud && sameReference);
            p.previousHudReference = reference;
            p.previousHudView = view;
        }
        const bool showFlightMarkers = !p.xalariaMap || p.flightHud.showMarkers();
        const bool deckHud = walking && !seatMoving && !menuOpen && !p.cockpitHelp;
        auto hud = (scopeStar || deckHud || (seatMoving && !menuOpen)) ? CockpitOverlay{} :
                   strategyOpen?xalariaStrategyOverlay(p.xalariaCombat->strategy(),*p.xalariaEconomy,aspect):
                   convoysOpen?xalariaConvoyOverlay(p.xalariaCombat->convoys(),*p.xalariaEconomy,aspect,p.activity?p.activity->seconds:0.):
                   combatOpen?xalariaCombatOverlay(*p.xalariaCombat,*p.xalariaEconomy,aspect):
                   storiesOpen ? xalariaStoryOverlay(*p.xalariaStories,p.storyContext,
                                 p.activity?p.activity->seconds:0.,aspect,p.storyRouteAction) :
                   economyOpen ? xalariaEconomyOverlay(*p.xalariaEconomy,aspect) : atlasOpen ? xalariaMapOverlay(*p.xalariaMap,aspect) : cockpitOverlay(cockpit, aspect, p.xalariaMap?"XALARIA":renderModeName(p.mode), notice, p.contacts,
                                        p.activityContacts, p.cockpitHelp, viewport.projectionScale, p.market,
                                        stationAim * p.head.orientation, p.crew,p.expeditionPlan,&micro,showFlightMarkers);
        if(flightHud)xalariaShieldHud(hud,aspect,p.playerShield);
        if(scopeStar){
            const double room=900*aspect-64;
            const glm::vec3 ink{.83f,.90f,.96f};
            atlasText(hud,"Stellar survey",32,28,room,19,{.47f,.69f,.83f});
            atlasText(hud,scopeStar->label(),32,53,room,35,ink);
            atlasText(hud,stellarKindName(scopeStar->stellar->kind),32,99,room,22,ink);
            atlasText(hud,std::to_string(int(std::round(scopeStar->temperatureK)))+" K   /   Radius "+
                      std::to_string(int(std::round(scopeStar->stellarRadiusMetres/1000.)))+" km",32,798,room,19,ink);
            atlasText(hud,"F6 or Esc   Return to flight",32,839,room,18,{.47f,.69f,.83f});
        }
        if (deckHud) {
            p.deckRoomLabel.update(p.shipInterior->room(),p.frameSeconds);
            if(boardedVessel){
                xalariaStationLocation(hud,p.deckRoomLabel.text(),boardedVessel->body.name,
                                      boarding->key.role,p.deckRoomLabel.opacity());
            }else if (boarding && p.xalariaEconomy && p.xalariaEconomy->dockedPlace()) {
                xalariaStationLocation(hud,p.deckRoomLabel.text(),
                    p.xalariaEconomy->placeName(*p.xalariaEconomy->dockedPlace()),boarding->key.role,p.deckRoomLabel.opacity());
            } else {
                atlasText(hud,p.deckRoomLabel.text(),32,31,320,26,{.90,.95,.96},p.deckRoomLabel.opacity());
                if (p.xalariaMap && p.xalariaMap->flying)
                    atlasText(hud,"Course active",32,67,320,16,{.62,.88,.87});
            }
            const bool atTerminal=p.xalariaEconomy && p.shipInterior->cargoTerminalInReach();
            const bool linked=atTerminal && p.xalariaEconomy->dockedPlace().has_value();
            constexpr std::string_view movementHint="WASD   Walk    /    Mouse   Look";
            std::string_view prompt=movementHint;
            switch (p.shipInterior->boardingInteraction()) {
            case BoardingInteraction::ShipDoor:
                if (!boarding) prompt="Airlock sealed   /   Dock to board";
                else if (shipOpening>=stationDeck::doorClear)
                    prompt=p.shipInterior->position().x>stationDeck::shipDoor?"E   Seal ship door":"Walk through to the ship";
                else prompt="E   Open ship door";
                break;
            case BoardingInteraction::BridgeDoor:
                prompt=bridgeOpening>=stationDeck::doorClear?"Walk through the bridge door":"E   Cycle bridge door";
                break;
            case BoardingInteraction::Freight:
                if(prize)prompt=p.xalariaCombat->prizeCargo(boardedVessel->body.id)>0?"E   Seize prize cargo":"Prize cargo already transferred";
                else if (loadingCharter)
                    prompt=freightClaimed?"Today's freight collected":boardedVessel?"E   Collect courier parcel":
                           loadingCharter->units?"E   Collect sealed freight":"No charter available today";
                break;
            case BoardingInteraction::None:
                if (p.shipInterior->seatInReach()) prompt="E   Sit at the helm";
                else if (atTerminal) prompt=linked?"E   Cargo market    /    B   Case files":"B   Case files    /    Dock to trade";
                break;
            }
            const auto deckPosition=p.shipInterior->position();
            if (boarding && deckPosition.x>stationDeck::bridgeDoor-1.10 &&
                deckPosition.x<stationDeck::shipDoor+1.10 &&
                std::abs(deckPosition.y-stationDeck::centre)<stationDeck::doorHalf+.35) {
                switch (p.shipInterior->airlockMotion()) {
                case AirlockMotion::SealingShip: prompt="Sealing ship door"; break;
                case AirlockMotion::SealingBridge: prompt="Sealing bridge door"; break;
                case AirlockMotion::OpeningShip: prompt="Opening ship door"; break;
                case AirlockMotion::OpeningBridge: prompt="Opening bridge door"; break;
                case AirlockMotion::Holding: break;
                }
            }
            p.deckPromptLabel.update(prompt,p.frameSeconds);
            atlasText(hud,p.deckPromptLabel.text(),32,841,520,19,{.90,.95,.96},p.deckPromptLabel.opacity());
            if (!cockpit.stationStatus.empty())
                atlasText(hud,cockpit.stationStatus,32,806,900*aspect-64,19,{.97,.78,.43});
        }
        if(flightHud&&!cockpit.contactsExpanded)
            xalariaFlightOverlay(hud,*p.xalariaMap,aspect);
        if(flightHud&&p.xalariaCombat){
            const auto selected=std::find_if(p.activityContacts.begin(),p.activityContacts.end(),[&](const auto& c){return p.xalariaCombat->target==c.body.id;});
            xalariaCombatHud(hud,*p.xalariaCombat,aspect,selected==p.activityContacts.end()?nullptr:&*selected,viewport.projectionScale);
        }
        if(flightHud&&p.xalariaCombat)xalariaConvoyHud(hud,p.xalariaCombat->convoys(),aspect,p.activity?p.activity->seconds:0.);
        if(flightHud&&p.xalariaEconomy)xalariaEconomyHud(hud,*p.xalariaEconomy,aspect,false);
        const bool radioShown=(flightHud||deckHud)&&p.xalariaRadio&&p.xalariaRadio->open;
        if(radioShown)
            xalariaRadioOverlay(hud,*p.xalariaRadio,aspect);
        if (p.validateCockpit) {
            const auto conflicts = cockpitTextConflicts(hud);
            if (!conflicts.empty()) {
                const auto [a, b] = conflicts.front();
                throw std::runtime_error("Native HUD text overlap: " + hud.textRuns[a].value + " / " +
                                         hud.textRuns[b].value);
            }
            for (const auto& run : hud.textRuns)
                if (run.bounds.x < 0 || run.bounds.y < 0 || run.bounds.x + run.bounds.w > 900 * aspect + .01 ||
                    run.bounds.y + run.bounds.h > 900.01)
                    throw std::runtime_error("Native HUD text outside canvas: " + run.value);
        }
        // Tint the flight HUD with its surroundings while retaining the warm
        // warning colours and the atlas's route colours.
        const float lookOpacity = flightHud && !cockpit.contactsExpanded
            ? float(glm::smoothstep(std::cos(glm::radians(58.)), std::cos(glm::radians(28.)),
                                    std::cos(p.cockpitLook.x) * std::cos(p.cockpitLook.y)))
            : 1.f;
        const float hudOpacity = flightHud ? lookOpacity*p.flightHud.opacity() : 1.f;
        for(size_t i=0;i<hud.atlasGlyphs.size();++i) {
            auto& colour=hud.atlasGlyphs[i].colour;
            colour=(colour&0x00ffffffu)|(uint32_t(float(colour>>24)*hudOpacity)<<24);
        }
        const auto hudLook = menuOpen ? glm::vec3(0.f) : p.regionalLook;
        const auto colourInterface = [hudLook,hudOpacity](Mesh& mesh) { for(auto& vertex:mesh.vertices){
            const auto value=vertex.color;
            const glm::vec3 c=glm::vec3(value&255,(value>>8)&255,(value>>16)&255)/255.f;
            const float y=glm::dot(c,glm::vec3(.299f,.587f,.114f));
            glm::vec3 warm=y<.32f?glm::vec3(y*1.08f,y*.43f,y*.28f):glm::vec3(y*1.25f,y*1.10f,y*.72f);
            const bool warning = c.r > c.g*1.45f && c.r > c.b*1.35f && c.r > .4f;
            if(warning)warm=glm::vec3(c.r,.25f+y*.21f,.10f+y*.12f);
            else if(c.g>c.r*1.28f&&c.g>.7f)warm=glm::vec3(1.f,.80f,.22f)*std::min(1.f,y*1.28f);
            const auto mist = y < .32f ? y * glm::vec3(.36f,.72f,.49f) : y * glm::vec3(.95f,1.18f,.76f);
            const auto tide = y < .32f ? y * glm::vec3(.33f,.54f,.75f) : y * glm::vec3(.82f,1.06f,1.20f);
            const auto acid = y < .32f ? y * glm::vec3(.61f,.63f,.68f) : y * glm::vec3(1.15f,1.10f,.79f);
            const auto colour = warning ? warm :
                warm + (mist - warm)*hudLook.x + (tide - warm)*hudLook.y + (acid - warm)*hudLook.z;
            vertex.color=rgba(glm::clamp(colour,glm::vec3(0),glm::vec3(1)),float(value>>24)/255.f*hudOpacity);
        }};
        if (!strategyOpen && !convoysOpen && !combatOpen && !atlasOpen && !storiesOpen)
            for(auto* mesh:{&hud.panels,&hud.strokes,&hud.lettering})colourInterface(*mesh);
        colourInterface(p.prospectMarkers);
        p.cockpitHitRegions = hud.hitRegions;
        p.cockpitContactHits = hud.contactHits;
        maskCockpitMarkers(p.prospectMarkers, viewport, hud);
        const auto projection = bgfx::getCaps()->homogeneousDepth
                                    ? glm::orthoRH_NO(0.f, float(900 * aspect), 900.f, 0.f, -1.f, 1.f)
                                    : glm::orthoRH_ZO(0.f, float(900 * aspect), 900.f, 0.f, -1.f, 1.f);
        bgfx::setViewRect(254, uint16_t(viewport.x), uint16_t(viewport.y), uint16_t(viewport.width),
                          uint16_t(viewport.height));
        bgfx::setViewTransform(254, nullptr, glm::value_ptr(projection));
        bgfx::setViewMode(254, bgfx::ViewMode::Sequential);
        p.transient(hud.panels, 254, false, false, false);
        p.transient(hud.strokes, 254, true, false, false);
        if(p.xalariaMap){
            // Shape the surviving flight text only after modal occlusion and
            // regional tinting. Hit regions and the tested layout stay exact;
            // the same high-resolution atlas used by physical displays draws
            // the glyphs instead of thousands of block-font rectangles.
            CockpitOverlay type;
            for(const auto& run:hud.textRuns){
                if(!run.vertexCount || run.firstVertex>=hud.lettering.vertices.size())continue;
                const auto colour=hud.lettering.vertices[run.firstVertex].color;
                const glm::vec3 rgb=glm::vec3(colour&255,(colour>>8)&255,(colour>>16)&255)/255.f;
                const double nominal=run.bounds.h*1.20;
                const double fontSize=nominal*std::min(1.,run.bounds.w/
                                                       std::max(.001,atlasTextWidth(run.value,nominal)));
                atlasText(type,run.value,run.bounds.x,run.bounds.y,run.bounds.w+.01,
                          fontSize,rgb,float(colour>>24)/255.f);
            }
            hud.atlasGlyphs.insert(hud.atlasGlyphs.end(),type.atlasGlyphs.begin(),type.atlasGlyphs.end());
        }else{
            p.transient(hud.lettering, 254, false, false, false);
        }
        if (!hud.atlasGlyphs.empty())
            p.hyperion->drawAtlasText(hud.atlasGlyphs, 254);
    }
    auto ortho =
        glm::orthoRH_NO(0.f, static_cast<float>(p.width), static_cast<float>(p.height), 0.f, -1.f, 1.f);
    if (!bgfx::getCaps()->homogeneousDepth)
        ortho =
            glm::orthoRH_ZO(0.f, static_cast<float>(p.width), static_cast<float>(p.height), 0.f, -1.f, 1.f);
    bgfx::setViewTransform(255, nullptr, glm::value_ptr(ortho));
    bgfx::setViewRect(255, 0, 0, static_cast<uint16_t>(p.width), static_cast<uint16_t>(p.height));
    bgfx::setViewTransform(253, nullptr, glm::value_ptr(ortho));
    bgfx::setViewRect(253, 0, 0, static_cast<uint16_t>(p.width), static_cast<uint16_t>(p.height));
    if (!p.instruments)
        p.transient(locators, 255, true, false, false);
    if (!scopeStar && overlay && !p.specimen && !walking && !seatMoving &&
        (!p.xalariaMap || p.flightHud.showMarkers()))
        p.transient(p.prospectMarkers, p.cockpitVisible ? 253 : 255, true, false, false);
    bgfx::dbgTextClear();
    if (looking && !p.instruments)
        bgfx::dbgTextPrintf(p.width / 16, p.height / 32, 0x0f, "+");
    if (overlay && !p.instruments) {
        const auto& node = nodes.back();
        auto info = cache.stats();
        int bottom = std::max(10, p.height / 16 - 9);
        bgfx::dbgTextPrintf(3, 2, 0x0f, "I N F I N I T E");

        bgfx::dbgTextPrintf(3, 3, 0x0b, "%s", std::string(renderModeName(p.mode)).c_str());
        bgfx::dbgTextPrintf(3, 4, 0x08, "A DETERMINISTIC UNIVERSE   /   SEED %08llX",
                            static_cast<unsigned long long>(p.universe.seed() & 0xffffffff));
        bgfx::dbgTextPrintf(3, bottom, 0x0f, "%s", node.label().c_str());
        bgfx::dbgTextPrintf(3, bottom + 1, 0x07, "VIEW  10^%+.2f m    OBJECT  10^%+.2f m", nav.logDistance,
                            node.frame.logRadius);
        if (node.address.kind() >= Kind::Planet) {
            const auto& mat = material(node.environment.mineral);
            bgfx::dbgTextPrintf(3, bottom + 2, 0x0b, "%s / %s    %.0f K    %.2f GPa",
                                std::string(mat.name).c_str(), std::string(mat.formula).c_str(),
                                node.environment.temperature, node.environment.pressureGPa);
        } else
            bgfx::dbgTextPrintf(3, bottom + 2, 0x08, "%.1f Gyr    METALS %.0f%%    %zu MARKERS",
                                node.environment.ageGyr, node.environment.chemistry.metal * 100, markers);
        bgfx::dbgTextPrintf(3, bottom + 3, 0x08, "LOC %s", nav.anchor.str().c_str());
        if (nav.selected)
            bgfx::dbgTextPrintf(3, bottom + 4, 0x07, "APPROACH %s TO DESCEND    F FOCUS    BACKSPACE PARENT",
                                std::string(kindName(nav.selected->kind())).c_str());
        else
            bgfx::dbgTextPrintf(3, bottom + 4, 0x07,
                                "MATERIAL LATTICE / STATIC EDUCATIONAL MODEL    BACKSPACE  PARENT");
        bgfx::dbgTextPrintf(3, bottom + 5, 0x08,
                            "WASD FLY   Q/E DOWN/UP   C MOUSE LOOK   APPROACH TO AUTO SCALE   SCROLL SCALE");
        bgfx::dbgTextPrintf(3, bottom + 6, 0x08,
                            "SPEED x%.3g   -/+ ADJUST   SHIFT BOOST   CTRL FINE   CLICK SELECT   ESC %s",
                            nav.speedMultiplier(), looking ? "RELEASE MOUSE" : "QUIT");
        bgfx::dbgTextPrintf(3, bottom + 7, 0x08,
                            "%.0f FPS   GEN %.1f ms   %zu JOBS   CPU CACHE %.1f MiB   GPU %.1f MiB", fps,
                            info.lastMs, info.pending, info.bytes / 1048576., p.gpuBytes / 1048576.);
        if (!p.sectionLabel.empty())
            bgfx::dbgTextPrintf(3, 8, 0x08, "LOCAL MATERIAL SECTION / %s", p.sectionLabel.c_str());
        if (node.address.kind() == Kind::Galaxy)
            bgfx::dbgTextPrintf(3, 10, 0x08, "%s / GAS %.0f%%",
                                std::string(processName(node.galaxy.type)).c_str(),
                                node.galaxy.gasFraction * 100);
        else if (node.address.kind() >= Kind::Rock)
            bgfx::dbgTextPrintf(3, 10, 0x08, "%s / COOLING %.2f / STRAIN %.2f",
                                std::string(processName(node.rock.type)).c_str(),
                                node.environment.coolingRate, node.environment.strain);
        else if (node.geology)
            bgfx::dbgTextPrintf(3, 10, 0x08, "%s / ACTIVITY %.2f",
                                std::string(processName(node.geology->type)).c_str(), node.geology->activity);
        if (node.address.kind() == Kind::Lattice)
            bgfx::dbgTextPrintf(
                3, 12, 0x08, "%s",
                std::string(processName(crystalCell(node.environment.mineral, node.environment).phase))
                    .c_str());
        if (!notice.empty())
            bgfx::dbgTextPrintf(3, 6, 0x0b, "%.*s", static_cast<int>(notice.size()), notice.data());
        if (!info.error.empty())
            bgfx::dbgTextPrintf(3, 7, 0x0c, "GENERATION ERROR: %s", info.error.c_str());
    }
    cache.finishFrame();
    p.prune();
    p.stats.cpuMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    bgfx::frame();
    p.seatTransition.update(p.frameSeconds);
    p.stats.submitMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() -
        p.stats.cpuMs;
    const auto* st = bgfx::getStats();
    p.stats.gpuBytes = p.gpuBytes + p.craftBytes + p.surfaceMeshBytes + p.probeRigBytes;
    p.stats.effectBytes = p.hyperion->targetBytes();
    p.stats.shadowBytes = p.hyperion->sunBytes();
    p.stats.shadowUpdates = p.hyperion->sunUpdated();
    p.stats.shadowActive = p.hyperion->sunActive();
    p.stats.materialBytes = p.hyperion->materialBytes();
    p.stats.weatherPending = p.hyperion->weatherPending();
    p.stats.weatherCacheBytes = p.hyperion->weatherStats().bytes;
    p.stats.weatherGpuBytes = p.hyperion->weatherGpuBytes();
    p.stats.drawCalls = st->numDraw;
    p.stats.gpuMs = st->gpuTimerFreq
                        ? 1000. * static_cast<double>(st->gpuTimeEnd - st->gpuTimeBegin) / st->gpuTimerFreq
                        : 0;
    return p.stats;
}
std::optional<Address> Renderer::pick(double x, double y) const {
    const auto& p = *impl_;
    if (p.cockpitVisible && (p.cockpitHelp || (p.instruments&&p.instruments->discovery&&p.instruments->discovery->open) || (p.instruments&&p.instruments->vessel&&p.instruments->vessel->open) || (p.expeditionPlan&&p.expeditionPlan->open) || (p.market && p.market->open) || (p.crew && p.crew->open) ||
                             p.instruments->phase == "MINING PROBE CAMERA"))
        return {};
    if(p.cockpitVisible){const auto point=p.hudViewport.toCanvas({x,y});
        for(const auto& hit:p.cockpitContactHits)if(hit.bounds.contains(point)){
            if(hit.life)return nextLivingWorld(p.contacts,p.lifeSelection);
            return hit.world;
        }
    }
    if (p.cockpitVisible &&
        std::any_of(p.cockpitHitRegions.begin(), p.cockpitHitRegions.end(),
                    [&](const auto& r) { return r.contains(p.hudViewport.toCanvas({x, y})); }))
        return {};
    const Pick* chosen = nullptr;
    double best = 40 * 40;
    for (auto& candidate : p.picks) {
        double distance = glm::dot(candidate.screen - glm::dvec2(x, y), candidate.screen - glm::dvec2(x, y));
        if (distance < best) {
            best = distance;
            chosen = &candidate;
        }
    }
    return chosen ? std::optional<Address>(chosen->address) : std::optional<Address>{};
}
void Renderer::screenshot(const std::string& file) {
    bgfx::requestScreenShot(BGFX_INVALID_HANDLE, file.c_str());
}
std::string Renderer::backend() const {
    return bgfx::getRendererName(bgfx::getRendererType());
}
} // namespace infinity

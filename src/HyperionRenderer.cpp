#include "HyperionRenderer.hpp"
#include "AtlasFontData.hpp"
#include "CrewSilhouette.hpp"
#include "infinity/ShipInterior.hpp"
#include "infinity/Cockpit.hpp"
#include "infinity/MaterialTexture.hpp"
#include "infinity/StellarEvolution.hpp"
#include "infinity/WorldDepth.hpp"
#include <HyperionShaders.hpp>
#include <algorithm>
#include <cstring>
#include <numbers>
#include <glm/gtc/type_ptr.hpp>
#include <stdexcept>

namespace infinity {
namespace {
bgfx::ShaderHandle shader(const char* name) {
#define LOAD_SHADER(NAME, BACKEND)                                                                           \
    if (std::strcmp(name, #NAME) == 0)                                                                       \
    return bgfx::createShader(bgfx::copy(NAME##_##BACKEND, sizeof(NAME##_##BACKEND)))
#define LOAD_BACKEND(BACKEND)                                                                                \
    LOAD_SHADER(vs_xalaria_crew, BACKEND);                                                                \
    LOAD_SHADER(fs_xalaria_crew, BACKEND);                                                                \
    LOAD_SHADER(vs_xalaria_atlas, BACKEND);                                                                \
    LOAD_SHADER(fs_xalaria_atlas, BACKEND);                                                                \
    LOAD_SHADER(vs_planet_rings, BACKEND);                                                                   \
    LOAD_SHADER(fs_planet_rings, BACKEND);                                                                   \
    LOAD_SHADER(vs_hyperion, BACKEND);                                                                       \
    LOAD_SHADER(vs_hyperion_smooth, BACKEND);                                                                \
    LOAD_SHADER(vs_hyperion_shadow, BACKEND);                                                                \
    LOAD_SHADER(fs_hyperion_shadow, BACKEND);                                                                \
    LOAD_SHADER(fs_hyperion_coverage, BACKEND);                                                              \
    LOAD_SHADER(fs_hyperion_cloud, BACKEND);                                                                 \
    LOAD_SHADER(fs_hyperion_cloud_volume, BACKEND);                                                          \
    LOAD_SHADER(fs_hyperion_stellar, BACKEND);                                                               \
    LOAD_SHADER(fs_hyperion_corona, BACKEND);                                                                \
    LOAD_SHADER(fs_hyperion_blur_solar, BACKEND);                                                            \
    LOAD_SHADER(fs_hyperion, BACKEND);                                                                       \
    LOAD_SHADER(fs_cockpit_glazing, BACKEND);                                                                \
    LOAD_SHADER(fs_xalaria_cabin_composite, BACKEND);                                                       \
    LOAD_SHADER(fs_xalaria_cabin, BACKEND);                                                                 \
    LOAD_SHADER(fs_xalaria_stellar_scope, BACKEND);                                                          \
    LOAD_SHADER(fs_xalaria_wormhole, BACKEND);                                                               \
    LOAD_SHADER(fs_hyperion_flight, BACKEND);                                                                \
    LOAD_SHADER(fs_hyperion_flight_post, BACKEND);                                                           \
    LOAD_SHADER(fs_hyperion_weather_post, BACKEND);                                                          \
    LOAD_SHADER(fs_hyperion_polar, BACKEND);                                                                 \
    LOAD_SHADER(fs_hyperion_sky, BACKEND);                                                                   \
    LOAD_SHADER(fs_hyperion_glow, BACKEND);                                                                  \
    LOAD_SHADER(vs_hyperion_screen, BACKEND);                                                                \
    LOAD_SHADER(fs_hyperion_blur, BACKEND);                                                                  \
    LOAD_SHADER(fs_hyperion_lens_seed, BACKEND);                                                            \
    LOAD_SHADER(fs_hyperion_bokeh, BACKEND);                                                                \
    LOAD_SHADER(fs_hyperion_lens, BACKEND);                                                                 \
    LOAD_SHADER(fs_hyperion_present, BACKEND);                                                              \
    LOAD_SHADER(fs_hyperion_post, BACKEND)
    switch (bgfx::getRendererType()) {
    case bgfx::RendererType::Metal: {
        LOAD_BACKEND(metal);
        break;
    }
    case bgfx::RendererType::Vulkan: {
        LOAD_BACKEND(spirv);
        break;
    }
    case bgfx::RendererType::OpenGL: {
        LOAD_BACKEND(glsl);
        break;
    }
#if defined(_WIN32)
    case bgfx::RendererType::Direct3D11: {
        LOAD_BACKEND(dxbc);
        break;
    }
#endif
    default:
        break;
    }
#undef LOAD_BACKEND
#undef LOAD_SHADER
    throw std::runtime_error("No Hyperion shader for this backend");
}
bgfx::ProgramHandle program(const char* vs, const char* fs) {
    auto result = bgfx::createProgram(shader(vs), shader(fs), true);
    if (!bgfx::isValid(result))
        throw std::runtime_error("Hyperion shader program creation failed");
    return result;
}
constexpr uint64_t sampling = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
} // namespace
struct HyperionRenderer::DetailBake {
    std::atomic<bool> cancel{false}, ready{false};
    MaterialTexture texture;
};
void HyperionRenderer::publishDetail() {
    if (!detailBake_ || !detailBake_->ready.load(std::memory_order_acquire))
        return;
    auto& pixels = detailBake_->texture.pixels;
    if (!pixels.empty()) {
        auto texture = bgfx::createTexture2D(MaterialTexture::size, MaterialTexture::size, true, 1,
                                             bgfx::TextureFormat::RGBA8, 0,
                                             bgfx::copy(pixels.data(), uint32_t(pixels.size())));
        if (bgfx::isValid(texture)) {
            bgfx::destroy(detail_);
            detail_ = texture;
            detailBytes_ = detailUploadBytes_ = pixels.size();
        }
    }
    detailBake_.reset();
}
void HyperionRenderer::init() {
    if (bgfx::isValid(geometry_))
        return;
    detailSampler_ = bgfx::createUniform("s_materialDetail", bgfx::UniformType::Sampler);
    noiseSampler_ = bgfx::createUniform("s_materialNoise", bgfx::UniformType::Sampler);
    const auto noise = materialNoiseTexture();
    noise_ = bgfx::createTexture2D(256, 256, false, 1, bgfx::TextureFormat::RGBA8, 0,
                                   bgfx::copy(noise.data(), uint32_t(noise.size())));
    noiseBytes_ = noise.size();
    starTypeParams_ = bgfx::createUniform("u_starType", bgfx::UniformType::Vec4, 2);
    starMatrix_ = bgfx::createUniform("u_starFromEye", bgfx::UniformType::Mat4);
    stellarParams_ = bgfx::createUniform("u_stellar", bgfx::UniformType::Vec4, 3);
    landingParams_ = bgfx::createUniform("u_landingContact", bgfx::UniformType::Vec4, 2);
    weatherVisualParams_ = bgfx::createUniform("u_weatherVisual", bgfx::UniformType::Vec4, 3);
    weatherParams_ = bgfx::createUniform("u_weather", bgfx::UniformType::Vec4, 4);
    weatherMatrix_ = bgfx::createUniform("u_weatherFromEye", bgfx::UniformType::Mat4);
    weatherSampler_ = bgfx::createUniform("s_weatherField", bgfx::UniformType::Sampler);
    weatherNextSampler_ = bgfx::createUniform("s_weatherNext", bgfx::UniformType::Sampler);
    cloudSampler_ = bgfx::createUniform("s_cloudVolume", bgfx::UniformType::Sampler);
    detailUploadBytes_ += noiseBytes_;
    environment_ = bgfx::createUniform("u_environment", bgfx::UniformType::Vec4);
    planetParams_ = bgfx::createUniform("u_planet", bgfx::UniformType::Vec4, 5);
    planetRingsParams_ = bgfx::createUniform("u_planetRings", bgfx::UniformType::Vec4, 2);
    ringParams_ = bgfx::createUniform("u_rings", bgfx::UniformType::Vec4, 4);
    surface_ = bgfx::createUniform("u_surface", bgfx::UniformType::Vec4);
    terrainMaterial_ = bgfx::createUniform("u_terrainMaterial", bgfx::UniformType::Vec4, 4);
    const uint8_t average[] = {128, 0, 128, 128};
    detail_ = bgfx::createTexture2D(1, 1, false, 1, bgfx::TextureFormat::RGBA8, 0,
                                    bgfx::copy(average, sizeof(average)));
    auto work = detailBake_ = std::make_shared<DetailBake>();
    detailTasks_.run([work] {
        try {
            work->texture = bakeMaterialTexture(&work->cancel);
        } catch (...) {
            work->cancel.store(true);
        }
        work->ready.store(true, std::memory_order_release);
    });
    geometry_ = program("vs_hyperion", "fs_hyperion");
    glow_ = program("vs_hyperion", "fs_hyperion_glow");
    blur_ = program("vs_hyperion_screen", "fs_hyperion_blur");
    post_ = program("vs_hyperion_screen", "fs_hyperion_post");
    sceneSampler_ = bgfx::createUniform("s_scene", bgfx::UniformType::Sampler);
    ringSampler_ = bgfx::createUniform("s_ringLayer", bgfx::UniformType::Sampler);
    shipTrailSampler_ = bgfx::createUniform("s_shipTrail", bgfx::UniformType::Sampler);
    shipBlastSampler_ = bgfx::createUniform("s_shipBlast", bgfx::UniformType::Sampler);
    shipBlastParams_ = bgfx::createUniform("u_shipBlastParams", bgfx::UniformType::Vec4);
    wormholeParams_ = bgfx::createUniform("u_wormholeOptics", bgfx::UniformType::Vec4, 5);
    glazingParams_ = bgfx::createUniform("u_glazing", bgfx::UniformType::Vec4);
    cabinSunParams_=bgfx::createUniform("u_cabinSun",bgfx::UniformType::Vec4,2);
    cabinFromEyeParams_=bgfx::createUniform("u_cabinFromEye",bgfx::UniformType::Mat4);
    bloomSampler_ = bgfx::createUniform("s_bloom", bgfx::UniformType::Sampler);
    depthSampler_ = bgfx::createUniform("s_opaqueDepth", bgfx::UniformType::Sampler);
    reflectionSampler_=bgfx::createUniform("s_reflectionSurface",bgfx::UniformType::Sampler);
    reflectionParams_=bgfx::createUniform("u_reflection",bgfx::UniformType::Vec4,2);
    spaceGlassParams_=bgfx::createUniform("u_spaceGlass",bgfx::UniformType::Vec4);
    regionLook_ = bgfx::createUniform("u_regionLook", bgfx::UniformType::Vec4, 2);
    stationWindows_ = bgfx::createUniform("u_stationWindows", bgfx::UniformType::Vec4,
                                         uint16_t(std::tuple_size_v<StationWindowLighting>));
    stationAnnexes_ = bgfx::createUniform("u_stationAnnexes", bgfx::UniformType::Vec4,
                                        uint16_t(std::tuple_size_v<StationAnnexLighting>));
    stationDoors_ = bgfx::createUniform("u_stationDoors", bgfx::UniformType::Vec4, 2);
    spaceGlassMatrix_=bgfx::createUniform("u_spaceGlassRotation",bgfx::UniformType::Mat4);
    atmosphere_ = bgfx::createUniform("u_atmosphere", bgfx::UniformType::Vec4, 4);
    polar_ = bgfx::createUniform("u_polar", bgfx::UniformType::Vec4, 2);
    polarSampler_ = bgfx::createUniform("s_polarLight", bgfx::UniformType::Sampler);
    atmosphereRotation_ = bgfx::createUniform("u_atmosphereRotation", bgfx::UniformType::Mat4);
    style_ = bgfx::createUniform("u_hyperion", bgfx::UniformType::Vec4, 4);
    pattern_ = bgfx::createUniform("u_pattern", bgfx::UniformType::Mat4);
    cavity_ = bgfx::createUniform("u_cavity", bgfx::UniformType::Vec4);
    meso_ = bgfx::createUniform("u_meso", bgfx::UniformType::Mat4);
    micro_ = bgfx::createUniform("u_micro", bgfx::UniformType::Mat4);
    microParams_ = bgfx::createUniform("u_microParams", bgfx::UniformType::Vec4);
    domain_ = bgfx::createUniform("u_materialDomain", bgfx::UniformType::Vec4);
    skyParams_ = bgfx::createUniform("u_sky", bgfx::UniformType::Vec4, 2);
    skyRotation_ = bgfx::createUniform("u_skyRotation", bgfx::UniformType::Mat4);
    postParams_ = bgfx::createUniform("u_post", bgfx::UniformType::Vec4, 2);
    lensParams_=bgfx::createUniform("u_lens",bgfx::UniformType::Vec4,2);
    focusSeedSampler_=bgfx::createUniform("s_focusSeed",bgfx::UniformType::Sampler);
    lensSeedProgram_=program("vs_hyperion_screen","fs_hyperion_lens_seed");
    bokehProgram_=program("vs_hyperion_screen","fs_hyperion_bokeh");
    lensProgram_=program("vs_hyperion_screen","fs_hyperion_lens");
    presentProgram_=program("vs_hyperion_screen","fs_hyperion_present");
    sunSampler_ = bgfx::createUniform("s_sunShadow", bgfx::UniformType::Sampler);
    sunMatrix_ = bgfx::createUniform("u_sunFromWorld", bgfx::UniformType::Mat4);
    sunParams_ = bgfx::createUniform("u_sunShadowParams", bgfx::UniformType::Vec4);
    screenLayout_.begin()
        .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
        .end();
}
void HyperionRenderer::clearTargets() {
    if (bgfx::isValid(ringLayer_))
        bgfx::destroy(ringLayer_);
    if (bgfx::isValid(ringColour_))
        bgfx::destroy(ringColour_);
    ringLayer_ = BGFX_INVALID_HANDLE;
    ringColour_ = BGFX_INVALID_HANDLE;
    if (bgfx::isValid(shipTrailLayer_))
        bgfx::destroy(shipTrailLayer_);
    if (bgfx::isValid(shipTrailMask_))
        bgfx::destroy(shipTrailMask_);
    shipTrailLayer_ = BGFX_INVALID_HANDLE;
    shipTrailMask_ = BGFX_INVALID_HANDLE;
    if(bgfx::isValid(shipBlastLayer_))bgfx::destroy(shipBlastLayer_);
    if(bgfx::isValid(shipBlastColour_))bgfx::destroy(shipBlastColour_);
    shipBlastLayer_=BGFX_INVALID_HANDLE;shipBlastColour_=BGFX_INVALID_HANDLE;
    if (bgfx::isValid(scene_))
        bgfx::destroy(scene_);
    scene_ = BGFX_INVALID_HANDLE;
    for(auto* target:{&optical_,&lensSeed_,&bokeh_,&display_,&cabin_}){
        if(bgfx::isValid(*target))bgfx::destroy(*target);
        *target=BGFX_INVALID_HANDLE;
    }
    for (auto& target : bloom_) {
        if (bgfx::isValid(target))
            bgfx::destroy(target);
        target = BGFX_INVALID_HANDLE;
    }
    if (bgfx::isValid(polarTarget_))
        bgfx::destroy(polarTarget_);
    polarTarget_ = BGFX_INVALID_HANDLE;
    if (bgfx::isValid(cloudTarget_))
        bgfx::destroy(cloudTarget_);
    cloudTarget_ = BGFX_INVALID_HANDLE;
    bytes_ = 0;
    width_ = height_ = 0;
}
HyperionRenderer::~HyperionRenderer() {
    if(bgfx::isValid(cabinSunParams_))bgfx::destroy(cabinSunParams_);
    if(bgfx::isValid(cabinFromEyeParams_))bgfx::destroy(cabinFromEyeParams_);
    if(bgfx::isValid(cabinGeometry_))bgfx::destroy(cabinGeometry_);
    if (detailBake_)
        detailBake_->cancel.store(true);
    detailTasks_.wait();
    if (bgfx::isValid(detail_))
        bgfx::destroy(detail_);
    if (bgfx::isValid(atlasTextTexture_))
        bgfx::destroy(atlasTextTexture_);
    if (bgfx::isValid(atlasTextProgram_))
        bgfx::destroy(atlasTextProgram_);
    if (bgfx::isValid(atlasTextSampler_))
        bgfx::destroy(atlasTextSampler_);
    if (bgfx::isValid(deckCrewTexture_))
        bgfx::destroy(deckCrewTexture_);
    if (bgfx::isValid(deckCrewProgram_))
        bgfx::destroy(deckCrewProgram_);
    if (bgfx::isValid(deckCrewSampler_))
        bgfx::destroy(deckCrewSampler_);
    clearTargets();
    if (bgfx::isValid(noise_))
        bgfx::destroy(noise_);
    for (auto t : weatherTextures_)
        if (bgfx::isValid(t))
            bgfx::destroy(t);
    if (bgfx::isValid(sunTarget_))
        bgfx::destroy(sunTarget_);
    for (auto p : {lensSeedProgram_,bokehProgram_,lensProgram_,presentProgram_,geometry_,cockpitGlazing_,cabinComposite_,wormhole_,stellarScope_,
                   glow_,
                   blur_,
                   post_,
                   flightGeometry_,
                   flightPost_,
                   sky_,
                   smoothGeometry_[0],
                   smoothGeometry_[1],
                   sunProgram_,
                   cloudProgram_,
                   coverageProgram_,
                   polarProgram_,
                   cloudVolumeProgram_,
                   corona_,
                   solarBlur_,
                   stellarGeometry_[0],
                   stellarGeometry_[1],
                   weatherPost_,
                   rings_})
        if (bgfx::isValid(p))
            bgfx::destroy(p);
    for (auto u : {lensParams_,glazingParams_,wormholeParams_,focusSeedSampler_,planetParams_,   planetRingsParams_,
                   ringParams_,     ringSampler_,     shipTrailSampler_, shipBlastSampler_, shipBlastParams_, weatherVisualParams_,
                   landingParams_,  stellarParams_,
                   starTypeParams_, starMatrix_,
                   weatherParams_,  weatherMatrix_,
                   weatherSampler_, weatherNextSampler_,
                   cloudSampler_,   polarSampler_,
                   polar_,          environment_,
                   noiseSampler_,   detailSampler_,
                   surface_,        sceneSampler_,
                   bloomSampler_,   style_,
                   pattern_,        postParams_,
                   reflectionSampler_,reflectionParams_,spaceGlassParams_,spaceGlassMatrix_,regionLook_,
                   cavity_,         depthSampler_, stationWindows_, stationAnnexes_, stationDoors_,
                   atmosphere_,     atmosphereRotation_,
                   micro_,          microParams_, terrainMaterial_,
                   meso_,           domain_,
                   skyParams_,      skyRotation_,
                   sunSampler_,     sunMatrix_,
                   sunParams_})
        if (bgfx::isValid(u))
            bgfx::destroy(u);
}
bgfx::ProgramHandle HyperionRenderer::coronaProgram() {
    init();
    if (!bgfx::isValid(corona_))
        corona_ = program("vs_hyperion", "fs_hyperion_corona");
    return corona_;
}
bgfx::ProgramHandle HyperionRenderer::geometryProgram(bool smooth, bool stellar) {
    init();
    if (stellar) {
        auto& handle = stellarGeometry_[smooth ? 1 : 0];
        if (!bgfx::isValid(handle))
            handle = program(smooth ? "vs_hyperion_smooth" : "vs_hyperion", "fs_hyperion_stellar");
        return handle;
    }
    if (smooth) {
        auto& handle = smoothGeometry_[reflectiveTargets_ ? 1 : 0];
        if (!bgfx::isValid(handle))
            handle = program("vs_hyperion_smooth", reflectiveTargets_ ? "fs_hyperion_flight" : "fs_hyperion");
        return handle;
    }
    if (!reflectiveTargets_)
        return geometry_;
    if (!bgfx::isValid(flightGeometry_))
        flightGeometry_ = program("vs_hyperion", "fs_hyperion_flight");
    return flightGeometry_;
}
void HyperionRenderer::setRegionalLook(glm::vec3 look, double seconds, double logMetres, bool exterior) {
    const float charred = std::max(0.f, 1.f - look.x - look.y - look.z);
    regionLookData_ = {{
        {look.x, float(seconds), float(logMetres), exterior ? 1.f : 0.f},
        {look.y, charred, look.z, 0.f}
    }};
}
bgfx::ProgramHandle HyperionRenderer::cockpitGlazingProgram() {
    init();
    if (!bgfx::isValid(cockpitGlazing_))
        cockpitGlazing_ = program("vs_hyperion", "fs_cockpit_glazing");
    return cockpitGlazing_;
}

void HyperionRenderer::bindCockpitGlazing(GlazingView view) {
    const glm::vec4 glazing{1.f / width_, 1.f / height_, bgfx::getCaps()->originBottomLeft ? 1.f : -1.f, float(view)};
    bgfx::setUniform(glazingParams_, glm::value_ptr(glazing));
    bgfx::setTexture(0, sceneSampler_,
                     view==GlazingView::TunnelExterior?noise_:bgfx::getTexture(display_),sampling);
    bgfx::setTexture(2, depthSampler_, view==GlazingView::TunnelInterior && readableDepth_
                         ? bgfx::getTexture(scene_,depthAttachment_) : noise_,
                     sampling | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT);
}
void HyperionRenderer::bindStationLighting(std::span<const glm::vec4> openings,std::span<const glm::vec4> annexes,
                                          const std::array<glm::vec4,2>& doors) {
    bgfx::setUniform(cabinSunParams_,cabinSunData_.data(),2);
    bgfx::setUniform(stationWindows_,openings.data(),uint16_t(openings.size()));
    bgfx::setUniform(stationAnnexes_,annexes.data(),uint16_t(annexes.size()));
    bgfx::setUniform(stationDoors_,doors.data(),uint16_t(doors.size()));
}
void HyperionRenderer::drawAtlasText(std::span<const AtlasGlyphQuad> glyphs, bgfx::ViewId view,
                                     const glm::mat4* model,float pixelsPerUnit) {
    if (glyphs.empty())
        return;
    if (!bgfx::isValid(atlasTextProgram_)) {
        std::vector<uint8_t> pixels;
        pixels.reserve(atlasFontData::width * atlasFontData::height);
        for (const auto run : atlasFontData::coverageRuns)
            pixels.insert(pixels.end(), (run & 255) + 1, uint8_t(run >> 8));
        atlasTextTexture_ = bgfx::createTexture2D(atlasFontData::width, atlasFontData::height, false, 1,
            bgfx::TextureFormat::R8, sampling, bgfx::copy(pixels.data(), uint32_t(pixels.size())));
        atlasTextSampler_ = bgfx::createUniform("s_atlasText", bgfx::UniformType::Sampler);
        atlasTextProgram_ = program("vs_xalaria_atlas", "fs_xalaria_atlas");
        atlasTextLayout_.begin()
            .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
            .end();
    }
    struct Vertex { float x, y, z, u, v; uint32_t colour; };
    bgfx::TransientVertexBuffer buffer;
    const auto count = uint32_t(glyphs.size() * 6);
    if (bgfx::getAvailTransientVertexBuffer(count, atlasTextLayout_) < count)
        return;
    bgfx::allocTransientVertexBuffer(&buffer, count, atlasTextLayout_);
    auto* vertices = reinterpret_cast<Vertex*>(buffer.data);
    for (const auto& g : glyphs) {
        // Distant panels stay in the world. Their text fades before it
        // becomes a pattern of flickering subpixel strokes.
        const float opacity=pixelsPerUnit>0.f?glm::smoothstep(2.5f,6.f,(g.y1-g.y0)*pixelsPerUnit):1.f;
        const uint32_t colour=(g.colour&0xffffffu)|(uint32_t(float(g.colour>>24)*opacity+.5f)<<24);
        const Vertex a{g.x0,g.y0,0,g.u0,g.v0,colour}, b{g.x1,g.y0,0,g.u1,g.v0,colour};
        const Vertex c{g.x0,g.y1,0,g.u0,g.v1,colour}, d{g.x1,g.y1,0,g.u1,g.v1,colour};
        for (const auto& point : {a,b,c,b,d,c})
            *vertices++ = point;
    }
    bgfx::setVertexBuffer(0, &buffer);
    bgfx::setTexture(0, atlasTextSampler_, atlasTextTexture_, sampling);
    if (model)
        bgfx::setTransform(glm::value_ptr(*model));
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_BLEND_ALPHA | BGFX_STATE_MSAA |
                   (model ? BGFX_STATE_DEPTH_TEST_LESS : 0));
    bgfx::submit(view, atlasTextProgram_);
}
void HyperionRenderer::prepareDeckCrew() {
    if (bgfx::isValid(deckCrewProgram_))
        return;
    constexpr unsigned atlasWidth=crewSpriteWidth*crewSpriteFrames;
    constexpr unsigned atlasHeight=crewSpriteHeight*crewSpriteDirections;
    const auto pixels=crewSilhouettePixels();
    deckCrewTexture_=bgfx::createTexture2D(atlasWidth,atlasHeight,false,1,bgfx::TextureFormat::R8,
        sampling,bgfx::copy(pixels.data(),uint32_t(pixels.size())));
    deckCrewSampler_=bgfx::createUniform("s_deckCrew",bgfx::UniformType::Sampler);
    deckCrewProgram_=program("vs_xalaria_crew","fs_xalaria_crew");
    atlasTextLayout_.begin()
        .add(bgfx::Attrib::Position,3,bgfx::AttribType::Float)
        .add(bgfx::Attrib::TexCoord0,2,bgfx::AttribType::Float)
        .add(bgfx::Attrib::Color0,4,bgfx::AttribType::Uint8,true)
        .end();
}
void HyperionRenderer::drawDeckCrew(std::span<const DeckCrewPose> crew,glm::dvec3 eye,bgfx::ViewId view) {
    if (crew.empty() || !bgfx::isValid(deckCrewProgram_) || !bgfx::isValid(deckCrewTexture_))
        return;
    constexpr unsigned atlasWidth=crewSpriteWidth*crewSpriteFrames;
    constexpr unsigned atlasHeight=crewSpriteHeight*crewSpriteDirections;
    struct Vertex { float x,y,z,u,v; uint32_t colour; };
    const auto count=uint32_t(crew.size()*6);
    if (bgfx::getAvailTransientVertexBuffer(count,atlasTextLayout_)<count)
        return;
    bgfx::TransientVertexBuffer buffer;
    bgfx::allocTransientVertexBuffer(&buffer,count,atlasTextLayout_);
    auto* out=reinterpret_cast<Vertex*>(buffer.data);
    const glm::dvec2 camera(eye.x,eye.z);
    std::vector<const DeckCrewPose*> order;
    order.reserve(crew.size());
    for(const auto& pose:crew)order.push_back(&pose);
    // Paint the more distant figure first if two antialiased contours overlap.
    std::sort(order.begin(),order.end(),[&](const auto* a,const auto* b) {
        const auto da=a->position-camera,db=b->position-camera;
        return glm::dot(da,da)>glm::dot(db,db);
    });
    for (const auto* figure : order) {
        const auto& member=*figure;
        const glm::dvec2 delta=camera-member.position;
        const auto facing=std::atan2(delta.x,delta.y)-member.yaw;
        const auto dir=unsigned((int(std::round(facing*crewSpriteDirections/(2*std::numbers::pi)))%int(crewSpriteDirections)+
                                 int(crewSpriteDirections))%int(crewSpriteDirections));
        const unsigned frame=member.moving ? std::min(7u,unsigned(member.stride*8.)) :
            8u+unsigned(std::floor(member.taskPhase*4/(2*std::numbers::pi)))%4;
        const auto tangent=glm::normalize(glm::dvec2(delta.y,-delta.x)+glm::dvec2(1e-8,0));
        const double texel=member.height/(1.78*crewSpriteTexelsPerMetre);
        const double halfWidth=crewSpriteWidth*texel*.5;
        const auto right=member.position+tangent*halfWidth, left=member.position-tangent*halfWidth;
        const float u0=(frame*crewSpriteWidth+.5f)/atlasWidth,u1=((frame+1)*crewSpriteWidth-.5f)/atlasWidth;
        const float v0=(dir*crewSpriteHeight+.5f)/atlasHeight,v1=((dir+1)*crewSpriteHeight-.5f)/atlasHeight;
        const float bottom=float(shipDeck::deckFloor-crewSpriteGroundMargin*texel);
        const float top=bottom+float(crewSpriteHeight*texel);
        const auto colour=rgba(member.rim);
        const Vertex a{float(left.x),top,float(left.y),u0,v0,colour};
        const Vertex b{float(right.x),top,float(right.y),u1,v0,colour};
        const Vertex c{float(left.x),bottom,float(left.y),u0,v1,colour};
        const Vertex d{float(right.x),bottom,float(right.y),u1,v1,colour};
        for (const auto& v : {a,b,c,b,d,c})
            *out++=v;
    }
    bgfx::setVertexBuffer(0,&buffer);
    bgfx::setTexture(0,deckCrewSampler_,deckCrewTexture_,sampling);
    bgfx::setState(BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A|BGFX_STATE_WRITE_Z|BGFX_STATE_DEPTH_TEST_LESS|BGFX_STATE_MSAA|
                   BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE,BGFX_STATE_BLEND_INV_SRC_ALPHA));
    bgfx::submit(view,deckCrewProgram_);
}
bgfx::ProgramHandle HyperionRenderer::wormholeProgram() {
    init();
    if (!bgfx::isValid(wormhole_))
        wormhole_ = program("vs_hyperion", "fs_xalaria_wormhole");
    return wormhole_;
}
bool HyperionRenderer::wormholeOptics(glm::vec3 centre, glm::vec3 right, glm::vec3 up,
                                     float radius, glm::vec3 plasma, float tanHalfFov, double seconds) {
    if (!readableDepth_)
        return false;
    wormholeData_[0] = {centre, radius};
    wormholeData_[1] = {right, width_ * tanHalfFov / height_};
    wormholeData_[2] = {up, tanHalfFov * (bgfx::getCaps()->originBottomLeft ? 1.f : -1.f)};
    wormholeData_[3] = {plasma, float(seconds)};
    wormholeData_[4] = {glm::cross(right, up), 0.f};
    return true;
}
void HyperionRenderer::begin(RenderMode, int width, int height, bool opticalDepth, bool flight) {
    detailUploadBytes_ = 0;
    ringLayerActive_ = false;
    shipTrailsActive_ = false;
    shipBlastsActive_ = false;
    flight_ = flight;
    spaceGlassData_={};spaceGlassFromEye_=glm::mat4(1);
    weatherVisualData_ = {};
    landingData_ = {};
    landingPlanet_.reset();
    stellarData_ = {};
    starTypeData_ = {};
    starFromEye_ = glm::mat4(1);
    atmosphereData_ = {};
    wormholeData_ = {};
    polarData_ = {};
    weatherData_ = {};
    weatherPending_ = false;
    weatherCache_.beginFrame();
    sunActive_ = false;
    sunUpdated_ = false;
    init();
    publishDetail();
    const bool mirrors=flight&&opticalDepth&&bgfx::getCaps()->limits.maxFBAttachments>=3;
    const bool replaceTarget = reflectiveTargets_!=mirrors || width_ != width || height_ != height || readableDepth_ != opticalDepth;
    if (replaceTarget) {
        clearTargets();
        readableDepth_ = opticalDepth;reflectiveTargets_=mirrors;depthAttachment_=mirrors?2:1;
        width_ = width;
        height_ = height;
        auto color = bgfx::createTexture2D(width, height, false, 1, bgfx::TextureFormat::RGBA16F,
                                           BGFX_TEXTURE_RT | sampling);
        auto depth =
            bgfx::createTexture2D(width, height, false, 1, bgfx::TextureFormat::D32F,
                                  opticalDepth ? BGFX_TEXTURE_RT | sampling : BGFX_TEXTURE_RT_WRITE_ONLY);
        if(mirrors){
            auto surface=bgfx::createTexture2D(width,height,false,1,bgfx::TextureFormat::RGBA16F,BGFX_TEXTURE_RT|sampling);
            bgfx::TextureHandle attachments[]={color,surface,depth};
            scene_=bgfx::createFrameBuffer(3,attachments,true);
        }else{
            bgfx::TextureHandle attachments[]={color,depth};
            scene_=bgfx::createFrameBuffer(2,attachments,true);
        }
        for (auto& target : bloom_)
            target = bgfx::createFrameBuffer(std::max(1, width / 3), std::max(1, height / 3),
                                             bgfx::TextureFormat::RGBA16F, BGFX_TEXTURE_RT | sampling);
        optical_=bgfx::createFrameBuffer(width,height,bgfx::TextureFormat::RGBA16F,BGFX_TEXTURE_RT|sampling);
        display_=bgfx::createFrameBuffer(width,height,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_RT|sampling);
        const bool filteredLensSeed=(bgfx::getCaps()->formats[bgfx::TextureFormat::RGBA16F]&
                                    BGFX_CAPS_FORMAT_TEXTURE_MIP_AUTOGEN)!=0;
        const auto seed=bgfx::createTexture2D(std::max(1,width/3),std::max(1,height/3),
                                             filteredLensSeed,1,bgfx::TextureFormat::RGBA16F,
                                             BGFX_TEXTURE_RT|sampling);
        bgfx::Attachment seedAttachment;
        seedAttachment.init(seed);
        lensSeed_=bgfx::createFrameBuffer(1,&seedAttachment,true);
        bokeh_=bgfx::createFrameBuffer(std::max(1,width/3),std::max(1,height/3),bgfx::TextureFormat::RGBA16F,BGFX_TEXTURE_RT|sampling);
        if(!bgfx::isValid(optical_)||!bgfx::isValid(display_)||!bgfx::isValid(lensSeed_)||!bgfx::isValid(bokeh_))
            throw std::runtime_error("Camera lens target allocation failed");
        if (!bgfx::isValid(scene_) || !bgfx::isValid(bloom_[0]) || !bgfx::isValid(bloom_[1]))
            throw std::runtime_error("Hyperion HDR target allocation failed");
        bytes_ = size_t(width) * height * (mirrors?20:12) + size_t(std::max(1, width / 3)) * std::max(1, height / 3) * 16;
        bytes_+=size_t(width)*height*12+size_t(std::max(1,width/3))*std::max(1,height/3)*16;
        if(filteredLensSeed){
            int w=std::max(1,width/3),h=std::max(1,height/3);
            while(w>1||h>1){w=std::max(1,w/2);h=std::max(1,h/2);bytes_+=size_t(w)*h*8;}
        }
        if (opticalDepth) {
            polarTarget_ = bgfx::createFrameBuffer(std::max(1, width / 2), std::max(1, height / 2),
                                                   bgfx::TextureFormat::RGBA16F, BGFX_TEXTURE_RT | sampling);
            if (!bgfx::isValid(polarTarget_))
                throw std::runtime_error("Polar optical target allocation failed");
            bytes_ += size_t(std::max(1, width / 2)) * std::max(1, height / 2) * 8;
            const auto size = cloudBufferSize(width, height);
            cloudWidth_ = size.x;
            cloudHeight_ = size.y;
            cloudTarget_ = bgfx::createFrameBuffer(cloudWidth_, cloudHeight_, bgfx::TextureFormat::RGBA16F,
                                                   BGFX_TEXTURE_RT | sampling);
            if (!bgfx::isValid(cloudTarget_))
                throw std::runtime_error("Cloud optical target allocation failed");
            bytes_ += size_t(cloudWidth_) * cloudHeight_ * 8;
        }
    }
    if (replaceTarget) {
        for (bgfx::ViewId id = 0; id < 67; ++id)
            bgfx::setViewFrameBuffer(id, scene_);
        bgfx::setViewFrameBuffer(stationGlassViewId,scene_);
        bgfx::setViewFrameBuffer(warpFieldViewId,scene_);
    }
    // Hyperion skyFragment's linear ink (.004,.007,.016), rounded to the
    // clear API's RGBA8 input; the HDR display transform is applied later.
    bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x010204ff, 0.f, 0);
}
bgfx::ProgramHandle HyperionRenderer::sunProgram() {
    if (!bgfx::isValid(sunProgram_))
        sunProgram_ = program("vs_hyperion_shadow", "fs_hyperion_shadow");
    return sunProgram_;
}
bgfx::ProgramHandle HyperionRenderer::cloudProgram() {
    if (!bgfx::isValid(cloudProgram_))
        cloudProgram_ = program("vs_hyperion", "fs_hyperion_cloud");
    return cloudProgram_;
}
bgfx::ProgramHandle HyperionRenderer::coverageProgram() {
    if (!bgfx::isValid(coverageProgram_))
        coverageProgram_ = program("vs_hyperion_shadow", "fs_hyperion_coverage");
    return coverageProgram_;
}
bool HyperionRenderer::prepareSun(const SunShadowFrame& frame, uint64_t key) {
    if (!readableDepth_)
        return false;
    if (!bgfx::isValid(sunTarget_)) {
        if (!bgfx::isTextureValid(0, false, 1, bgfx::TextureFormat::D16, BGFX_TEXTURE_RT))
            return false;
        auto texture = bgfx::createTexture2D(sunShadowResolution, sunShadowResolution, false, 1,
                                             bgfx::TextureFormat::D16, BGFX_TEXTURE_RT | sampling);
        sunTarget_ = bgfx::createFrameBuffer(1, &texture, true);
        if (!bgfx::isValid(sunTarget_))
            throw std::runtime_error("Sun shadow target allocation failed");
        sunBytes_ = size_t(sunShadowResolution) * sunShadowResolution * 2;
    }
    sunActive_ = true;
    sunFromWorld_ = glm::mat4(frame.textureFromWorld);
    const bool redraw = !sunValid_ || sunKey_ != key || sunSpan_ != frame.spanMetres ||
                        glm::length(frame.anchorPlanet - sunAnchor_) > .001 ||
                        glm::length(frame.lightPlanet - sunLight_) > 1e-7;
    if (!redraw)
        return false;
    sunUpdated_ = true;
    sunKey_ = key;
    sunAnchor_ = frame.anchorPlanet;
    sunLight_ = frame.lightPlanet;
    sunSpan_ = frame.spanMetres;
    sunValid_ = true;
    const glm::mat4 view(frame.view), projection(frame.projection);
    bgfx::setViewFrameBuffer(70, sunTarget_);
    bgfx::setViewRect(70, 0, 0, sunShadowResolution, sunShadowResolution);
    bgfx::setViewTransform(70, glm::value_ptr(view), glm::value_ptr(projection));
    bgfx::setViewClear(70, BGFX_CLEAR_DEPTH, 0, 1.f, 0);
    bgfx::touch(70);
    return true;
}
void HyperionRenderer::atmosphere(glm::vec3 eye, glm::vec3 light, const glm::mat4& rotation, float radius,
                                  float height, float strength, float aspect, float tanHalfFov, float phase,
                                  float radiusKm, const PolarLight& polar, double seconds) {
    atmosphereData_[0] = glm::vec4(eye, strength);
    atmosphereData_[1] = glm::vec4(light, height);
    atmosphereData_[2] = {radius, float(worldNearPlane), float(worldFarPlane), phase};
    atmosphereData_[3] = {aspect * tanHalfFov, tanHalfFov, bgfx::getCaps()->originBottomLeft ? 1.f : -1.f,
                          radiusKm};
    atmosphereMatrix_ = rotation;
    polarData_[0] = glm::vec4(polar.axis, polar.strength);
    polarData_[1] = {float(polar.innerRadius), float(polar.outerRadius), float(seconds), 0};
}
void HyperionRenderer::landingContact(const Address& planet, glm::vec3 point, glm::vec3 up, float radius,
                                      float strength) {
    landingPlanet_ = planet;
    landingData_[0] = glm::vec4(point, radius);
    landingData_[1] = glm::vec4(up, strength);
}
void HyperionRenderer::localWeather(const PlanetClimate& c, const WeatherSample& sample, double seconds,
                                    bool enabled) {
    weatherVisualData_ = {};
    if (!enabled || c.pressureBar == 0)
        return;
    const float discharge = float(weatherDischarge(c, seconds));
    const bool dust = sample.type == WeatherType::Dust;
    const float frozen = float(1 - smoothstep(268., 278., sample.temperatureK));
    const auto tint = dust ? glm::vec3(.24, .115, .045)
                           : glm::mix(glm::vec3(.055, .09, .14), glm::vec3(.24, .32, .40), frozen);
    // Crossing the cloud base or a weak shower must not switch a finite
    // optical depth on in one frame. Phase tint also blends across freezing.
    const float obscured = float(smoothstep(.02, .12, sample.precipitation));
    const auto layers = weatherLayers(c);
    weatherVisualData_[0] = {obscured * float(2.4 * c.radiusMetres / sample.visibilityMetres),
                             float(layers.hazeHeightMetres / c.radiusMetres), dust ? 1.f : 0.f, 1};
    weatherVisualData_[1] = glm::vec4(tint, discharge * float(std::pow(sample.storm, 4)));
    weatherVisualData_[2] = {discharge, float(sample.storm), float(sample.precipitation),
                             dust ? float(sample.dust) : 0.f};
}
void HyperionRenderer::stellar(const StellarImage& image, float aspect, float tanHalfFov, double seconds) {
    if (!readableDepth_ || image.visibility <= 0 || image.direction.z >= -.0001)
        return;
    const auto d = image.direction;
    const float x = float(.5 + .5 * d.x / (-d.z * tanHalfFov * aspect));
    const float y = float(.5 + .5 * d.y / (-d.z * tanHalfFov) * (bgfx::getCaps()->originBottomLeft ? 1 : -1));
    const float edge = std::min({x, y, 1 - x, 1 - y});
    const float fade = glm::smoothstep(-.06f, .015f, edge);
    if (fade <= 0)
        return;
    const double z = std::max(worldNearPlane, image.nearDepthUnits);
    const float depth =
        float(std::max(0., worldNearPlane * worldFarPlane / (z * (worldFarPlane - worldNearPlane)) -
                               worldNearPlane / (worldFarPlane - worldNearPlane)));
    stellarData_[0] = {x, y, float(std::tan(image.angularRadius) / (2 * tanHalfFov)),
                       image.visibility * fade};
    stellarData_[1] = glm::vec4(image.color, depth);
    starTypeData_ = image.starType;
    starTypeData_[1].w = float(seconds);
    starFromEye_ = glm::mat4_cast(glm::quat(image.cameraToStar));
    starFromEye_[3][3] = bgfx::getCaps()->originBottomLeft ? 1.f : -1.f;
    const float flux = std::clamp(float(image.angularRadius / .0015), 0.f, 1.f);
    stellarData_[2] = {aspect, flux * flux, 1.f / height_, 0};
}
void HyperionRenderer::showStellarScope(const Node& star,double seconds) {
    if(!bgfx::isValid(stellarScope_))
        stellarScope_=program("vs_hyperion_screen","fs_xalaria_stellar_scope");
    auto typed=stellarPresentation(star);
    typed[1].w=float(seconds);
    const std::array<glm::vec4,3> optics{{
        {float(width_)/height_,1.f/height_,0,0},
        {stellarRadiance(star),0}, {}
    }};
    bgfx::setUniform(starTypeParams_,typed.data(),2);
    bgfx::setUniform(stellarParams_,optics.data(),3);
    bgfx::setTexture(5,noiseSampler_,noise_);
    bgfx::setViewFrameBuffer(86,BGFX_INVALID_HANDLE);
    bgfx::setViewRect(86,0,0,width_,height_);
    screen(86,stellarScope_);
}
void HyperionRenderer::prepareWeather(const Node& planet, const glm::dmat4& mapping, double seconds,
                                      double delta, bool volumes, bool frozen) {
    const auto climate = planetClimate(planet);
    if (!readableDepth_ || climate.pressureBar == 0)
        return;
    if (!weatherPlanet_ || *weatherPlanet_ != planet.address) {
        weatherRamp_ = 0;
        weatherPlanet_ = planet.address;
    }
    const double start = std::floor(seconds / 8.) * 8.;
    std::array<std::shared_ptr<const WeatherField>, 2> fields{weatherCache_.request(planet, start),
                                                              weatherCache_.request(planet, start + 8)};
    const bool ready = fields[0] && fields[1];
    if (!ready) {
        weatherPending_ = true;
        // Keep the last complete weather pair during an asynchronous time
        // boundary. A late worker must not flash the old shell back on screen.
        if (!weatherFields_[0] || !weatherFields_[1] || weatherFields_[0]->planet != planet.address)
            return;
        fields = weatherFields_;
    } else if (seconds - start > 4) {
        // Warm the next pair before its eight-second boundary.
        weatherCache_.request(planet, start + 16);
    }
    auto oldTextures = weatherTextures_;
    auto oldFields = weatherFields_;
    std::array<bool, 2> retained{};
    for (unsigned i = 0; i < 2; ++i) {
        bool found = false;
        for (unsigned j = 0; j < 2; ++j)
            if (fields[i] == oldFields[j]) {
                weatherTextures_[i] = oldTextures[j];
                retained[j] = true;
                found = true;
                break;
            }
        if (!found) {
            weatherTextures_[i] = bgfx::createTexture2D(
                WeatherField::width, WeatherField::height, true, 1, bgfx::TextureFormat::RGBA8,
                BGFX_SAMPLER_V_CLAMP,
                bgfx::copy(fields[i]->pixels.data(), uint32_t(fields[i]->pixels.size())));
            if (!bgfx::isValid(weatherTextures_[i]))
                throw std::runtime_error("Weather atlas allocation failed");
            detailUploadBytes_ += fields[i]->pixels.size();
        }
    }
    for (unsigned j = 0; j < 2; ++j)
        if (!retained[j] && bgfx::isValid(oldTextures[j]))
            bgfx::destroy(oldTextures[j]);
    weatherFields_ = fields;
    weatherGpuBytes_ = fields[0]->pixels.size() + fields[1]->pixels.size();
    weatherRamp_ = frozen ? 1.f : std::min(1.f, weatherRamp_ + float(std::max(0., delta) / .35));
    weatherPending_ = !ready || weatherRamp_ < 1;
    const double radius = std::pow(10., planet.frame.logRadius);
    const auto layers = weatherLayers(climate);
    const double base = layers.cloudBaseMetres, thickness = layers.cloudTopMetres - layers.cloudBaseMetres;
    weatherData_[0] = {float(1 + base / radius), float(1 + (base + thickness) / radius),
                       float(climate.pressureBar), volumes ? weatherRamp_ : 0.f};
    weatherData_[1] = {float(std::clamp((seconds - fields[0]->seconds) / 8., 0., 1.)),
                       hyperionPhase(climate.seed), float(climate.moisture), 1};
    const auto flow =
        glm::mod(seconds * climate.windMetresPerSecond / 1000. * glm::dvec3(.29, .07, .13), glm::dvec3(256));
    weatherData_[2] = glm::vec4(flow, float(thickness / 1000.));
    weatherData_[3] = {1.f / cloudWidth_, 1.f / cloudHeight_, 20.f, 1};
    weatherFromEye_ = glm::mat4(mapping);
}
float HyperionRenderer::weatherBlend(const Address& planet) const {
    return weatherPlanet_ && *weatherPlanet_ == planet ? weatherData_[0].w : 0.f;
}
void HyperionRenderer::localLamp() {
    const glm::vec4 shadow{};
    const std::array<glm::vec4,4> weather{};
    const std::array<glm::vec4,3> visual{};
    bgfx::setUniform(sunParams_,glm::value_ptr(shadow));
    bgfx::setUniform(weatherParams_,weather.data(),4);
    bgfx::setUniform(weatherVisualParams_,visual.data(),3);
}
void HyperionRenderer::bind(const ScenePresentation& p, const glm::vec3& eye, const glm::vec3& light,
                            const glm::mat4& pattern, float primitive, double seconds, glm::vec4 cavity,
                            const glm::mat4& micro, float relief, const glm::mat4& meso, float mesoRelief) {
    init();
    bgfx::setUniform(regionLook_, regionLookData_.data(), 2);
    bgfx::setUniform(starTypeParams_, p.starType.data(), 2);
    bgfx::setUniform(planetParams_, p.planet.data(), 5);
    bgfx::setUniform(planetRingsParams_, p.planetRings.data(), 2);
    std::array<glm::vec4, 4> data = {glm::vec4(p.strength, float(seconds), p.phase, primitive), p.material,
                                     glm::vec4(eye, 1), glm::vec4(light, 0)};
    bgfx::setUniform(style_, data.data(), 4);
    bgfx::setUniform(pattern_, glm::value_ptr(pattern));
    bgfx::setUniform(domain_, glm::value_ptr(p.domain));
    bgfx::setUniform(surface_, glm::value_ptr(p.surface));
    bgfx::setUniform(terrainMaterial_, p.terrainMaterial.data(), 4);
    bgfx::setUniform(environment_, glm::value_ptr(p.environment));
    const auto contact =
        landingPlanet_ && *landingPlanet_ == p.materialFrame ? landingData_ : std::array<glm::vec4, 2>{};
    bgfx::setUniform(landingParams_, contact.data(), 2);
    bgfx::setUniform(cavity_, glm::value_ptr(cavity));
    auto weather = weatherData_;
    weather[1].w =
        weather[3].w > .5f && weatherPlanet_ && *weatherPlanet_ == p.materialFrame ? weatherRamp_ : 0.f;
    bgfx::setUniform(weatherParams_, weather.data(), 4);
    bgfx::setUniform(weatherMatrix_, glm::value_ptr(weatherFromEye_));
    const auto visual = weatherPlanet_ && *weatherPlanet_ == p.materialFrame ? weatherVisualData_
                                                                             : std::array<glm::vec4, 3>{};
    bgfx::setUniform(weatherVisualParams_, visual.data(), 3);
    bgfx::setTexture(7, weatherSampler_, bgfx::isValid(weatherTextures_[0]) ? weatherTextures_[0] : noise_,
                     BGFX_SAMPLER_V_CLAMP);
    bgfx::setTexture(8, weatherNextSampler_,
                     bgfx::isValid(weatherTextures_[1]) ? weatherTextures_[1] : noise_, BGFX_SAMPLER_V_CLAMP);
    bgfx::setUniform(sunMatrix_, glm::value_ptr(sunFromWorld_));
    const glm::vec4 shadow{sunActive_ ? 1.f : 0.f, 1.f / sunShadowResolution, 0, 0};
    bgfx::setUniform(sunParams_, glm::value_ptr(shadow));
    bgfx::setTexture(4, sunSampler_, sunActive_ ? bgfx::getTexture(sunTarget_) : detail_,
                     sampling | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT);
    {
        bgfx::setUniform(micro_, glm::value_ptr(micro));
        bgfx::setUniform(meso_, glm::value_ptr(meso));
        bgfx::setTexture(5, noiseSampler_, noise_);
        bgfx::setTexture(3, detailSampler_, detail_,
                         BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_ANISOTROPIC);
        const glm::vec4 microParams{relief, detailBytes_ ? 1.f : 0.f, mesoRelief, 0};
        bgfx::setUniform(microParams_, glm::value_ptr(microParams));
    }
}
bgfx::ViewId HyperionRenderer::planetRingView() {
    if (!readableDepth_)
        return 65;
    if (!bgfx::isValid(ringLayer_)) {
        ringColour_ = bgfx::createTexture2D(width_, height_, false, 1, bgfx::TextureFormat::RGBA16F,
                                           BGFX_TEXTURE_RT | sampling);
        const bgfx::TextureHandle attachments[]{ringColour_, bgfx::getTexture(scene_, depthAttachment_)};
        ringLayer_ = bgfx::createFrameBuffer(2, attachments, false);
        if (!bgfx::isValid(ringLayer_))
            throw std::runtime_error("Planet ring target allocation failed");
        bytes_ += size_t(width_) * height_ * 8;
    }
    if (!ringLayerActive_) {
        // Keep the world depth and accumulate only ring colour and coverage.
        bgfx::setViewFrameBuffer(ringLayerViewId, ringLayer_);
        bgfx::setViewClear(ringLayerViewId, BGFX_CLEAR_COLOR, 0);
        bgfx::setViewMode(ringLayerViewId, bgfx::ViewMode::Sequential);
        ringLayerActive_ = true;
    }
    return ringLayerViewId;
}
bgfx::ViewId HyperionRenderer::shipTrailView() {
    if (!bgfx::isValid(shipTrailLayer_)) {
        shipTrailMask_ = bgfx::createTexture2D(width_, height_, false, 1, bgfx::TextureFormat::R8,
                                              BGFX_TEXTURE_RT | sampling);
        const bgfx::TextureHandle attachments[]{shipTrailMask_, bgfx::getTexture(scene_, depthAttachment_)};
        shipTrailLayer_ = bgfx::createFrameBuffer(2, attachments, false);
        if (!bgfx::isValid(shipTrailLayer_))
            throw std::runtime_error("Ship trail target allocation failed");
        bytes_ += size_t(width_) * height_;
    }
    // Test against the world depth. Keep intensity separate from the scene's colour grade.
    bgfx::setViewFrameBuffer(shipTrailViewId, shipTrailLayer_);
    bgfx::setViewRect(shipTrailViewId, 0, 0, width_, height_);
    bgfx::setViewClear(shipTrailViewId, BGFX_CLEAR_COLOR, 0);
    bgfx::touch(shipTrailViewId);
    shipTrailsActive_ = true;
    return shipTrailViewId;
}
bgfx::ViewId HyperionRenderer::shipBlastView() {
    if(!bgfx::isValid(shipBlastLayer_)){
        shipBlastColour_=bgfx::createTexture2D(width_,height_,false,1,bgfx::TextureFormat::RGBA16F,
                                               BGFX_TEXTURE_RT|sampling);
        const bgfx::TextureHandle targets[]{shipBlastColour_,bgfx::getTexture(scene_,depthAttachment_)};
        shipBlastLayer_=bgfx::createFrameBuffer(2,targets,false);
        if(!bgfx::isValid(shipBlastLayer_))throw std::runtime_error("Ship blast target allocation failed");
        bytes_+=size_t(width_)*height_*8;
    }
    for(unsigned pass=0;pass<2;++pass){
        const auto view=bgfx::ViewId(shipBlastViewId+pass);
        bgfx::setViewFrameBuffer(view,shipBlastLayer_);
        bgfx::setViewRect(view,0,0,width_,height_);
        bgfx::setViewClear(view,pass?BGFX_CLEAR_NONE:BGFX_CLEAR_COLOR,0);
        bgfx::setViewMode(view,bgfx::ViewMode::Sequential);
    }
    bgfx::touch(shipBlastViewId);shipBlastsActive_=true;
    return shipBlastViewId;
}
bgfx::ProgramHandle HyperionRenderer::ringProgram(const PlanetTraits& t, glm::vec3 light, glm::vec3 eye,
                                                  float opacity, float logPlanetRadius) {
    init();
    if (!bgfx::isValid(rings_))
        rings_ = program("vs_planet_rings", "fs_planet_rings");
    const auto& r = t.rings;
    const auto colour = glm::mix(glm::vec3(.25, .16, .10), glm::vec3(.80, .77, .65), float(r.iceFraction));
    const std::array<glm::vec4, 4> data = {glm::vec4(float(r.innerRadius), float(r.outerRadius),
                                                     float(r.opticalDepth), float(r.seed % 10000) / 10000.f),
                                           glm::vec4(colour, opacity), glm::vec4(light, logPlanetRadius),
                                           glm::vec4(eye, ringLayerActive_ ? 1.f : 0.f)};
    bgfx::setUniform(ringParams_, data.data(), 4);
    bgfx::setUniform(regionLook_, regionLookData_.data(), 2);
    return rings_;
}
void HyperionRenderer::sky(const SpectralSky& sky, double seconds, float aspect, float tanHalfFov) {
    if (sky.strength <= .001f)
        return;
    init();
    if (!bgfx::isValid(sky_))
        sky_ = program("vs_hyperion_screen", "fs_hyperion_sky");
    const std::array<glm::vec4, 2> parameters{
        glm::vec4(sky.strength * (1.f), sky.phase, sky.gas, float(seconds)),
        glm::vec4(aspect * tanHalfFov, tanHalfFov, bgfx::getCaps()->originBottomLeft ? 1.f : -1.f, 0)};
    const auto rotation = glm::mat4_cast(glm::quat(sky.cameraToField));
    spaceGlassFromEye_=rotation;
    const float glass = spaceGlassEnabled_ ? sky.strength * 1.75f * regionLookData_[1].y : 0.f;
    spaceGlassData_ = {glass, sky.phase, float(seconds), 0};
    bgfx::setUniform(regionLook_, regionLookData_.data(), 2);
    bgfx::setUniform(spaceGlassParams_,glm::value_ptr(spaceGlassData_));
    bgfx::setUniform(skyParams_, parameters.data(), 2);
    bgfx::setUniform(skyRotation_, glm::value_ptr(rotation));
    bgfx::setTexture(5, noiseSampler_, noise_);
    // First in sequential view 0, before stars and all opaque/depth-tested
    // universe geometry. The optical field never writes depth or adds objects.
    screen(0, sky_);
}
void HyperionRenderer::bindEmissions(const Node& node, const glm::mat4& pattern, double seconds) {
    const auto stellar = stellarPresentation(node);
    bgfx::setUniform(starTypeParams_, stellar.data(), 2);
    const float phase = hyperionPhase(node.seed);
    std::array<glm::vec4, 4> data{};
    data[0] = {1.f, float(seconds), phase, 3};
    bgfx::setUniform(style_, data.data(), 4);
    bgfx::setUniform(pattern_, glm::value_ptr(pattern));
    const glm::vec4 domain{};
    bgfx::setUniform(domain_, glm::value_ptr(domain));
}
void HyperionRenderer::screen(bgfx::ViewId id, bgfx::ProgramHandle p) {
    struct Vertex {
        float x, y, z, u, v;
    };
    float bottom = bgfx::getCaps()->originBottomLeft ? 0.f : 1.f;
    float top = 1.f - bottom;
    const Vertex vertices[] = {
        {-1, -1, 0, 0, bottom}, {3, -1, 0, 2, bottom}, {-1, 3, 0, 0, top * 2 - bottom}};
    bgfx::TransientVertexBuffer vb;
    bgfx::allocTransientVertexBuffer(&vb, 3, screenLayout_);
    std::memcpy(vb.data, vertices, sizeof(vertices));
    bgfx::setVertexBuffer(0, &vb);
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(id, p);
}
void HyperionRenderer::resolveExterior(float fixedFocus) {
    if (readableDepth_) {
        weatherData_[3].x = 1.f / cloudWidth_;
        weatherData_[3].y = 1.f / cloudHeight_;
        bgfx::setViewFrameBuffer(78, cloudTarget_);
        bgfx::setViewRect(78, 0, 0, cloudWidth_, cloudHeight_);
        bgfx::setViewClear(78, BGFX_CLEAR_COLOR, 0x000000ff);
        bgfx::touch(78);
        if (weatherData_[0].w > 0 && atmosphereData_[0].w > 0) {
            if (!bgfx::isValid(cloudVolumeProgram_))
                cloudVolumeProgram_ = program("vs_hyperion_screen", "fs_hyperion_cloud_volume");
            bgfx::setTexture(2, depthSampler_, bgfx::getTexture(scene_, depthAttachment_),
                             sampling | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT);
            bgfx::setTexture(5, noiseSampler_, noise_);
            bgfx::setTexture(7, weatherSampler_, weatherTextures_[0], BGFX_SAMPLER_V_CLAMP);
            bgfx::setTexture(8, weatherNextSampler_, weatherTextures_[1], BGFX_SAMPLER_V_CLAMP);
            bgfx::setUniform(weatherParams_, weatherData_.data(), 4);
            bgfx::setUniform(weatherVisualParams_, weatherVisualData_.data(), 3);
            bgfx::setUniform(atmosphere_, atmosphereData_.data(), 4);
            bgfx::setUniform(atmosphereRotation_, glm::value_ptr(atmosphereMatrix_));
            screen(78, cloudVolumeProgram_);
        }
        bgfx::setViewFrameBuffer(79, polarTarget_);
        bgfx::setViewRect(79, 0, 0, std::max(1, width_ / 2), std::max(1, height_ / 2));
        bgfx::setViewClear(79, BGFX_CLEAR_COLOR, 0);
        bgfx::touch(79);
        if (polarData_[0].w > 0) {
            if (!bgfx::isValid(polarProgram_))
                polarProgram_ = program("vs_hyperion_screen", "fs_hyperion_polar");
            bgfx::setTexture(2, depthSampler_, bgfx::getTexture(scene_, depthAttachment_),
                             sampling | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT);
            bgfx::setUniform(atmosphere_, atmosphereData_.data(), 4);
            bgfx::setUniform(atmosphereRotation_, glm::value_ptr(atmosphereMatrix_));
            bgfx::setUniform(polar_, polarData_.data(), 2);
            screen(79, polarProgram_);
        }
    }
    for (unsigned i = 0; i < 2; ++i) {
        auto id = bgfx::ViewId(80 + i);
        bgfx::setViewFrameBuffer(id, bloom_[i]);
        bgfx::setViewRect(id, 0, 0, std::max(1, width_ / 3), std::max(1, height_ / 3));
        bgfx::setViewClear(id, BGFX_CLEAR_COLOR, 0x000000ff);
        std::array<glm::vec4, 2> params{};
        params[0] =
            i == 0 ? glm::vec4(4.f / width_, 0, 1, 0) : glm::vec4(0, 4.f / std::max(1, height_ / 3), 0, 0);
        bgfx::setUniform(postParams_, params.data(), 2);
        bgfx::setTexture(0, sceneSampler_, bgfx::getTexture(i == 0 ? scene_ : bloom_[0]), sampling);
        auto blurProgram = blur_;
        if (i == 1 && readableDepth_ && stellarData_[0].w > 0) {
            if (!bgfx::isValid(solarBlur_))
                solarBlur_ = program("vs_hyperion_screen", "fs_hyperion_blur_solar");
            stellarData_[2].w = weatherData_[0].w > 0 ? 1.f : 0.f;
            bgfx::setUniform(stellarParams_, stellarData_.data(), 3);
            bgfx::setUniform(starTypeParams_, starTypeData_.data(), 2);
            bgfx::setUniform(starMatrix_, glm::value_ptr(starFromEye_));
            bgfx::setTexture(5, noiseSampler_, noise_);
            bgfx::setTexture(2, depthSampler_, bgfx::getTexture(scene_, depthAttachment_),
                             sampling | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT);
            bgfx::setTexture(9, cloudSampler_, bgfx::getTexture(cloudTarget_), sampling);
            blurProgram = solarBlur_;
        }
        screen(id, blurProgram);
    }
    // Keep atmosphere, reflections and refracted space in HDR until the
    // camera has extracted its highlights. The display curve runs once at 85.
    const std::array<glm::vec4,2> lens{{
        {lensEnabled_&&readableDepth_&&flight_?1.f:0.f,1.15f,5.f,1.f},
        {1.f/width_,1.f/height_,float(worldNearPlane),float(worldFarPlane)}}};
    bgfx::setUniform(lensParams_,lens.data(),2);
    bgfx::setViewFrameBuffer(82,optical_);
    bgfx::setUniform(regionLook_, regionLookData_.data(), 2);
    bgfx::setViewRect(82,0,0,width_,height_);
    std::array<glm::vec4, 2> params{};
    params[0] = {1.f / width_, 1.f / height_, 1.f, reflectiveTargets_ ? 1.f : 0.f};
    params[1] = {1.f / std::max(1, width_ / 2), 1.f / std::max(1, height_ / 2),
                 ringLayerActive_ ? 1.f : 0.f, fixedFocus};
    bgfx::setUniform(postParams_, params.data(), 2);
    bgfx::setTexture(0, sceneSampler_, bgfx::getTexture(scene_), sampling);
    bgfx::setTexture(1, bloomSampler_, bgfx::getTexture(bloom_[1]), sampling);
    const bool optical = readableDepth_;
    auto finalProgram = post_;
    if (optical) {
        bgfx::setTexture(4, ringSampler_, ringLayerActive_ ? ringColour_ : noise_, sampling);
        stellarData_[2].w = weatherData_[0].w > 0 ? 1.f : 0.f;
        bgfx::setUniform(stellarParams_, stellarData_.data(), 3);
        bgfx::setUniform(starTypeParams_, starTypeData_.data(), 2);
        bgfx::setUniform(starMatrix_, glm::value_ptr(starFromEye_));
        bgfx::setTexture(5, noiseSampler_, noise_);
        if (!bgfx::isValid(flightPost_))
            flightPost_ = program("vs_hyperion_screen", "fs_hyperion_flight_post");
        finalProgram = flightPost_;
        if (weatherVisualData_[0].x > 0) {
            if (!bgfx::isValid(weatherPost_))
                weatherPost_ = program("vs_hyperion_screen", "fs_hyperion_weather_post");
            finalProgram = weatherPost_;
            bgfx::setTexture(7, weatherSampler_,
                             bgfx::isValid(weatherTextures_[0]) ? weatherTextures_[0] : noise_,
                             BGFX_SAMPLER_V_CLAMP);
            bgfx::setTexture(8, weatherNextSampler_,
                             bgfx::isValid(weatherTextures_[1]) ? weatherTextures_[1] : noise_,
                             BGFX_SAMPLER_V_CLAMP);
        }
        bgfx::setTexture(2, depthSampler_, bgfx::getTexture(scene_, depthAttachment_),
                         sampling | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT);
        const std::array<glm::vec4,2> reflection{{
            {float(width_)/height_*.7002075f,.7002075f,bgfx::getCaps()->originBottomLeft?1.f:-1.f,float(worldNearPlane)},
            {float(worldFarPlane),reflectiveTargets_&&reflectionsEnabled_?1.f:0.f,1.f/width_,1.f/height_}}};
        bgfx::setUniform(reflectionParams_,reflection.data(),2);
        bgfx::setUniform(spaceGlassParams_,glm::value_ptr(spaceGlassData_));
        bgfx::setUniform(spaceGlassMatrix_,glm::value_ptr(spaceGlassFromEye_));
        bgfx::setUniform(wormholeParams_, wormholeData_.data(), 5);
        bgfx::setTexture(3,reflectionSampler_,bgfx::getTexture(scene_,reflectiveTargets_?1:0),sampling|BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT);
        bgfx::setUniform(atmosphere_, atmosphereData_.data(), 4);
        bgfx::setUniform(weatherParams_, weatherData_.data(), 4);
        bgfx::setUniform(weatherVisualParams_, weatherVisualData_.data(), 3);
        bgfx::setTexture(9, cloudSampler_, bgfx::getTexture(cloudTarget_),
                         sampling | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT);
        bgfx::setUniform(polar_, polarData_.data(), 2);
        bgfx::setTexture(6, polarSampler_, bgfx::getTexture(polarTarget_),
                         sampling | BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT);
        bgfx::setUniform(atmosphereRotation_, glm::value_ptr(atmosphereMatrix_));
    }
    screen(82, finalProgram);
    const auto depth=readableDepth_?bgfx::getTexture(scene_,depthAttachment_):noise_;
    // Match the source footprint to the aperture gather. A coarse light seed
    // prevents large cat's-eye highlights breaking into individual sample dots.
    params[0].z=(bgfx::getCaps()->formats[bgfx::TextureFormat::RGBA16F]&
                 BGFX_CAPS_FORMAT_TEXTURE_MIP_AUTOGEN)?1.f:0.f;
    bgfx::setViewFrameBuffer(83,lensSeed_);
    bgfx::setViewRect(83,0,0,std::max(1,width_/3),std::max(1,height_/3));
    bgfx::setUniform(lensParams_,lens.data(),2);
    bgfx::setUniform(postParams_,params.data(),2);
    bgfx::setTexture(0,sceneSampler_,bgfx::getTexture(optical_),sampling);
    bgfx::setTexture(2,depthSampler_,depth,sampling|BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT);
    screen(83,lensSeedProgram_);
    bgfx::setViewFrameBuffer(84,bokeh_);
    bgfx::setViewRect(84,0,0,std::max(1,width_/3),std::max(1,height_/3));
    bgfx::setUniform(lensParams_,lens.data(),2);
    bgfx::setUniform(postParams_,params.data(),2);
    bgfx::setTexture(0,sceneSampler_,bgfx::getTexture(lensSeed_),sampling);
    bgfx::setTexture(2,depthSampler_,depth,sampling|BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT);
    screen(84,bokehProgram_);
    bgfx::setViewFrameBuffer(85,display_);
    bgfx::setViewRect(85,0,0,width_,height_);
    auto lensLook = regionLookData_;
    lensLook[1].w = shipTrailsActive_ ? 1.f : 0.f;
    bgfx::setUniform(regionLook_, lensLook.data(), 2);
    bgfx::setUniform(lensParams_,lens.data(),2);
    bgfx::setUniform(postParams_,params.data(),2);
    bgfx::setUniform(stellarParams_,stellarData_.data(),3);
    bgfx::setUniform(starTypeParams_,starTypeData_.data(),2);
    bgfx::setUniform(starMatrix_,glm::value_ptr(starFromEye_));
    bgfx::setTexture(0,sceneSampler_,bgfx::getTexture(optical_),sampling);
    bgfx::setTexture(1,bloomSampler_,bgfx::getTexture(bokeh_),sampling);
    bgfx::setTexture(2,depthSampler_,depth,sampling|BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT);
    bgfx::setTexture(3,focusSeedSampler_,bgfx::getTexture(lensSeed_),sampling|BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT);
    bgfx::setTexture(4,ringSampler_,ringLayerActive_ ? ringColour_ : noise_,sampling);
    bgfx::setTexture(5,noiseSampler_,noise_);
    bgfx::setTexture(6,shipTrailSampler_,shipTrailsActive_ ? shipTrailMask_ : noise_,sampling);
    bgfx::setTexture(10,shipBlastSampler_,shipBlastsActive_ ? shipBlastColour_ : noise_,sampling);
    const glm::vec4 blastParams{shipBlastsActive_?1.f:0.f,0,0,0};
    bgfx::setUniform(shipBlastParams_,glm::value_ptr(blastParams));
    bgfx::setTexture(9,cloudSampler_,readableDepth_?bgfx::getTexture(cloudTarget_):noise_,sampling);
    screen(85,lensProgram_);
}

bgfx::ProgramHandle HyperionRenderer::cockpitProgram() {
    init();
    if(!cabinReflective_)return geometry_;
    if(!bgfx::isValid(cabinGeometry_))cabinGeometry_=program("vs_hyperion","fs_xalaria_cabin");
    return cabinGeometry_;
}

void HyperionRenderer::cabinSun(const glm::mat4& hullFromEye,glm::vec3 direction,glm::vec3 colour,
                               float power,float angularRadius) {
    cabinFromEye_=hullFromEye;
    cabinSunData_[0]={glm::dot(direction,direction)>1e-20f?glm::normalize(direction):glm::vec3(0,1,0),power};
    cabinSunData_[1]={colour,angularRadius};
}

bool HyperionRenderer::beginCabin(float tanHalfFov, float farClip) {
    cabinFrustum_={tanHalfFov*float(width_)/height_,
                   tanHalfFov*(bgfx::getCaps()->originBottomLeft?1.f:-1.f),.025f,farClip};
    if (!bgfx::isValid(cabin_)) {
        cabinDepthReadable_=bgfx::isTextureValid(0,false,1,bgfx::TextureFormat::D32F,BGFX_TEXTURE_RT|sampling);
        // MSAA covers the actual thin fitting edges before the cabin resolves.
        // A post-process alone cannot recover their subpixel coverage in motion.
        const uint64_t multisample=
            bgfx::isTextureValid(0,false,1,bgfx::TextureFormat::RGBA16F,BGFX_TEXTURE_RT_MSAA_X4|sampling) &&
            bgfx::isTextureValid(0,false,1,bgfx::TextureFormat::D32F,BGFX_TEXTURE_RT_MSAA_X4|sampling)
                ? BGFX_TEXTURE_RT_MSAA_X4 : 0;
        cabinReflective_=cabinDepthReadable_&&reflectionsEnabled_&&bgfx::getCaps()->limits.maxFBAttachments>=3;
        cabinDepthAttachment_=cabinReflective_?2:1;
        std::array<bgfx::TextureHandle,3> targets;
        targets.fill(BGFX_INVALID_HANDLE);
        targets[0]=bgfx::createTexture2D(width_,height_,false,1,bgfx::TextureFormat::RGBA16F,BGFX_TEXTURE_RT|multisample|sampling);
        if(cabinReflective_)
            targets[1]=bgfx::createTexture2D(width_,height_,false,1,bgfx::TextureFormat::RGBA16F,BGFX_TEXTURE_RT|multisample|sampling);
        targets[cabinDepthAttachment_]=bgfx::createTexture2D(width_,height_,false,1,bgfx::TextureFormat::D32F,
                                  (cabinDepthReadable_?BGFX_TEXTURE_RT|sampling:BGFX_TEXTURE_RT_WRITE_ONLY)|multisample);
        bool valid=true;
        for(unsigned i=0;i<=cabinDepthAttachment_;++i)valid&=bgfx::isValid(targets[i]);
        if(valid)cabin_=bgfx::createFrameBuffer(cabinDepthAttachment_+1,targets.data(),true);
        if (!bgfx::isValid(cabin_)) {
            for (auto target : targets)
                if (bgfx::isValid(target))
                    bgfx::destroy(target);
            cabinReflective_=false;
            return false;
        }
        bytes_ += size_t(width_)*height_*(cabinReflective_?20:12)*(multisample?5:1);
    }
    bgfx::setViewFrameBuffer(cabinViewId,cabin_);
    bgfx::setViewClear(cabinViewId,BGFX_CLEAR_COLOR|BGFX_CLEAR_DEPTH,0,1.f,0);
    return true;
}

void HyperionRenderer::finishCabin() {
    // Reuse the world blur buffers after the exterior lens has resolved.
    const auto cabinColour = bgfx::getTexture(cabin_);
    for (unsigned pass=0;pass<2;++pass) {
        const auto view = bgfx::ViewId(cabinViewId+1+pass);
        const int w = std::max(1,width_/3), h = std::max(1,height_/3);
        bgfx::setViewFrameBuffer(view,bloom_[pass]);
        bgfx::setViewRect(view,0,0,w,h);
        const std::array<glm::vec4,2> blur{{
            pass==0 ? glm::vec4(6.f/width_,0,1,0) : glm::vec4(0,2.f/h,0,0), {}
        }};
        bgfx::setUniform(postParams_,blur.data(),2);
        bgfx::setTexture(0,sceneSampler_,pass==0 ? cabinColour : bgfx::getTexture(bloom_[0]),sampling);
        screen(view,blur_);
    }
    if (!bgfx::isValid(cabinComposite_))
        cabinComposite_ = program("vs_hyperion_screen","fs_xalaria_cabin_composite");
    constexpr bgfx::ViewId view = cabinViewId+3;
    bgfx::setViewFrameBuffer(view,BGFX_INVALID_HANDLE);
    bgfx::setViewRect(view,0,0,width_,height_);
    const std::array<glm::vec4,2> params{{
        {1.f/width_,1.f/height_,cabinFrustum_.x,cabinFrustum_.y},
        {cabinFrustum_.z,cabinFrustum_.w,.13f,cabinDepthReadable_?.56f:0.f}
    }};
    bgfx::setUniform(postParams_,params.data(),2);
    bgfx::setTexture(0,sceneSampler_,bgfx::getTexture(display_),sampling);
    bgfx::setUniform(cabinSunParams_,cabinSunData_.data(),2);
    bgfx::setUniform(cabinFromEyeParams_,glm::value_ptr(cabinFromEye_));
    bgfx::setTexture(5,noiseSampler_,noise_);
    bgfx::setTexture(1,bloomSampler_,bgfx::getTexture(bloom_[1]),sampling);
    // Apply the same photographic image circle to the cabin layer. The HUD
    // is drawn afterward, so navigation and instrument overlays stay sharp.
    const std::array<glm::vec4,2> cabinLens{{
        {lensEnabled_?1.f:0.f,1.15f,5.f,cabinReflective_&&reflectionsEnabled_?1.f:0.f},
        {1.f/width_,1.f/height_,cabinFrustum_.z,cabinFrustum_.w}
    }};
    bgfx::setUniform(lensParams_,cabinLens.data(),2);
    // The exterior depth slot is idle after its lens pass.
    bgfx::setTexture(2,depthSampler_,cabinColour,sampling);
    // Reuse the existing opaque cabin depth and the idle exterior surface
    // sampler. No extra render target or temporal shadow history is needed.
    bgfx::setTexture(3,reflectionSampler_,cabinDepthReadable_?bgfx::getTexture(cabin_,cabinDepthAttachment_):noise_,
                     sampling|BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT);
    bgfx::setTexture(4,ringSampler_,cabinReflective_?bgfx::getTexture(cabin_,1):noise_,
                     sampling|BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT);
    screen(view,cabinComposite_);
}

void HyperionRenderer::presentExterior() {
    // The canopy and the back buffer both sample this finished camera image.
    bgfx::setViewFrameBuffer(86,BGFX_INVALID_HANDLE);
    bgfx::setViewRect(86,0,0,width_,height_);
    bgfx::setTexture(0,sceneSampler_,bgfx::getTexture(display_),sampling);
    screen(86,presentProgram_);
}
} // namespace infinity

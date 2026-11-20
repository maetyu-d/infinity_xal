#include "infinity/ShipExplosion.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace infinity {
ShipPropellant shipPropellant(const ActivityBody& body) {
    // A stable, role-weighted design attribute: unrelated to region tint,
    // camera, clock, hull damage or the address frame used to draw the ship.
    const auto choice=mix64(body.seed^0x50524f50454c4c41ULL)%100;
    if(body.kind==ActivityKind::Raider)
        return choice<55?ShipPropellant::Hypergolic:choice<80?ShipPropellant::KeroseneLox:
               choice<90?ShipPropellant::HydrogenLox:ShipPropellant::MethaneLox;
    if(body.kind==ActivityKind::Freighter||body.kind==ActivityKind::Ferry)
        return choice<15?ShipPropellant::Hypergolic:choice<60?ShipPropellant::KeroseneLox:
               choice<80?ShipPropellant::HydrogenLox:ShipPropellant::MethaneLox;
    if(body.kind==ActivityKind::Survey)
        return choice<15?ShipPropellant::Hypergolic:choice<30?ShipPropellant::KeroseneLox:
               choice<75?ShipPropellant::HydrogenLox:ShipPropellant::MethaneLox;
    return choice<30?ShipPropellant::Hypergolic:choice<50?ShipPropellant::KeroseneLox:
           choice<80?ShipPropellant::HydrogenLox:ShipPropellant::MethaneLox;
}
std::string_view shipPropellantName(ShipPropellant fuel) {
    constexpr std::array names{"NTO / hydrazine", "RP-1 / LOX", "LH2 / LOX", "Methane / LOX"};
    return names.at(size_t(fuel));
}
ShipExplosionImage sampleShipExplosion(const ShipExplosion& blast) {
    ShipExplosionImage image;
    if (!std::isfinite(blast.age) || blast.age < 0. || blast.age >= shipExplosionLifetime ||
        !std::isfinite(blast.radius) || blast.radius <= 0.) return image;
    const double t = blast.age, r = std::clamp(blast.radius,4.,500.)*1.20;
    const Random random{blast.seed};
    struct Palette {glm::vec3 hot,cool,core,halo,shock,gas;float gasOpacity;unsigned gasCount;};
    // Colour direction is inspired by the propellant family; luminosity is
    // exaggerated for gameplay, especially for faint clean hydrogen flames.
    constexpr std::array palettes{
        Palette{{1.,.43,.065},{.67,.12,.025},{1.,.81,.48},{1.,.25,.025},{1.,.65,.30},{.38,.13,.038},.24f,36},
        Palette{{1.,.68,.10},{.80,.19,.018},{1.,.93,.65},{1.,.38,.025},{1.,.78,.41},{.068,.050,.038},.27f,36},
        Palette{{.09,.49,1.},{.34,.055,.84},{.62,.84,1.},{.075,.22,1.},{.32,.66,1.},{.28,.39,.52},.024f,6},
        Palette{{.20,.30,1.},{.49,.045,.88},{.77,.78,1.},{.32,.075,.95},{.54,.36,1.},{.23,.25,.39},.055f,9}
    };
    const auto& palette=palettes.at(size_t(blast.propellant));
    image.coreColour=palette.core;image.haloColour=palette.halo;image.shockColour=palette.shock;
    const auto direction = [&](unsigned key) {
        const double z = random.unit(key)*2.-1., angle = random.unit(key+1)*2.*std::numbers::pi;
        const double ring = std::sqrt(std::max(0.,1.-z*z));
        return glm::dvec3(ring*std::cos(angle),z,ring*std::sin(angle));
    };
    image.flash = float((1.-smoothstep(.07,.72,t))*(.65+.35*smoothstep(0.,.045,t)));
    image.afterglow = float((1.-smoothstep(.8,4.8,t))*.36);
    image.coreRadius = r*(.8+3.2*(1.-std::exp(-t*6.)));
    image.haloRadius = r*(3.+6.*(1.-std::exp(-t*2.4)));
    image.shockRadius = r*(1.15+11.5*t);
    image.shock = float(smoothstep(0.,.06,t)*(1.-smoothstep(.18,2.5,t))*.46);

    // Irregular overlapping lobes occupy a real 3D volume. There is no up
    // direction, gravity, camera-facing radial pattern or per-frame RNG.
    for (unsigned i=0;i<54;++i) {
        const unsigned key=32+i*11;
        const double age=t-random.unit(key)*.28;
        if (age<0. || age>=4.5) continue;
        const double grow=1.-std::exp(-age*1.8), life=1.-smoothstep(.65,4.5,age);
        const auto axis=direction(key+1);
        const double travel=r*(.12+(2.4+random.unit(key+3)*2.2)*grow);
        const float cool=float(smoothstep(.10,2.8,age));
        auto& p=image.particles[image.count++];
        p.offset=axis*travel;
        p.radius=r*(.32+grow*(.75+random.unit(key+4)*.6))*std::sqrt(life);
        p.colour=glm::mix(palette.hot,palette.cool,cool);
        p.opacity=float(life*(.20+.22*random.unit(key+5))*smoothstep(0.,.08,age));
    }
    // Several secondary reactor/fuel flashes punctuate the expanding cloud.
    for (unsigned i=0;i<6;++i) {
        const double phase=double(i)*3./5.;
        const double age=t-(.42+phase*.32+random.unit(501+i)*.12);
        if (age<0. || age>=.75) continue;
        auto& p=image.particles[image.count++];
        p.offset=direction(510+i*3)*r*(1.2+phase*.48);
        p.radius=r*(.7+1.5*age);
        p.colour=palette.core;
        p.opacity=float((1.-smoothstep(0.,.75,age))*.8);
    }
    for (unsigned i=0;i<108;++i) {
        const unsigned key=600+i*13;
        const double fade=1.-smoothstep(4.+random.unit(key)*2.,shipExplosionLifetime,t);
        if (fade<=0.) continue;
        const auto axis=direction(key+1);
        const double speed=r*(1.2+random.unit(key+3)*6.2);
        auto& p=image.particles[image.count++];
        p.velocity=axis*speed;
        p.offset=axis*r*.34+p.velocity*t;
        p.axis=direction(key+4);
        p.spin=(random.unit(key+6)*2.-1.)*3.;
        p.kind=i<60?ExplosionParticleKind::Fragment:ExplosionParticleKind::Ember;
        p.radius=r*(i<60?.025+random.unit(key+7)*.14:.009+random.unit(key+7)*.024);
        const float cool=float(smoothstep(.35,7.,t+random.unit(key+8)));
        // Hot metal can retain amber incandescence inside a blue fuel fire.
        p.colour=glm::mix(glm::mix(glm::vec3(1.,.66,.18),palette.hot,.35f),glm::vec3(.35,.026,.008),cool);
        p.opacity=float(fade*(i<60?1.:.7));
    }
    for(unsigned i=0;i<palette.gasCount;++i){
        const unsigned key=1900+i*7;
        const double life=smoothstep(.12,1.1,t)*(1.-smoothstep(5.,shipExplosionLifetime,t));
        if(life<=0.)continue;
        auto& p=image.particles[image.count++];
        const auto axis=direction(key);
        p.kind=ExplosionParticleKind::Vapour;
        p.offset=axis*r*(.6+(1.-std::exp(-t*.55))*(3.+random.unit(key+2)*2.5));
        p.radius=r*(.65+(1.-std::exp(-t*.8))*(1.7+random.unit(key+3)));
        p.colour=palette.gas*float(.75+.25*random.unit(key+4));
        p.opacity=float(life)*palette.gasOpacity*float(.6+.4*random.unit(key+5));
    }
    return image;
}
} // namespace infinity

#include "infinity/Activity.hpp"
#include "infinity/OrbitalDebris.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace infinity {
namespace {
const glm::vec3 hull{.31, .39, .45}, trim{.48, .62, .68}, glass{.13, .37, .53}, glow{.45, .91, .88};
void tri(Mesh& m, glm::dvec3 a, glm::dvec3 b, glm::dvec3 c, glm::vec3 color) {
    const auto n = glm::normalize(glm::cross(b - a, c - a));
    const float light = float(.54 + .46 * std::max(0., glm::dot(n, glm::normalize(glm::dvec3(.4, .8, -.5)))));
    const auto first = uint32_t(m.vertices.size());
    for (auto p : {a, b, c})
        m.vertices.push_back({float(p.x), float(p.y), float(p.z), rgba(color * light)});
    m.indices.insert(m.indices.end(), {first, first + 1, first + 2});
}
void box(Mesh& m, glm::dvec3 c, glm::dvec3 dimensions, glm::vec3 color, double yaw = 0.) {
    const auto h = dimensions * .5;
    std::array<glm::dvec3,8> p{{{-h.x,-h.y,-h.z},{h.x,-h.y,-h.z},{h.x,h.y,-h.z},{-h.x,h.y,-h.z},
                               {-h.x,-h.y,h.z},{h.x,-h.y,h.z},{h.x,h.y,h.z},{-h.x,h.y,h.z}}};
    const double cs=std::cos(yaw), sn=std::sin(yaw);
    for (auto& v : p)
        v = c + glm::dvec3(v.x*cs-v.z*sn,v.y,v.x*sn+v.z*cs);
    for (auto face : {std::array{0, 3, 2, 1}, std::array{4, 5, 6, 7}, std::array{0, 4, 7, 3},
                      std::array{1, 2, 6, 5}, std::array{3, 7, 6, 2}, std::array{0, 1, 5, 4}}) {
        tri(m, p[face[0]], p[face[1]], p[face[2]], color);
        tri(m, p[face[0]], p[face[2]], p[face[3]], color);
    }
}
void ring(Mesh& m, double radius, double thickness, double y, glm::vec3 color, unsigned sectors) {
    for (unsigned i = 0; i < sectors; i++) {
        const double a = i * 2 * std::numbers::pi / sectors, b = (i + 1) * 2 * std::numbers::pi / sectors;
        const auto p = [&](double r, double t, double h) {
            return glm::dvec3(std::cos(t) * r, h, std::sin(t) * r);
        };
        for (int side : {-1, 1}) {
            const double h = y + side * thickness * .5;
            tri(m, p(radius - thickness, a, h), p(radius + thickness, a, h), p(radius + thickness, b, h),
                color);
            tri(m, p(radius - thickness, a, h), p(radius + thickness, b, h), p(radius - thickness, b, h),
                color);
        }
        tri(m, p(radius + thickness, a, y - thickness * .5), p(radius + thickness, b, y - thickness * .5),
            p(radius + thickness, b, y + thickness * .5), color * .85f);
        tri(m, p(radius + thickness, a, y - thickness * .5), p(radius + thickness, b, y + thickness * .5),
            p(radius + thickness, a, y + thickness * .5), color * .85f);
    }
}
void drum(Mesh& mesh, glm::dvec3 centre, double radius, double height, glm::vec3 colour, unsigned sectors) {
    const auto point = [&](double angle, double r, double y) {
        return centre + glm::dvec3(r*std::cos(angle),y,r*std::sin(angle));
    };
    constexpr std::array bevel{glm::dvec2(.79,-.5),glm::dvec2(1.,-.33),glm::dvec2(1.,.33),glm::dvec2(.79,.5)};
    for (unsigned i=0; i<sectors; ++i) {
        const double a=i*2*std::numbers::pi/sectors, b=(i+1)*2*std::numbers::pi/sectors;
        for (size_t j=1; j<bevel.size(); ++j) {
            const auto lower=bevel[j-1], upper=bevel[j];
            const auto p=point(a,radius*lower.x,height*lower.y), q=point(b,radius*lower.x,height*lower.y);
            const auto u=point(a,radius*upper.x,height*upper.y), v=point(b,radius*upper.x,height*upper.y);
            tri(mesh,p,u,q,colour);
            tri(mesh,u,v,q,colour);
        }
        for (double side : {-1.,1.}) {
            const double y=side*height*.5;
            const auto p=point(a,radius*.79,y), q=point(b,radius*.79,y);
            tri(mesh,centre+glm::dvec3(0,y,0),side>0?q:p,side>0?p:q,colour);
        }
    }
}
} // namespace
Mesh activityMesh(ActivityKind kind, unsigned detail) {
    detail = std::min(detail, 2u);
    Mesh m;
    // Source silhouettes: Hyperion flight/settlements.cpp::buildCraft and orbitalCore.
    if (kind == ActivityKind::Harbour) {
        const glm::vec3 ceramic{.42,.49,.51}, insulation{.51,.30,.145}, radiator{.095,.157,.205};
        const unsigned sectors=detail ? 48 : 24;
        box(m, {0,-.04,0}, {1.60,.21,.39}, hull);
        drum(m,{0,.06,0},.35,.40,hull,sectors/2);
        drum(m,{0,.33,0},.265,.23,ceramic,sectors/2);
        drum(m,{0,.48,0},.179,.13,trim,sectors/2);
        ring(m,.82,.066,0,hull,sectors);
        ring(m,.795,.048,-.14,hull,sectors);
        ring(m,.284,.021,.28,glass,sectors/2);
        if (detail) {
            // An open arrival terrace on the retained harbour, facing local +Z.
            box(m, {0, .105, 1.12}, {.18, .065, .66}, hull);
            box(m, {0, .13, 1.45}, {.36, .025, .48}, {.055, .08, .11});
            for (int side : {-1, 1}) {
                box(m, {side * .18, .15, 1.45}, {.012, .025, .48}, glow);
                box(m, {side * .21, .20, 1.22}, {.024, .16, .035}, trim);
                box(m, {side * .21, .29, 1.22}, {.028, .025, .035}, {.74, .35, .93});
                for (int i = 0; i < 7; ++i)
                    box(m, {side * .12, .146, 1.25 + i * .06}, {.018, .006, .022}, glow);
            }
            box(m, {0, .146, 1.21}, {.30, .007, .016}, {.95, .65, .26});
            box(m, {0, .278, 1.22}, {.44, .025, .03}, hull);
            box(m, {0, .278, 1.24}, {.38, .010, .008}, {.74, .35, .93});
            if (detail == 2) {
                for (int i = 0; i < 8; ++i) {
                    const double z = 1.28 + i * .05;
                    for (int side : {-1, 1}) {
                        box(m, {side * .105, .144, z}, {.12, .004, .042}, {.035, .045, .06});
                        const glm::dvec3 a(0, .151, z - .012), b(side * .032, .151, z + .010),
                            c(side * .032, .151, z + .019);
                        tri(m, a, side > 0 ? c : b, side > 0 ? b : c, glow);
                    }
                }
            }
            ring(m,.824,.012,.051,glow,sectors);
            ring(m,.792,.014,-.18,trim,sectors);
            ring(m,.304,.015,.19,insulation,sectors/2);
            drum(m,{0,-.24,0},.23,.18,radiator,sectors/2);
            for (int segment=0; segment<12; ++segment) {
                const double angle=(segment+.5)*2*std::numbers::pi/12;
                const auto radial=glm::dvec3(std::cos(angle),0,std::sin(angle));
                box(m,radial*.56+glm::dvec3(0,-.126,0),{.47,.067,.074},trim,angle);
                box(m,radial*.818+glm::dvec3(0,-.062,0),{.12,.192,.067},ceramic,angle);
                box(m,radial*.875+glm::dvec3(0,-.054,0),{.018,.048,.062},glass,angle);
            }
            for (int side : {-1, 1}) {
                drum(m,{side*.59,.117,-.06},.168,.32,hull,sectors/3);
                drum(m,{side*.59,.293,-.06},.122,.071,ceramic,sectors/3);
                box(m, {side * .52, -.05, .58}, {.53, .025, .32}, glass);
                box(m,{side*.55,-.058,-.66},{.49,.048,.51},radiator);
                for (int fin=0; fin<(detail==2 ? 9 : 4); ++fin) {
                    const double z=-.88+fin*(detail==2 ? .055 : .139);
                    box(m,{side*.55,-.025,z},{.465,.025,.023},trim);
                }
                box(m, {side * .82, .035, 0}, {.04, .08, .20}, glow);
            }
        }
        if (detail == 2) {
            for (int i = 0; i < 8; i++) {
                const double a = i * 2 * std::numbers::pi / 8;
                box(m, {std::cos(a) * .82, .12, std::sin(a) * .82}, {.13, .12, .13}, trim);
                box(m, {std::cos(a) * .82, .19, std::sin(a) * .82}, {.05, .02, .05}, glow);
            }
            for (int side : {-1, 1}) {
                box(m, {side * .45, .43, 0}, {.035, .42, .035}, trim);
                box(m, {side * .45, .64, -.16}, {.035, .035, .35}, trim);
                for (int port=0; port<5; ++port) {
                    const double z=-.34+port*.10;
                    box(m,{side*.602,.105,z},{.064,.038,.062},glass);
                    box(m,{side*.609,.104,z},{.046,.018,.031},glow);
                }
                box(m,{side*.157,.082,1.46},{.04,.045,.42},trim);
            }
            drum(m,{0,.59,0},.069,.19,hull,16);
            drum(m,{0,.686,0},.082,.046,insulation,16);
            box(m,{0,.744,0},{.022,.13,.022},trim);
            box(m,{0,.814,0},{.040,.024,.040},glow);
        }
    } else if (kind == ActivityKind::Freighter) {
        box(m, {}, {1.4, .6, 2.2}, hull);
        for (int side : {-1, 1}) {
            box(m, {side * .9, 0, .1}, {.35, .4, 2}, hull);
            if (detail) {
                box(m, {side * .72, .33, .15}, {.12, .08, 1.8}, glow);
                for (int i = 0; i < (detail == 2 ? 4 : 2); i++)
                    box(m, {side * .91, -.45, .85 - i * .55}, {.5, .5, .48}, {.65f, .43f, .26f});
            }
        }
        if (detail)
            box(m, {0, .36, -.45}, {.55, .17, .5}, glass);
    } else if (kind == ActivityKind::Ferry) {
        for (int side : {-1, 1}) {
            tri(m, {0, .16, -1.7}, {side * 1.2, 0, .7}, {0, -.16, .9}, hull);
            tri(m, {0, .16, -1.7}, {0, .28, .8}, {side * 1.2, 0, .7}, hull);
            tri(m, {0, -.16, .9}, {side * 1.2, 0, .7}, {0, .28, .8}, hull * .65f);
        }
        if (detail)
            box(m, {0, .28, -.15}, {.3, .16, .8}, glass);
    } else if (kind == ActivityKind::Patrol) {
        for (int side : {-1, 1}) {
            box(m, {side * .65, 0, 0}, {.35, .42, 2.6}, hull);
            if (detail)
                box(m, {side * .65, .24, -.6}, {.15, .05, 1.2}, glow);
        }
        box(m, {}, {1.2, .25, .9}, hull);
        if (detail)
            box(m, {0, .22, -.2}, {.48, .22, .6}, glass);
    } else if (kind == ActivityKind::Survey) {
        appendSphere(m, {}, 1, hull, detail ? 8 : 4, detail ? 14 : 8);
        for (auto& v : m.vertices) {
            v.y *= .29f;
            v.z *= 1.4f;
        }
        if (detail) {
            ring(m, 1.05, .025, 0, glow, 32);
            box(m, {0, .36, -.3}, {.48, .16, .75}, glass);
        }
    } else {
        appendSphere(m, {}, 1, {1, 1, 1}, detail ? 6 : 3, detail ? 10 : 6);
        for (auto& v : m.vertices) {
            const float f = .85f + .14f * std::sin(v.x * 9 + v.z * 5);
            v.x *= f;
            v.y *= .66f;
        }
    }
    if (kind != ActivityKind::Harbour && kind != ActivityKind::Debris && detail)
        for (int side : {-1, 1})
            box(m, {side * .44, 0, 1.16}, {.25, .22, .14}, glow);
    m.compact();
    return m;
}
Mesh landableDebrisMesh(unsigned detail) {
    auto mesh = activityMesh(ActivityKind::Debris, std::max(1u, detail));
    for (auto& vertex : mesh.vertices)
        vertex.y = std::clamp(vertex.y, -float(debrisFaceHeight), float(debrisFaceHeight));
    return mesh;
}
} // namespace infinity

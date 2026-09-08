#include "core.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace cz {
namespace {
constexpr float kNear = 0.1f;
bool Finite(Vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
float Dot(Vec3 a, Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
Vec3 Add(Vec3 a, Vec3 b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
struct Projection {
    Vec3 forward{}, right{}, up{}, origin{}; float scale{}; Viewport viewport{};
    bool Init(const Camera& camera, Viewport view) {
        if (view.width<=0 || view.height<=0 || !Finite(camera.origin) || !Finite(camera.angles) ||
            !std::isfinite(camera.horizontalFov) || camera.horizontalFov<1 || camera.horizontalFov>=179) return false;
        constexpr float rad = 0.017453292519943295f;
        const float sp=std::sin(camera.angles.x*rad), cp=std::cos(camera.angles.x*rad);
        const float sy=std::sin(camera.angles.y*rad), cy=std::cos(camera.angles.y*rad);
        const float sr=std::sin(camera.angles.z*rad), cr=std::cos(camera.angles.z*rad);
        forward={cp*cy,cp*sy,-sp};
        right={-sr*sp*cy+cr*sy,-sr*sp*sy-cr*cy,-sr*cp};
        up={cr*sp*cy+sr*sy,cr*sp*sy-sr*cy,cr*cp};
        origin=camera.origin; viewport=view;
        scale=static_cast<float>(view.width)/(2*std::tan(camera.horizontalFov*rad/2));
        return true;
    }
    Vec3 Transform(Vec3 point) const {
        Vec3 d{point.x-origin.x,point.y-origin.y,point.z-origin.z};
        return {Dot(d,right),Dot(d,up),Dot(d,forward)};
    }
    Point Screen(Vec3 p) const {
        return {viewport.width*0.5f+p.x*scale/p.z, viewport.height*0.5f-p.y*scale/p.z};
    }
};
}
Camera ResolveProjectionCamera(Camera camera, Viewport view, bool widescreenFov) {
    if(!widescreenFov || view.width<=0 || view.height<=0 || !std::isfinite(camera.horizontalFov) ||
        camera.horizontalFov<1 || camera.horizontalFov>=179) return camera;
    const double aspect=static_cast<double>(view.width)/view.height;
    // Current Steam GoldSrc applies gl_widescreen_yfov only above 3:2.
    // The input is its 4:3 base FOV; preserve that vertical angle at wider ratios.
    if(aspect<=1.5) return camera;
    constexpr double halfDegreesToRadians=0.008726646259971648;
    camera.horizontalFov=static_cast<float>(2*std::atan(std::tan(camera.horizontalFov*halfDegreesToRadians)*aspect*0.75)/
        (2*halfDegreesToRadians));
    return camera;
}
bool ProjectPoint(const Camera& c, Vec3 p, Viewport v, Point& result) {
    Projection projection;
    if (!Finite(p) || !projection.Init(c,v)) return false;
    const Vec3 local=projection.Transform(p);
    if (!Finite(local) || local.z<kNear) return false;
    result=projection.Screen(local);
    return std::isfinite(result.x) && std::isfinite(result.y);
}
bool ProjectBox(const Camera& c, const Player& player, Viewport v, Rect& result) {
    Projection projection;
    if (!projection.Init(c,v) || !Finite(player.origin) || !Finite(player.mins) || !Finite(player.maxs) ||
        player.mins.x>=player.maxs.x || player.mins.y>=player.maxs.y || player.mins.z>=player.maxs.z) return false;
    Vec3 corners[8]{};
    float left=std::numeric_limits<float>::infinity(), top=left, right=-left, bottom=-left;
    auto include=[&](Vec3 point) {
        Point screen=projection.Screen(point);
        if (!std::isfinite(screen.x) || !std::isfinite(screen.y)) return;
        left=std::min(left,screen.x); right=std::max(right,screen.x);
        top=std::min(top,screen.y); bottom=std::max(bottom,screen.y);
    };
    for (int i=0;i<8;++i) {
        corners[i]=projection.Transform(Add(player.origin,{i&1?player.maxs.x:player.mins.x,i&2?player.maxs.y:player.mins.y,i&4?player.maxs.z:player.mins.z}));
        if (!Finite(corners[i])) return false;
        if (corners[i].z>=kNear) include(corners[i]);
    }
    for (int i=0;i<8;++i) for (int bit=1;bit<=4;bit*=2) if (!(i&bit)) {
        const Vec3 a=corners[i], b=corners[i|bit];
        if ((a.z<kNear)==(b.z<kNear)) continue;
        const float t=(kNear-a.z)/(b.z-a.z);
        include({a.x+t*(b.x-a.x),a.y+t*(b.y-a.y),kNear});
    }
    if (!std::isfinite(left) || right<0 || bottom<0 || left>=v.width || top>=v.height) return false;
    result={std::max(0.0f,left),std::max(0.0f,top),std::min(static_cast<float>(v.width),right),std::min(static_cast<float>(v.height),bottom)};
    return result.right-result.left>=1 && result.bottom-result.top>=1;
}
bool ValidSnapshot(const Snapshot& s, std::uint32_t pid, std::uint64_t now) {
    return s.magic==kMagic && s.version==kVersion && s.bytes==sizeof(Snapshot) && s.processId==pid &&
        s.timestampMs>0 && s.timestampMs<=now && now-s.timestampMs<=kStaleMs && s.playerCount<=kMaxPlayers &&
        static_cast<std::uint32_t>(s.status)<=static_cast<std::uint32_t>(SessionStatus::Spectating);
}
bool IsTarget(const Snapshot& s, const Player& p) {
    return s.status==SessionStatus::Active && s.localIndex>=1 && s.localIndex<=kMaxPlayers &&
        (s.localTeam==1 || s.localTeam==2) && p.index>=1 && p.index<=kMaxPlayers && p.index!=s.localIndex &&
        (p.team==1 || p.team==2) && p.team!=s.localTeam && (p.flags&(Alive|Bot))==(Alive|Bot) &&
        std::isfinite(p.health) && p.health>0 && std::isfinite(p.maxHealth) && p.maxHealth>0;
}
bool KeyToggle::Update(bool foreground,bool down) {
    const bool changed=foreground && wasForeground_ && down && !previous_;
    if (changed) enabled=!enabled;
    previous_=down; wasForeground_=foreground;
    return changed;
}
bool Switches::Update(bool foreground, const std::array<bool, 3>& down) {
    bool changed=false;
    if (foreground && wasForeground_) {
        if (down[0] && !previous_[0]) { enabled=!enabled; changed=true; }
        if (down[1] && !previous_[1]) { options.boxes=!options.boxes; changed=true; }
        if (down[2] && !previous_[2]) { options.health=!options.health; changed=true; }
    }
    previous_=down; wasForeground_=foreground;
    return changed;
}
}

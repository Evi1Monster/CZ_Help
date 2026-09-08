#include "core.h"
#include <cmath>
#include <iostream>
#include <limits>
int failures = 0;
void check(bool result, const char* name) { if (!result) { ++failures; std::cerr << "FAIL: " << name << '\n'; } }
bool near(float a, float b) { return std::abs(a-b) < 0.05f; }
int main() {
    cz::Camera c{}; cz::Point p{}; const cz::Viewport v{800,600};
    check(cz::ProjectPoint(c,{100,0,0},v,p) && near(p.x,400) && near(p.y,300),"ahead is center");
    check(cz::ProjectPoint(c,{100,-50,50},v,p) && near(p.x,600) && near(p.y,100),"right and up axes");
    check(!cz::ProjectPoint(c,{-100,0,0},v,p),"behind camera rejected");
    c.angles.y=90;
    check(cz::ProjectPoint(c,{0,100,0},v,p) && near(p.x,400),"yaw rotates projection");
    c.angles={}; c.horizontalFov=0;
    check(!cz::ProjectPoint(c,{100,0,0},v,p),"zero FOV rejected");
    c.horizontalFov=90;
    check(!cz::ProjectPoint(c,{100,0,0},{0,600},p),"zero viewport rejected");
    const cz::Viewport wide{1920,1080};
    auto corrected=cz::ResolveProjectionCamera(c,wide,true);
    check(near(corrected.horizontalFov,106.2602f),"Steam widescreen expands base 90 degree FOV");
    check(cz::ProjectPoint(corrected,{100,-50,0},wide,p) && near(p.x,1320),"off-center right target uses widescreen scale");
    check(cz::ProjectPoint(corrected,{100,50,0},wide,p) && near(p.x,600),"off-center left target uses widescreen scale");
    check(cz::ProjectPoint(corrected,{100,0,50},wide,p) && near(p.y,180),"widescreen keeps vertical scale");
    check(cz::ProjectPoint(corrected,{100,0,0},wide,p) && near(p.x,960) && near(p.y,540),"FOV correction keeps center stable");
    corrected=cz::ResolveProjectionCamera(c,wide,false);
    check(near(corrected.horizontalFov,90) && cz::ProjectPoint(corrected,{100,-50,0},wide,p) && near(p.x,1440),"disabled widescreen mode retains legacy projection");
    corrected=cz::ResolveProjectionCamera(c,v,true);
    check(near(corrected.horizontalFov,90),"4:3 baseline unchanged");
    corrected=cz::ResolveProjectionCamera(c,{1500,1000},true);
    check(near(corrected.horizontalFov,90),"Steam leaves exactly 3:2 unchanged");
    corrected=cz::ResolveProjectionCamera(c,{1501,1000},true);
    check(corrected.horizontalFov>96.7f,"Steam applies widescreen correction strictly above 3:2");
    corrected=cz::ResolveProjectionCamera(c,{1280,1024},true);
    check(near(corrected.horizontalFov,90),"narrow displays retain base FOV");
    c.horizontalFov=40;
    corrected=cz::ResolveProjectionCamera(c,wide,true);
    check(near(corrected.horizontalFov,51.774f),"zoom FOV uses same aspect correction");
    c.horizontalFov=90;
    c.angles={25,70,0};
    corrected=cz::ResolveProjectionCamera(c,wide,true);
    check(cz::ProjectPoint(corrected,{0.0f,100.0f,0.0f},wide,p) && near(p.x,670.856f) && near(p.y,204.258f),"pitched and yawed camera shares corrected off-center scale");
    c.angles={};
    check(!cz::ProjectPoint(c,{std::numeric_limits<float>::quiet_NaN(),0,0},v,p),"NaN point rejected");
    cz::Player target{}; target.origin={100,0,0}; target.mins={-10,-10,-20}; target.maxs={10,10,20}; cz::Rect r{};
    check(cz::ProjectBox(c,target,v,r) && near(r.left,355.5556f) && near(r.top,211.1111f) && near(r.right,444.4444f),"world bounds project to screen rectangle");
    target.origin={5,0,0};
    check(cz::ProjectBox(c,target,v,r) && r.left>=0 && r.right<=800 && r.top>=0 && r.bottom<=600,"near-plane crossing clipped");
    target.origin={-100,0,0};
    check(!cz::ProjectBox(c,target,v,r),"behind bounds rejected");
    cz::KeyToggle bhop;
    check(!bhop.enabled,"bhop starts disabled");
    bhop.Update(true,false);
    check(bhop.Update(true,true) && bhop.enabled,"bhop key enables");
    check(!bhop.Update(true,true) && bhop.enabled,"bhop held key toggles once");
    bhop.Update(false,false);
    check(!bhop.Update(false,true) && bhop.enabled,"bhop ignores background key");
    check(!bhop.Update(true,true) && bhop.enabled,"bhop ignores held key on focus regain");
    bhop.Update(true,false);
    check(bhop.Update(true,true) && !bhop.enabled,"bhop next press disables");
    cz::Switches s;
    s.Update(true,{false,false,false});
    check(s.Update(true,{true,false,false}) && s.enabled,"master key enables");
    check(!s.Update(true,{true,false,false}) && s.enabled,"held key only toggles once");
    s.Update(true,{false,false,false}); s.Update(true,{false,true,false});
    check(!s.options.boxes && s.options.health,"box key independent");
    s.Update(true,{false,false,false}); s.Update(true,{true,false,false});
    check(!s.enabled && !s.options.boxes && s.options.health,"master preserves individual preferences");
    s.Update(false,{false,false,true});
    check(!s.Update(true,{false,false,true}) && s.options.health,"held key on focus regain ignored");
    s.Update(true,{false,false,false}); s.Update(true,{false,false,true});
    check(!s.options.health,"health key independent");
    cz::Snapshot snap{}; snap.bytes=sizeof(snap); snap.processId=42; snap.timestampMs=1000; snap.status=cz::SessionStatus::Active; snap.localIndex=1; snap.localTeam=1;
    check(cz::ValidSnapshot(snap,42,1250),"fresh snapshot accepted");
    check(!cz::ValidSnapshot(snap,42,1501),"stale snapshot rejected");
    check(!cz::ValidSnapshot(snap,43,1250),"other PID rejected");
    check(!cz::ValidSnapshot(snap,42,999),"future timestamp rejected");
    snap.version=999; check(!cz::ValidSnapshot(snap,42,1250),"wrong version rejected"); snap.version=cz::kVersion;
    snap.playerCount=33; check(!cz::ValidSnapshot(snap,42,1250),"too many players rejected"); snap.playerCount=1;
    target.index=2; target.team=2; target.flags=cz::Alive|cz::Bot; target.health=67; target.maxHealth=100;
    check(cz::IsTarget(snap,target),"enemy living BOT selected");
    target.team=1; check(!cz::IsTarget(snap,target),"teammate excluded"); target.team=2;
    target.health=0; check(!cz::IsTarget(snap,target),"dead BOT excluded"); target.health=67;
    target.flags=cz::Alive; check(!cz::IsTarget(snap,target),"human excluded");
    std::cout << (failures ? "FAILED " : "PASSED ") << failures << " failures\n";
    return failures ? 1 : 0;
}

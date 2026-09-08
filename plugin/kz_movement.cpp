#include "kz_movement.h"
#include "in_buttons.h"
#include <cmath>
namespace cz {
void JumpAssist::Reset() { *this={}; }
namespace {
float Wrap(float angle) { return std::remainder(angle,360.0f); }
void Strafe(playermove_t& m,float direction) {
    m.cmd.forwardmove=0;
    m.cmd.sidemove=direction*400.0f;
    m.cmd.buttons&=~(IN_MOVELEFT|IN_MOVERIGHT);
    m.cmd.buttons|=direction<0?IN_MOVELEFT:IN_MOVERIGHT;
    m.cmd.buttons&=~(IN_FORWARD|IN_BACK);
}
}
JumpResult JumpAssist::Apply(playermove_t* m,bool authorized,bool spaceHeld,JumpMode mode,std::uint32_t generation) {
    JumpResult result{};
    if(!m || !authorized || !spaceHeld || !ValidJumpMode(mode) || !m->server || m->player_index!=0 ||
        m->movetype!=MOVETYPE_WALK || m->waterjumptime!=0 || m->dead || m->deadflag!=DEAD_NO || m->spectator ||
        (m->flags&(FL_FROZEN|FL_ONTRAIN|FL_WATERJUMP)) || !(m->cmd.buttons&IN_JUMP) ||
        !std::isfinite(m->cmd.viewangles[0]) || !std::isfinite(m->cmd.viewangles[1]) || !std::isfinite(m->cmd.viewangles[2]) ||
        !std::isfinite(m->cmd.forwardmove) || !std::isfinite(m->cmd.sidemove)) {
        Reset(); return result;
    }
    if(!running_ || mode_!=mode || generation_!=generation) {
        Reset(); running_=true; mode_=mode; generation_=generation;
    }
    if(mode==JumpMode::AutoJump) { result.rearmed=ApplyBhop(m,true,true); return result; }
    // Zero-duration commands must not advance or generate a scripted action.
    const unsigned dt=m->cmd.msec;
    if(!dt) return result;
    const bool ground=(m->flags&FL_ONGROUND)!=0;
    const float rawYaw=m->cmd.viewangles[1];
    const float yawDelta=haveYaw_?Wrap(rawYaw-previousYaw_):0;
    previousYaw_=rawYaw; haveYaw_=true;
    result.rearmed=ApplyBhop(m,true,true);
    if(!ground && std::fabs(yawDelta)>0.01f && std::fabs(yawDelta)<90.0f)
        Strafe(*m,yawDelta>0?-1.0f:1.0f);
    return result;
}
}
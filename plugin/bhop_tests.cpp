#include "bhop.h"
#include "in_buttons.h"
#include <cstdio>
#include <cstring>
#include <memory>

namespace {
int failures{};
auto movement=std::make_unique<playermove_t>();
void Check(bool value,const char* label) { if(!value) { ++failures; std::fprintf(stderr,"FAIL: %s\n",label); } }
void Seed() {
    std::memset(movement.get(),0,sizeof(*movement));
    movement->server=TRUE; movement->player_index=0; movement->movetype=MOVETYPE_WALK;
    movement->onground=0; movement->flags=FL_ONGROUND;
    movement->cmd.buttons=IN_JUMP|IN_ATTACK; movement->oldbuttons=IN_JUMP|IN_DUCK;
    movement->cmd.forwardmove=180; movement->cmd.sidemove=-90;
    movement->velocity[0]=123; movement->velocity[1]=-234; movement->velocity[2]=0;
}
void Unchanged(const char* label,bool allowed=true,bool space=true) {
    const auto before=std::make_unique<playermove_t>();
    std::memcpy(before.get(),movement.get(),sizeof(*movement));
    Check(!cz::ApplyBhop(movement.get(),allowed,space) && std::memcmp(before.get(),movement.get(),sizeof(*movement))==0,label);
}
}
int main() {
    Seed(); auto expected=std::make_unique<playermove_t>();
    std::memcpy(expected.get(),movement.get(),sizeof(*movement)); expected->oldbuttons=IN_DUCK;
    Check(cz::ApplyBhop(movement.get(),true,true) && std::memcmp(expected.get(),movement.get(),sizeof(*movement))==0,
        "held space on ground rearms jump without changing command, velocity, or other old buttons");
    movement->oldbuttons=IN_JUMP|IN_DUCK;
    Check(cz::ApplyBhop(movement.get(),true,true) && movement->oldbuttons==IN_DUCK,"held space rearms after another landing");
    Seed(); Unchanged("disabled unchanged",false);
    Seed(); Unchanged("released space unchanged",true,false);
    Seed(); movement->cmd.buttons=IN_ATTACK; Unchanged("game has no jump command");
    Seed(); movement->onground=-1;
    Check(cz::ApplyBhop(movement.get(),true,true),"current ground flags override stale airborne result");
    Seed(); movement->flags&=~FL_ONGROUND; Unchanged("current airborne flags override stale ground result");
    Seed(); movement->waterlevel=2;
    Check(cz::ApplyBhop(movement.get(),true,true),"stale water calculation cannot suppress a dry host");
    Seed(); movement->waterjumptime=10; Unchanged("water jump unchanged");
    Seed(); movement->movetype=MOVETYPE_FLY; Unchanged("ladder unchanged");
    Seed(); movement->movetype=MOVETYPE_NOCLIP; Unchanged("noclip unchanged");
    Seed(); movement->dead=TRUE; Unchanged("dead unchanged");
    Seed(); movement->deadflag=DEAD_DYING; Unchanged("dying unchanged");
    Seed(); movement->flags|=FL_WATERJUMP; Unchanged("water jump flag unchanged");
    Seed(); movement->spectator=TRUE; Unchanged("spectator unchanged");
    Seed(); movement->flags|=FL_FROZEN; Unchanged("frozen unchanged");
    Seed(); movement->flags|=FL_ONTRAIN; Unchanged("train unchanged");
    Seed(); movement->server=FALSE; Unchanged("client prediction unchanged");
    Seed(); movement->player_index=1; Unchanged("other player unchanged");
    Seed(); movement->oldbuttons=IN_DUCK; Unchanged("already released latch unchanged");
    Check(!cz::ApplyBhop(nullptr,true,true),"null movement ignored");
    std::printf("bhop movement: %d failures\n",failures); return failures?1:0;
}

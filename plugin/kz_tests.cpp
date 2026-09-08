#include "kz_movement.h"
#include "in_buttons.h"
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <memory>
namespace {
using Mode=cz::JumpMode;
int failures{};
void Check(bool ok,const char* name) { if(!ok) { ++failures; std::printf("FAIL: %s\n",name); } }
struct Fixture {
    cz::JumpAssist assist;
    std::unique_ptr<playermove_t> m=std::make_unique<playermove_t>();
    Fixture() { m->server=TRUE; m->movetype=MOVETYPE_WALK; m->velocity[0]=250; m->gravity=1; m->origin[2]=36; }
    cz::JumpResult Step(Mode mode,bool ground=true,float yaw=0,bool allowed=true,bool held=true,unsigned generation=1) {
        m->flags=ground?FL_ONGROUND:0; m->cmd={}; m->cmd.msec=10;
        m->cmd.buttons=IN_JUMP|IN_ATTACK|IN_DUCK; m->oldbuttons=IN_JUMP|IN_DUCK;
        m->cmd.forwardmove=200; m->cmd.viewangles[0]=12; m->cmd.viewangles[1]=yaw;
        return assist.Apply(m.get(),allowed,held,mode,generation);
    }
};
void RetainedModes() {
    Fixture f; auto result=f.Step(Mode::AutoJump);
    Check(result.rearmed && f.m->oldbuttons==IN_DUCK && f.m->cmd.forwardmove==200,"original autojump retained");
    f.Step(Mode::AutoJump,false,4);
    Check(f.m->cmd.sidemove==0 && f.m->cmd.forwardmove==200 && f.m->cmd.viewangles[1]==4,"autojump leaves manual steering unchanged");
    result=f.Step(Mode::BhopJump,true,0);
    Check(result.rearmed,"BHopJump rearms on landing");
    f.Step(Mode::BhopJump,false,4);
    Check(f.m->cmd.sidemove<0 && f.m->cmd.forwardmove==0 && f.m->cmd.viewangles[1]==4,"manual left yaw pairs left strafe without changing view");
    f.Step(Mode::BhopJump,false,-4); Check(f.m->cmd.sidemove>0,"manual right yaw pairs right strafe");
    f.Step(Mode::BhopJump,false,-4); Check(f.m->cmd.sidemove==0,"stationary view adds no strafe");
    f.Step(Mode::BhopJump,false,179); f.Step(Mode::BhopJump,false,-179);
    Check(f.m->cmd.sidemove<0,"yaw wrap uses shortest direction");
    Check((f.m->cmd.buttons&IN_DUCK) && (f.m->oldbuttons&IN_DUCK),"manual crouch preserved");
}
void RemovedModes() {
    for(unsigned id:{1u,2u,3u,5u,99u}) {
        Fixture f; f.Step(Mode::AutoJump);
        f.m->oldbuttons=IN_JUMP|IN_DUCK;
        auto before=std::make_unique<playermove_t>(); std::memcpy(before.get(),f.m.get(),sizeof(*f.m));
        Check(!f.assist.Apply(f.m.get(),true,true,static_cast<Mode>(id),1).rearmed &&
            std::memcmp(before.get(),f.m.get(),sizeof(*f.m))==0,"removed mode cannot alter movement or view");
    }
}
void StopsAndInvariants() {
    for(auto mode:{Mode::AutoJump,Mode::BhopJump}) {
        Fixture f; f.Step(mode); f.Step(mode,false,4);
        f.Step(mode,false,15,true,false);
        Check(f.m->cmd.sidemove==0 && f.m->cmd.forwardmove==200,"released space returns raw command");
        f.Step(mode,false,30);
        Check(f.m->cmd.sidemove==0,"space resume does not reuse previous yaw");
        f.Step(mode,false,40,false,true);
        Check(f.m->cmd.sidemove==0,"inactive session cannot steer");
        f.Step(mode,false,45,true,true,2);
        Check(f.m->cmd.sidemove==0,"new generation clears yaw history");
        f.m->cmd.buttons=IN_JUMP|IN_ATTACK|IN_DUCK;
        auto before=std::make_unique<playermove_t>(); std::memcpy(before.get(),f.m.get(),sizeof(*f.m));
        f.assist.Apply(f.m.get(),true,true,mode,2);
        Check(std::memcmp(before->cmd.viewangles,f.m->cmd.viewangles,sizeof(f.m->cmd.viewangles))==0,"all view angles unchanged");
        before->cmd=f.m->cmd; before->oldbuttons=f.m->oldbuttons;
        Check(std::memcmp(before.get(),f.m.get(),sizeof(*f.m))==0,"entire SDK physics unchanged outside command and button latch");
        for(int gate=0;gate<8;++gate) {
            Fixture blocked; blocked.Step(mode,false,0); blocked.m->cmd.viewangles[1]=5;
            switch(gate) {
            case 0: blocked.m->dead=TRUE; break;
            case 1: blocked.m->movetype=MOVETYPE_FLY; break;
            case 2: blocked.m->flags|=FL_FROZEN; break;
            case 3: blocked.m->server=FALSE; break;
            case 4: blocked.m->player_index=1; break;
            case 5: blocked.m->spectator=TRUE; break;
            case 6: blocked.m->cmd.buttons&=~IN_JUMP; break;
            case 7: blocked.m->waterjumptime=10; break;
            }
            std::memcpy(before.get(),blocked.m.get(),sizeof(*blocked.m));
            blocked.assist.Apply(blocked.m.get(),true,true,mode,1);
            Check(std::memcmp(before.get(),blocked.m.get(),sizeof(*blocked.m))==0,"ineligible movement unchanged");
        }
    }
    Fixture f; f.Step(Mode::BhopJump,false,0); f.Step(Mode::BhopJump,false,5); f.Step(Mode::AutoJump,false,10);
    Check(f.m->cmd.sidemove==0,"mode switch drops strafe history");
    for(int axis:{0,1,2}) {
        Fixture invalid; invalid.Step(Mode::BhopJump,false);
        invalid.m->cmd.viewangles[axis]=std::numeric_limits<float>::infinity();
        auto before=std::make_unique<playermove_t>(); std::memcpy(before.get(),invalid.m.get(),sizeof(*invalid.m));
        invalid.assist.Apply(invalid.m.get(),true,true,Mode::BhopJump,1);
        Check(std::memcmp(before.get(),invalid.m.get(),sizeof(*invalid.m))==0,"nonfinite view is rejected");
    }
}
}
int main() { RetainedModes(); RemovedModes(); StopsAndInvariants(); std::printf("Movement modes: %d failures\n",failures); return failures?1:0; }
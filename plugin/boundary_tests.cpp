// SDK ABI boundary fixtures. These are not a claim of live game verification.
#include "plugin.cpp"
#include "bhop.h"
#include "bhop_control.h"
#include "in_buttons.h"
#include <cstdlib>
#include <limits>
#include <memory>

namespace {
edict_t entities[34]{};
globalvars_t globals{};
meta_globals_t meta{};
float lan = 1;
int dedicated = 0;
int failures{};
char modelName[32] = "unknown";
edict_t* Entity(int i) { return i >= 0 && i < 34 ? &entities[i] : nullptr; }
int Index(const edict_t* e) { return static_cast<int>(e - entities); }
float Cvar(const char*) { return lan; }
int Dedicated() { return dedicated; }
char* Info(edict_t*) { static char buffer[] = ""; return buffer; }
char* InfoValue(char*, char*) { return modelName; }
void Check(bool condition, const char* name) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", name); ++failures; }
}
void Seed() {
    std::memset(entities, 0, sizeof(entities));
    std::memset(teams, 0, sizeof(teams)); std::memset(teamKnown, 0, sizeof(teamKnown));
    std::memset(remoteAddress, 0, sizeof(remoteAddress));
    globals.maxClients = 32; gpGlobals = &globals; gpMetaGlobals = &meta;
    g_engfuncs.pfnPEntityOfEntIndex = Entity; g_engfuncs.pfnIndexOfEdict = Index;
    g_engfuncs.pfnCVarGetFloat = Cvar; g_engfuncs.pfnIsDedicatedServer = Dedicated;
    g_engfuncs.pfnGetInfoKeyBuffer = Info; g_engfuncs.pfnInfoKeyValue = InfoValue;
    mapActive = true; lan = 1; dedicated = 0; teamInfoId = 77;
    std::strcpy(modelName, "unknown");
    for (int i : {1, 2, 3}) {
        auto& v = entities[i].v; v.flags = FL_CLIENT | (i == 1 ? 0 : FL_FAKECLIENT);
        v.health = 100; v.max_health = 100; v.deadflag = DEAD_NO;
        v.origin = Vector(100.0f * i, 20, 30); v.mins = Vector(-16, -16, -36); v.maxs = Vector(16, 16, 36);
        teams[i] = i == 2 ? 1 : 2; teamKnown[i] = true;
    }
    entities[1].v.view_ofs = Vector(0, 0, 28); entities[1].v.v_angle = Vector(10, 20, 0);
    entities[1].v.punchangle = Vector(1, 2, 0); entities[1].v.fov = 40;
    entities[2].v.health = 73.5f;
}
void TestBhop(DLL_FUNCTIONS& api) {
    Check(api.pfnPM_Move!=nullptr,"pre movement hook installed");
    if (!api.pfnPM_Move) return;
    Seed();
    const DWORD pid=GetCurrentProcessId();
    cz::BhopControlMailbox writer; Check(writer.Open(pid,L"BhopControl",true),"bhop control writer opens");
    cz::BhopControl control{}; control.processId=pid; control.flags=cz::BhopEnabled|cz::BhopSpaceHeld;
    auto movement=std::make_unique<playermove_t>();
    auto reset=[&]() {
        std::memset(movement.get(),0,sizeof(*movement));
        movement->server=TRUE; movement->movetype=MOVETYPE_WALK; movement->flags=FL_ONGROUND;
        movement->onground=-1; movement->waterlevel=2; // Previous BOT's calculated output must be ignored.
        movement->cmd.buttons=IN_JUMP|IN_ATTACK; movement->oldbuttons=IN_JUMP|IN_DUCK;
    };
    auto publish=[&]() { control.timestampMs=GetTickCount64(); Check(writer.Publish(control),"bhop control published"); };
    auto unchanged=[&](const char* name,int server=TRUE) {
        auto before=std::make_unique<playermove_t>(); std::memcpy(before.get(),movement.get(),sizeof(*movement));
        api.pfnPM_Move(movement.get(),server);
        Check(std::memcmp(before.get(),movement.get(),sizeof(*movement))==0 && meta.mres==MRES_IGNORED,name);
    };
    reset(); publish(); api.pfnPM_Move(movement.get(),TRUE);
    Check(movement->oldbuttons==IN_DUCK && meta.mres==MRES_IGNORED,"fresh control rearms local host and forwards original physics");
    StartFrame(); cz::BhopStatusMailbox statusReader; cz::BhopStatus status{};
    Check(statusReader.Open(pid,L"BhopStatus",false) && statusReader.Read(status) &&
        cz::ValidBhopStatus(status,pid,GetTickCount64()) && (status.flags&cz::BhopAvailable) && status.rearmedCommands==1,
        "plugin reports fresh bhop capability and successful rearm");
    reset(); control.flags=0; publish(); unchanged("off control stops bhop");
    control.flags=cz::BhopEnabled; publish(); unchanged("released physical space stops bhop");
    control.flags=cz::BhopEnabled|cz::BhopSpaceHeld;
    control.timestampMs=GetTickCount64()-cz::kBhopControlStaleMs-1; writer.Publish(control);
    unchanged("expired control stops bhop");
    publish(); movement->cmd.buttons=IN_ATTACK; unchanged("released game command stops bhop despite cached held space");
    reset(); publish(); unchanged("non-server callback unchanged",FALSE);
    reset(); movement->player_index=1; unchanged("BOT movement unchanged");
    reset(); entities[3].v.flags=FL_CLIENT; publish(); unchanged("live second human stops bhop");
    Seed(); reset(); entities[1].v.health=0; publish(); unchanged("live host death stops bhop");
    Seed(); reset(); lan=0; publish(); unchanged("live LAN setting change stops bhop");
    Seed(); reset(); remoteAddress[1]=true; publish(); unchanged("remote host stops bhop");
    Seed(); reset(); entities[1].v.waterlevel=1; publish(); unchanged("current host water level stops bhop");
    Seed(); reset(); publish(); Clear(); unchanged("map deactivation stops bhop");
}
void TestJumpModes(DLL_FUNCTIONS& api) {
    Seed();
    DLL_FUNCTIONS post{}; int version=INTERFACE_VERSION; GetEntityAPI2_Post(&post,&version);
    Check(post.pfnPlayerPostThink==nullptr,"automatic view synchronization hook removed");
    cz::BhopControlMailbox writer; writer.Open(GetCurrentProcessId(),L"BhopControl",true);
    cz::BhopControl control{}; control.processId=GetCurrentProcessId(); control.flags=3;
    control.mode=cz::JumpMode::BhopJump; control.generation=123;
    auto movement=std::make_unique<playermove_t>();
    auto reset=[&](float yaw=0) {
        std::memset(movement.get(),0,sizeof(*movement)); movement->server=TRUE; movement->movetype=MOVETYPE_WALK;
        movement->cmd.buttons=IN_JUMP|IN_ATTACK; movement->oldbuttons=IN_JUMP; movement->cmd.msec=10;
        movement->cmd.viewangles[0]=12; movement->cmd.viewangles[1]=yaw;
        entities[1].v.fixangle=0; control.timestampMs=GetTickCount64(); writer.Publish(control);
    };
    reset(); api.pfnPM_Move(movement.get(),TRUE);
    reset(4); api.pfnPM_Move(movement.get(),TRUE);
    Check(movement->cmd.sidemove<0 && movement->cmd.viewangles[1]==4 && entities[1].v.fixangle==0 && entities[1].v.v_angle.y==20,
        "BHopJump pairs manual steering without forcing host view");
    reset(8); entities[1].v.waterlevel=1; api.pfnPM_Move(movement.get(),TRUE);
    Check(movement->cmd.sidemove==0,"water stops BHopJump steering");
    entities[1].v.waterlevel=0; reset(12); movement->player_index=1; api.pfnPM_Move(movement.get(),TRUE);
    Check(movement->cmd.sidemove==0,"BOT never receives steering");
    for(unsigned removed:{1u,2u,3u,5u}) {
        control.mode=static_cast<cz::JumpMode>(removed); reset(); movement->flags=FL_ONGROUND;
        auto before=std::make_unique<playermove_t>(); std::memcpy(before.get(),movement.get(),sizeof(*movement));
        api.pfnPM_Move(movement.get(),TRUE);
        Check(std::memcmp(before.get(),movement.get(),sizeof(*movement))==0,"legacy removed mode cannot execute through plugin");
    }
    control.mode=cz::JumpMode::BhopJump;
    reset(0); api.pfnPM_Move(movement.get(),TRUE);
    reset(4); api.pfnPM_Move(movement.get(),TRUE);
    control.flags=0; control.timestampMs=GetTickCount64(); writer.Publish(control); StartFrame();
    control.flags=3; reset(20); api.pfnPM_Move(movement.get(),TRUE);
    Check(movement->cmd.sidemove==0,"off interval without PM_Move clears steering history");
    reset(24); api.pfnPM_Move(movement.get(),TRUE);
    Check(movement->cmd.sidemove<0,"steering resumes from new input after interruption");
    Clear();
}
}
int main() {
    DLL_FUNCTIONS movementApi{}; int movementVersion=INTERFACE_VERSION;
    GetEntityAPI2(&movementApi,&movementVersion); TestBhop(movementApi);
    TestJumpModes(movementApi);
    Seed(); auto s = BuildSnapshot();
    Check(s.status == cz::SessionStatus::Active && s.playerCount == 1 && s.players[0].index == 2, "enemy alive BOT only");
    Check(s.players[0].health == 73.5f, "real entvars health is not inferred");
    Check(s.camera.origin.z == 58 && s.camera.angles.x == 11 && s.camera.angles.y == 22 && s.camera.horizontalFov == 40, "camera view offset punch and FOV");
    Check(s.bytes == sizeof(s) && s.processId == GetCurrentProcessId() && s.timestampMs != 0, "protocol identity");
    entities[2].v.health = 0; Check(BuildSnapshot().playerCount == 0, "dead BOT omitted");
    Seed(); entities[1].v.health = 0; Check(BuildSnapshot().status == cz::SessionStatus::Spectating, "dead host disabled");
    Seed(); entities[1].v.iuser1 = 4; Check(BuildSnapshot().status == cz::SessionStatus::Spectating, "observer host disabled");
    Seed(); entities[3].v.flags = FL_CLIENT; Check(BuildSnapshot().status == cz::SessionStatus::NotLocal, "second human disabled");
    Seed(); dedicated = 1; Check(BuildSnapshot().status == cz::SessionStatus::NotLocal, "dedicated disabled");
    Seed(); lan = 0; Check(BuildSnapshot().status == cz::SessionStatus::NotLocal, "Internet enabled server disabled");
    Seed(); remoteAddress[1] = true; Check(BuildSnapshot().status == cz::SessionStatus::NotLocal, "remote address disabled");
    Seed(); entities[1].free = TRUE; Check(BuildSnapshot().status == cz::SessionStatus::NoPlayer, "free host omitted");
    Seed(); teamKnown[2] = false; Check(BuildSnapshot().playerCount == 0, "unknown model and team omitted");
    std::strcpy(modelName, "militia"); entities[2].v.team = 2;
    Check(BuildSnapshot().playerCount == 1 && BuildSnapshot().players[0].team == 1, "CZ militia model maps T without entvars team assumption");
    Seed(); teamKnown[1] = false; Check(BuildSnapshot().status == cz::SessionStatus::Spectating, "unknown host team disabled");
    Seed(); entities[2].v.origin.x = std::numeric_limits<float>::quiet_NaN(); Check(BuildSnapshot().playerCount == 0, "NaN BOT omitted");
    Seed(); entities[1].v.fov = std::numeric_limits<float>::infinity(); Check(BuildSnapshot().status != cz::SessionStatus::Active, "invalid FOV disabled");
    Seed(); entities[2].v.mins.z = entities[2].v.maxs.z; Check(BuildSnapshot().playerCount == 0, "degenerate hull omitted");
    Seed(); entities[2].v.mins.z = -18; entities[2].v.maxs.z = 18; Check(BuildSnapshot().players[0].maxs.z == 18, "duck hull preserved");
    Seed(); MessageBegin(MSG_ALL, 77, nullptr, nullptr); WriteByte(2); WriteString("CT"); MessageEnd();
    Check(BuildSnapshot().playerCount == 0 && gpMetaGlobals->mres == MRES_IGNORED, "TeamInfo updates team without suppressing engine");
    MessageBegin(MSG_ALL, 77, nullptr, nullptr); WriteByte(2); WriteString("SPECTATOR"); MessageEnd();
    Check(teamKnown[2] && teams[2] == 0, "spectator message blocks model fallback");
    MessageBegin(MSG_ALL, 77, nullptr, nullptr); WriteByte(33); WriteString("CT"); MessageEnd();
    Check(teams[2] == 0, "out of range message slot ignored");
    Seed(); Clear(); Check(BuildSnapshot().status == cz::SessionStatus::Waiting && !teamKnown[2] && lastSnapshot.playerCount == 0, "map clear invalidates shared snapshot and teams");
    DLL_FUNCTIONS api{}; int version = 0;
    Check(!GetEntityAPI2(&api, &version) && version == INTERFACE_VERSION, "SDK version negotiation");
    Check(GetEntityAPI2(&api, &version) && api.pfnClientConnect && !api.pfnStartFrame, "pre hooks installed");
    Check(GetEntityAPI2_Post(&api, &version) && api.pfnStartFrame && api.pfnServerActivate, "post hooks installed");
    enginefuncs_t engine{}; engine.pfnPEntityOfEntIndexAllEntities = Entity;
    version = ENGINE_INTERFACE_VERSION;
    Check(GetEngineFunctions(&engine, &version) && engine.pfnMessageBegin &&
        engine.pfnPEntityOfEntIndexAllEntities == Entity, "legacy engine table tail is not overwritten");
    GiveFnptrsToDll(&engine, &globals);
    Check(g_engfuncs.pfnPEntityOfEntIndexAllEntities == nullptr && g_engfuncs.pfnMessageBegin == MessageBegin,
        "legacy engine table tail is not read");
    plugin_info_t* info{};
    Check(!Meta_Query("4:13", &info, nullptr) && Meta_Query("5:13", &info, nullptr) && info == &Plugin_info, "Metamod version negotiation");
    std::printf("plugin boundary fixtures: %s (%d failures); no live game was used\n", failures ? "FAIL" : "PASS", failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}

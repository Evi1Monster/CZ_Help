// SPDX-License-Identifier: GPL-2.0-or-later
// Uses the Metamod HL Engine linking exception; see third_party/metamod/GPL.txt
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include "sdk_bridge.h"
#include "channel.h"
#include "render_hook.h"
#include "bhop.h"
#include "bhop_control.h"
#include "kz_movement.h"

enginefuncs_t g_engfuncs{};
globalvars_t* gpGlobals{};
meta_globals_t* gpMetaGlobals{};
gamedll_funcs_t* gpGamedllFuncs{};
mutil_funcs_t* gpMetaUtilFuncs{};
plugin_info_t Plugin_info{META_INTERFACE_VERSION, "CZ Help Offline BOT Data", "1.4.1",
    "2026-09-07", "CZ Help", "", "CZHELP", PT_STARTUP, PT_NEVER};

namespace {
static_assert(sizeof(void*) == 4, "GoldSrc Metamod is x86");
// The current Valve SDK adds a trailing function without changing interface 138.
// Released Metamod binaries use the table ending at pfnCheckParm. Do not
// read or overwrite the newer tail when exchanging tables with Metamod.
constexpr std::size_t engineAbiBytes = offsetof(enginefuncs_t, pfnPEntityOfEntIndexAllEntities);
cz::WriteChannel channel;
cz::Snapshot lastSnapshot{};
cz::BhopControlMailbox bhopControl;
cz::BhopStatusMailbox bhopStatus;
cz::BhopControl latestBhop{};
cz::JumpAssist jumpAssist;
bool bhopControlOpen{},bhopStatusOpen{};
std::uint64_t nextBhopOpen{};
std::uint32_t rearmedCommands{};
unsigned teams[33]{};
bool teamKnown[33]{};
bool remoteAddress[33]{};
bool mapActive{};
bool channelOpen{};
unsigned long long droppedFrames{};
int teamInfoId{};
struct Message { bool team{}; int field{}, player{}; unsigned value{}; bool valid{}; } message;

cz::Vec3 Copy(const Vector& v) { return {v.x, v.y, v.z}; }
bool Finite(const Vector& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool Slot(int n) { return n >= 1 && n <= 32; }
bool Client(const edict_t* e) { return e && !e->free && (e->v.flags & FL_CLIENT) != 0; }
bool Alive(const edict_t* e) {
    return Client(e) && e->v.deadflag == DEAD_NO && std::isfinite(e->v.health) && e->v.health > 0;
}
unsigned TeamName(const char* name) {
    if (!name) return 0;
    if (_stricmp(name, "TERRORIST") == 0) return 1;
    if (_stricmp(name, "CT") == 0) return 2;
    return 0;
}
unsigned Team(edict_t* e, int slot) {
    if (teamKnown[slot]) return teams[slot]; // Includes explicit UNASSIGNED / SPECTATOR.
    if (!g_engfuncs.pfnGetInfoKeyBuffer || !g_engfuncs.pfnInfoKeyValue) return 0;
    char modelKey[] = "model";
    char* info = g_engfuncs.pfnGetInfoKeyBuffer(e);
    const char* model = info ? g_engfuncs.pfnInfoKeyValue(info, modelKey) : nullptr;
    if (!model) return 0;
    // Stock Counter-Strike / Condition Zero player model identifiers.
    for (const char* t : {"terror", "leet", "arctic", "guerilla", "militia"})
        if (_stricmp(model, t) == 0) return 1;
    for (const char* ct : {"urban", "gsg9", "sas", "gign", "spetsnaz"})
        if (_stricmp(model, ct) == 0) return 2;
    return 0;
}
cz::Snapshot Empty(cz::SessionStatus status) {
    cz::Snapshot s{};
    s.bytes = sizeof(s); s.processId = GetCurrentProcessId();
    s.timestampMs = GetTickCount64(); s.status = status;
    return s;
}
cz::Snapshot BuildSnapshot() {
    auto s = Empty(cz::SessionStatus::Waiting);
    if (!mapActive || !gpGlobals) return s;
    if (!g_engfuncs.pfnIsDedicatedServer || g_engfuncs.pfnIsDedicatedServer() ||
        !g_engfuncs.pfnCVarGetFloat || g_engfuncs.pfnCVarGetFloat("sv_lan") != 1.0f) {
        s.status = cz::SessionStatus::NotLocal; return s;
    }
    if (!g_engfuncs.pfnPEntityOfEntIndex) return s;
    const int maxClients = (std::min)(gpGlobals->maxClients, 32);
    edict_t* local{}; int localIndex{}, humans{};
    for (int i = 1; i <= maxClients; ++i) {
        edict_t* e = g_engfuncs.pfnPEntityOfEntIndex(i);
        if (!Client(e) || (e->v.flags & FL_FAKECLIENT)) continue;
        ++humans; local = e; localIndex = i;
    }
    if (!humans) { s.status = cz::SessionStatus::NoPlayer; return s; }
    // GoldSrc listen-server host occupies client slot 1. Never accept a remote-only slot.
    if (humans != 1 || localIndex != 1 || remoteAddress[localIndex]) {
        s.status = cz::SessionStatus::NotLocal; return s;
    }
    s.localIndex = localIndex; s.localTeam = Team(local, localIndex);
    if (!Alive(local) || local->v.iuser1 != 0 || s.localTeam == 0) {
        s.status = cz::SessionStatus::Spectating; return s;
    }
    if (!Finite(local->v.origin) || !Finite(local->v.view_ofs) ||
        !Finite(local->v.v_angle) || !Finite(local->v.punchangle)) return s;
    s.camera.origin = {local->v.origin.x + local->v.view_ofs.x,
        local->v.origin.y + local->v.view_ofs.y, local->v.origin.z + local->v.view_ofs.z};
    s.camera.angles = {local->v.v_angle.x + local->v.punchangle.x,
        local->v.v_angle.y + local->v.punchangle.y, local->v.v_angle.z + local->v.punchangle.z};
    s.camera.horizontalFov = local->v.fov == 0 ? 90.0f : local->v.fov;
    if (!std::isfinite(s.camera.horizontalFov) || s.camera.horizontalFov < 1 || s.camera.horizontalFov > 179) return s;
    s.status = cz::SessionStatus::Active;
    for (int i = 1; i <= maxClients; ++i) {
        edict_t* e = g_engfuncs.pfnPEntityOfEntIndex(i);
        if (!Alive(e) || !(e->v.flags & FL_FAKECLIENT) || e->v.iuser1 != 0) continue;
        const unsigned team = Team(e, i);
        if (!team || team == s.localTeam || !Finite(e->v.origin) || !Finite(e->v.mins) || !Finite(e->v.maxs)) continue;
        if (e->v.mins.x >= e->v.maxs.x || e->v.mins.y >= e->v.maxs.y || e->v.mins.z >= e->v.maxs.z) continue;
        auto& p = s.players[s.playerCount++];
        p.index = i; p.team = team; p.flags = cz::Alive | cz::Bot;
        p.health = e->v.health;
        p.maxHealth = std::isfinite(e->v.max_health) && e->v.max_health > 0 ? e->v.max_health : 100.0f;
        p.origin = Copy(e->v.origin); p.mins = Copy(e->v.mins); p.maxs = Copy(e->v.maxs);
    }
    return s;
}
bool ReadBhopControl() {
    const auto now=GetTickCount64();
    if (!bhopControlOpen && now>=nextBhopOpen) {
        nextBhopOpen=now+250;
        bhopControlOpen=bhopControl.Open(GetCurrentProcessId(),L"BhopControl",false);
    }
    cz::BhopControl next{};
    if (bhopControlOpen && bhopControl.Read(next)) latestBhop=next;
    // Take the validation time after reading: a newer producer timestamp is valid.
    const bool valid=cz::ValidBhopControl(latestBhop,GetCurrentProcessId(),GetTickCount64());
    // A paused host may produce no PM_Move while the UI sends an off packet.
    // Observe that interruption here too, before a later on packet replaces it.
    if(!valid || latestBhop.flags!=3) { jumpAssist.Reset(); }
    return valid;
}
void PublishBhopStatus(const cz::Snapshot& snapshot) {
    const bool fresh=ReadBhopControl();
    if (!bhopStatusOpen) bhopStatusOpen=bhopStatus.Open(GetCurrentProcessId(),L"BhopStatus",true);
    cz::BhopStatus status{}; status.processId=GetCurrentProcessId(); status.timestampMs=GetTickCount64();
    status.flags=cz::BhopAvailable; status.session=snapshot.status; status.rearmedCommands=rearmedCommands;
    if(fresh) status.mode=latestBhop.mode;
    if (fresh && (latestBhop.flags&cz::BhopEnabled) && snapshot.status==cz::SessionStatus::Active) status.flags|=cz::BhopArmed;
    if (bhopStatusOpen) bhopStatus.Publish(status);
}
void PM_Move(playermove_t* movement,qboolean server) {
    if (server && movement && movement->server && movement->player_index==0) {
        const bool controlReady=ReadBhopControl() && (latestBhop.flags&cz::BhopEnabled) && (latestBhop.flags&cz::BhopSpaceHeld);
        // Recheck the live session, including host identity, before each command.
        const bool local=controlReady && BuildSnapshot().status==cz::SessionStatus::Active;
        const edict_t* host=local?g_engfuncs.pfnPEntityOfEntIndex(1):nullptr;
        const auto result=jumpAssist.Apply(movement,host && host->v.waterlevel==0,controlReady,latestBhop.mode,latestBhop.generation);
        if(result.rearmed) ++rearmedCommands;
    }
    RETURN_META(MRES_IGNORED);
}
void Publish(const cz::Snapshot& s) {
    lastSnapshot = s;
    // Read the live engine setting; config.cfg alone may lag console/menu changes.
    const bool wide=g_engfuncs.pfnCVarGetFloat && g_engfuncs.pfnCVarGetFloat("gl_widescreen_yfov")!=0.0f;
    cz::render::Update(s,wide);
    PublishBhopStatus(s);
    if (!channelOpen || !channel.Publish(s)) ++droppedFrames;
}
void Clear() {
    mapActive = false;
    std::memset(teams, 0, sizeof(teams)); std::memset(teamKnown, 0, sizeof(teamKnown));
    message = {};
    latestBhop={};
    jumpAssist.Reset();
    Publish(Empty(cz::SessionStatus::Waiting));
}
void Status() {
    char text[512];
    std::snprintf(text, sizeof(text), "[CZHELP] pid=%lu active_map=%d channel=%d status=%u local=%u team=%u enemy_bots=%u dropped=%llu TeamInfo=%d\n",
        GetCurrentProcessId(), mapActive, channelOpen, static_cast<unsigned>(lastSnapshot.status),
        lastSnapshot.localIndex, lastSnapshot.localTeam, lastSnapshot.playerCount, droppedFrames, teamInfoId);
    g_engfuncs.pfnServerPrint(text);
}
void Dump() {
    const auto s = BuildSnapshot();
    Status(); char text[512];
    if (s.status != cz::SessionStatus::Active) {
        g_engfuncs.pfnServerPrint("[CZHELP] dump requires an active local LAN BOT session and a living host.\n");
        return;
    }
    std::snprintf(text, sizeof(text), "[CZHELP] camera origin=(%.3f %.3f %.3f) angles=(%.3f %.3f %.3f) fov=%.3f\n",
        s.camera.origin.x, s.camera.origin.y, s.camera.origin.z, s.camera.angles.x, s.camera.angles.y, s.camera.angles.z, s.camera.horizontalFov);
    g_engfuncs.pfnServerPrint(text);
    if (!gpGlobals || !mapActive) return;
    for (int i = 1; i <= (std::min)(gpGlobals->maxClients, 32); ++i) {
        edict_t* e = g_engfuncs.pfnPEntityOfEntIndex(i);
        if (!Client(e) || !(e->v.flags & FL_FAKECLIENT)) continue;
        std::snprintf(text, sizeof(text), "[CZHELP] bot index=%d team=%u hp=%.3f max=%.3f alive=%d origin=(%.3f %.3f %.3f) mins_z=%.3f maxs_z=%.3f\n",
            i, Team(e, i), e->v.health, e->v.max_health, Alive(e), e->v.origin.x, e->v.origin.y, e->v.origin.z, e->v.mins.z, e->v.maxs.z);
        g_engfuncs.pfnServerPrint(text);
    }
}
void GameInit() {
    static char statusCommand[] = "cz_help_status", dumpCommand[] = "cz_help_dump";
    g_engfuncs.pfnAddServerCommand(statusCommand, Status);
    g_engfuncs.pfnAddServerCommand(dumpCommand, Dump);
    RETURN_META(MRES_IGNORED);
}
void ServerActivate(edict_t*, int, int) {
    mapActive = true;
    if (gpMetaUtilFuncs && gpMetaUtilFuncs->pfnGetUserMsgID)
        teamInfoId = gpMetaUtilFuncs->pfnGetUserMsgID(&Plugin_info, "TeamInfo", nullptr);
    RETURN_META(MRES_IGNORED);
}
void ServerDeactivate() { Clear(); RETURN_META(MRES_IGNORED); }
void StartFrame() { Publish(BuildSnapshot()); RETURN_META(MRES_IGNORED); }
qboolean ClientConnect(edict_t* e, const char*, const char* address, char*) {
    const int i = g_engfuncs.pfnIndexOfEdict(e);
    if (Slot(i)) {
        teams[i] = 0; teamKnown[i] = false;
        const bool loopbackIp = address && std::strncmp(address, "127.0.0.1", 9) == 0 &&
            (address[9] == '\0' || address[9] == ':');
        remoteAddress[i] = address && std::strcmp(address, "loopback") != 0 &&
            std::strcmp(address, "localhost") != 0 && !loopbackIp;
    }
    RETURN_META_VALUE(MRES_IGNORED, TRUE);
}
void ClientDisconnect(edict_t* e) {
    const int i = g_engfuncs.pfnIndexOfEdict(e);
    if (Slot(i)) { teams[i] = 0; teamKnown[i] = false; remoteAddress[i] = false; }
    if (i == 1) Publish(Empty(cz::SessionStatus::NoPlayer));
    RETURN_META(MRES_IGNORED);
}
void MessageBegin(int, int type, const float*, edict_t*) {
    message = {}; message.team = teamInfoId > 0 && type == teamInfoId;
    RETURN_META(MRES_IGNORED);
}
void WriteByte(int value) {
    if (message.team) { if (message.field++ == 0) message.player = value; else message.team = false; }
    RETURN_META(MRES_IGNORED);
}
void WriteString(const char* value) {
    if (message.team) {
        if (message.field++ == 1) { message.value = TeamName(value); message.valid = true; }
        else message.team = false;
    }
    RETURN_META(MRES_IGNORED);
}
void MessageEnd() {
    if (message.team && message.valid && message.field == 2 && Slot(message.player)) {
        teams[message.player] = message.value; teamKnown[message.player] = true;
    }
    message = {}; RETURN_META(MRES_IGNORED);
}
} // namespace

extern "C" void WINAPI GiveFnptrsToDll(enginefuncs_t* engine, globalvars_t* globals) {
    g_engfuncs = {};
    if (engine) std::memcpy(&g_engfuncs, engine, engineAbiBytes);
    gpGlobals = globals;
}
C_DLLEXPORT int Meta_Query(const char* version, plugin_info_t** info, mutil_funcs_t* util) {
    if (!version || !info) return FALSE;
    int major{}, minor{};
    if (std::sscanf(version, "%d:%d", &major, &minor) != 2 || major != 5 || minor < 13) return FALSE;
    *info = &Plugin_info; gpMetaUtilFuncs = util; return TRUE;
}
C_DLLEXPORT int GetEntityAPI2(DLL_FUNCTIONS* table, int* version) {
    if (!table || !version) return FALSE;
    if (*version != INTERFACE_VERSION) { *version = INTERFACE_VERSION; return FALSE; }
    *table = {}; table->pfnGameInit = GameInit; table->pfnServerDeactivate = ServerDeactivate;
    table->pfnClientConnect = ClientConnect; table->pfnClientDisconnect = ClientDisconnect;
    table->pfnPM_Move = PM_Move;
    return TRUE;
}
C_DLLEXPORT int GetEntityAPI2_Post(DLL_FUNCTIONS* table, int* version) {
    if (!table || !version) return FALSE;
    if (*version != INTERFACE_VERSION) { *version = INTERFACE_VERSION; return FALSE; }
    *table = {}; table->pfnServerActivate = ServerActivate; table->pfnStartFrame = StartFrame;
    return TRUE;
}
C_DLLEXPORT int GetEngineFunctions(enginefuncs_t* table, int* version) {
    if (!table || !version) return FALSE;
    if (*version != ENGINE_INTERFACE_VERSION) { *version = ENGINE_INTERFACE_VERSION; return FALSE; }
    std::memset(table, 0, engineAbiBytes);
    table->pfnMessageBegin = MessageBegin; table->pfnMessageEnd = MessageEnd;
    table->pfnWriteByte = WriteByte; table->pfnWriteString = WriteString; return TRUE;
}
C_DLLEXPORT int Meta_Attach(PLUG_LOADTIME now, META_FUNCTIONS* table, meta_globals_t* globals, gamedll_funcs_t* game) {
    if (now > Plugin_info.loadable || !table || !globals || !game) return FALSE;
    gpMetaGlobals = globals; gpGamedllFuncs = game;
    *table = {}; table->pfnGetEntityAPI2 = GetEntityAPI2;
    table->pfnGetEntityAPI2_Post = GetEntityAPI2_Post; table->pfnGetEngineFunctions = GetEngineFunctions;
    channelOpen = channel.Open(GetCurrentProcessId());
    Clear(); cz::render::Install(); return TRUE;
}
C_DLLEXPORT int Meta_Detach(PLUG_LOADTIME now, PL_UNLOAD_REASON reason) {
    // Engine console commands cannot be unregistered safely from a live GoldSrc process.
    if (now > Plugin_info.unloadable && reason != PNL_CMD_FORCED && reason != PNL_PLG_FORCED) return FALSE;
    Clear(); cz::render::Shutdown(); channel.Close(); channelOpen = false;
    bhopControl.Close(); bhopStatus.Close(); bhopControlOpen=bhopStatusOpen=false; latestBhop={};
    return TRUE;
}

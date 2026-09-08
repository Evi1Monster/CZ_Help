#pragma once
#include "render_control.h"
#include "jump_modes.h"
namespace cz {
inline constexpr std::uint32_t kBhopMagic=0x435A4248,kBhopVersion=2;
inline constexpr std::uint64_t kBhopControlStaleMs=150;
enum BhopFlags : std::uint32_t { BhopEnabled=1,BhopSpaceHeld=2 };
enum BhopStatusFlags : std::uint32_t { BhopAvailable=1,BhopArmed=2 };
#pragma pack(push,4)
struct BhopControl {
    std::uint32_t magic{kBhopMagic},version{kBhopVersion},bytes{40},processId{};
    std::uint64_t timestampMs{};
    std::uint32_t flags{};
    JumpMode mode{JumpMode::AutoJump};
    std::uint32_t generation{},reserved{};
};
struct BhopStatus {
    std::uint32_t magic{kBhopMagic},version{kBhopVersion},bytes{40},processId{};
    std::uint64_t timestampMs{};
    std::uint32_t flags{},rearmedCommands{};
    SessionStatus session{SessionStatus::Waiting};
    JumpMode mode{JumpMode::AutoJump};
};
#pragma pack(pop)
static_assert(sizeof(BhopControl)==40 && sizeof(BhopStatus)==40);
inline bool ValidBhopControl(const BhopControl& c,std::uint32_t pid,std::uint64_t now) {
    return pid && c.magic==kBhopMagic && c.version==kBhopVersion && c.bytes==sizeof(c) && c.processId==pid &&
        c.timestampMs>0 && c.timestampMs<=now && now-c.timestampMs<=kBhopControlStaleMs && !(c.flags&~3u) && ValidJumpMode(c.mode);
}
inline bool ValidBhopStatus(const BhopStatus& s,std::uint32_t pid,std::uint64_t now) {
    return pid && s.magic==kBhopMagic && s.version==kBhopVersion && s.bytes==sizeof(s) && s.processId==pid &&
        s.timestampMs>0 && s.timestampMs<=now && now-s.timestampMs<=kStaleMs && !(s.flags&~3u) &&
        static_cast<std::uint32_t>(s.session)<=static_cast<std::uint32_t>(SessionStatus::Spectating) && ValidJumpMode(s.mode);
}
using BhopControlMailbox=RenderMailbox<BhopControl>;
using BhopStatusMailbox=RenderMailbox<BhopStatus>;
}

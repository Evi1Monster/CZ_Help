#pragma once
#include <cstdint>
namespace cz {
// Keep the original stored/protocol IDs; retired IDs must never select a new action.
enum class JumpMode : std::uint32_t { AutoJump=0, BhopJump=4 };
inline constexpr bool ValidJumpMode(JumpMode mode) { return mode==JumpMode::AutoJump || mode==JumpMode::BhopJump; }
}

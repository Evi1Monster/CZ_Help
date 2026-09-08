#pragma once
#include <cstdint>
#include <type_traits>

namespace cz {
inline constexpr std::uint32_t kMagic = 0x435A4850;
inline constexpr std::uint32_t kVersion = 1;
inline constexpr std::uint32_t kMaxPlayers = 32;
inline constexpr std::uint64_t kStaleMs = 500;
enum class SessionStatus : std::uint32_t { Waiting = 0, Active = 1, NotLocal = 2, NoPlayer = 3, Spectating = 4 };
enum PlayerFlags : std::uint32_t { Alive = 1, Bot = 2 };
#pragma pack(push, 4)
struct Vec3 { float x{}, y{}, z{}; };
struct Camera { Vec3 origin{}, angles{}; float horizontalFov{90.0f}; };
struct Player {
    std::uint32_t index{}, team{}, flags{};
    float health{}, maxHealth{};
    Vec3 origin{}, mins{}, maxs{};
};
struct Snapshot {
    std::uint32_t magic{kMagic}, version{kVersion}, bytes{}, processId{};
    std::uint64_t timestampMs{};
    SessionStatus status{SessionStatus::Waiting};
    std::uint32_t localIndex{}, localTeam{}, playerCount{};
    Camera camera{};
    Player players[kMaxPlayers]{};
};
#pragma pack(pop)
static_assert(std::is_trivially_copyable_v<Snapshot>);
static_assert(sizeof(Player) == 56);
static_assert(sizeof(Snapshot) == 1860);
} // namespace cz

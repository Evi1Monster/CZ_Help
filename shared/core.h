#pragma once
#include "protocol.h"
#include <array>

namespace cz {
struct Viewport { int width{}, height{}; };
struct Point { float x{}, y{}; };
struct Rect { float left{}, top{}, right{}, bottom{}; };
Camera ResolveProjectionCamera(Camera, Viewport, bool widescreenFov);
bool ProjectPoint(const Camera&, Vec3, Viewport, Point&);
bool ProjectBox(const Camera&, const Player&, Viewport, Rect&);
bool ValidSnapshot(const Snapshot&, std::uint32_t pid, std::uint64_t now);
bool IsTarget(const Snapshot&, const Player&);
struct Options { bool boxes{true}, health{true}; };
class KeyToggle {
public:
    bool enabled{false};
    bool Update(bool foreground,bool down);
private:
    bool previous_{false},wasForeground_{false};
};
class Switches {
public:
    bool enabled{false};
    Options options{};
    bool Update(bool foreground, const std::array<bool, 3>& down);
private:
    std::array<bool, 3> previous_{};
    bool wasForeground_{false};
};
}

#pragma once
#include "bhop.h"
#include "jump_modes.h"
namespace cz {
struct JumpResult { bool rearmed{}; };
class JumpAssist {
public:
    JumpResult Apply(playermove_t* movement,bool authorized,bool spaceHeld,JumpMode mode,std::uint32_t generation);
    void Reset();
private:
    JumpMode mode_{JumpMode::AutoJump};
    std::uint32_t generation_{};
    bool running_{},haveYaw_{};
    float previousYaw_{};
};
}

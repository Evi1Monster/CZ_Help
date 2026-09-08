#include "bhop.h"
#include "in_buttons.h"
namespace cz {
bool ApplyBhop(playermove_t* movement,bool authorized,bool spaceHeld) {
    if (!movement || !authorized || !spaceHeld || !movement->server || movement->player_index!=0 ||
        movement->movetype!=MOVETYPE_WALK || !(movement->flags&FL_ONGROUND) ||
        movement->waterjumptime!=0 || movement->dead || movement->deadflag!=DEAD_NO || movement->spectator ||
        (movement->flags&(FL_FROZEN|FL_ONTRAIN|FL_WATERJUMP)) ||
        !(movement->cmd.buttons&IN_JUMP) || !(movement->oldbuttons&IN_JUMP)) return false;
    // The original game PM_Jump still performs the jump and all stock physics.
    // The pre hook must use flags: onground/waterlevel are computed inside PM_Move
    // and can still belong to the previous player. Caller checks host water level.
    // Only release its held-key latch when the host has landed.
    movement->oldbuttons&=~IN_JUMP;
    return true;
}
}

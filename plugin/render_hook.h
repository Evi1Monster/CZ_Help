#pragma once
#include "protocol.h"
#include <windows.h>

namespace cz::render {
// Called from the engine/plugin lifecycle, outside DllMain.
bool Install();
void Update(const Snapshot& snapshot,bool widescreenFov=false);
void Shutdown();
// Draw into the current context immediately before its buffer exchange.
void BeforePresent(HDC dc);
}

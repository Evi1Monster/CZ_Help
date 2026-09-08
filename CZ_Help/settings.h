#pragma once
#include "core.h"
#include "jump_modes.h"
#include <string>
#include <array>
namespace cz {
struct Settings {
    std::wstring path;
    std::wstring gamePath{L"D:\\SteamLibrary\\steamapps\\common\\Half-Life"};
    std::array<int,3> keys{0x77,0x78,0x7A};
    int bhopKey{0x72};
    JumpMode jumpMode{JumpMode::AutoJump};
    Options options{};
    std::wstring warning;
    bool Load(const std::wstring& file);
    bool Save() const;
};
}

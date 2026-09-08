#include "settings.h"
#include <windows.h>
#include <iostream>
int main() {
    wchar_t tmp[MAX_PATH]{}; GetTempPathW(MAX_PATH,tmp);
    std::wstring path=std::wstring(tmp)+L"CZHelp-settings-test-"+std::to_wstring(GetCurrentProcessId())+L".ini";
    cz::Settings settings;
    if (!settings.Load(path) || !settings.options.boxes || settings.keys[0]!=119 || settings.bhopKey!=0x72) return 1;
    settings.options.boxes=false; settings.options.health=true; settings.keys={0x70,0x71,0x72};
    settings.bhopKey=0x73;
    settings.gamePath=L"D:\\测试游戏\\Half-Life";
    if (!settings.Save()) return 2;
    cz::Settings loaded;
    if (!loaded.Load(path) || loaded.options.boxes || !loaded.options.health || loaded.keys[2]!=0x72 || loaded.bhopKey!=0x73) {
        std::cerr<<"FAIL: independent bhop key persistence\n"; return 3;
    }
    if (loaded.gamePath!=settings.gamePath) { std::cerr<<"FAIL: selected game directory persistence\n"; return 7; }
    settings.jumpMode=cz::JumpMode::BhopJump;
    if(!settings.Save() || !loaded.Load(path) || loaded.jumpMode!=cz::JumpMode::BhopJump) { std::cerr<<"FAIL: jump mode preference persistence\n"; return 8; }
    if(GetPrivateProfileIntW(L"Movement",L"Mode",0,path.c_str())!=4) return 11;
    for(const wchar_t* removed:{L"1",L"2",L"3",L"5"}) {
        WritePrivateProfileStringW(L"Movement",L"Mode",removed,path.c_str());
        if(loaded.Load(path) || loaded.jumpMode!=cz::JumpMode::AutoJump || loaded.warning.empty()) { std::cerr<<"FAIL: removed mode must fall back to autojump\n"; return 12; }
        if(loaded.options.boxes || !loaded.options.health || loaded.keys!=settings.keys) return 13;
    }
    WritePrivateProfileStringW(L"Movement",L"Mode",L"99",path.c_str());
    if(loaded.Load(path) || loaded.jumpMode!=cz::JumpMode::AutoJump || loaded.warning.empty()) { std::cerr<<"FAIL: invalid jump mode fallback\n"; return 9; }
    WritePrivateProfileStringW(L"Movement",L"Mode",nullptr,path.c_str());
    if(!loaded.Load(path) || loaded.jumpMode!=cz::JumpMode::AutoJump) { std::cerr<<"FAIL: old configuration uses original autojump\n"; return 10; }
    WritePrivateProfileStringW(L"Hotkeys",L"BhopToggle",L"114",path.c_str());
    if (loaded.Load(path) || loaded.keys[0]!=0x70 || loaded.keys[2]!=0x72 || loaded.bhopKey!=0 || loaded.warning.empty()) return 5;
    WritePrivateProfileStringW(L"Hotkeys",L"BhopToggle",L"32",path.c_str());
    if (loaded.Load(path) || loaded.bhopKey!=0) return 6;
    WritePrivateProfileStringW(L"Hotkeys",L"BhopToggle",L"115",path.c_str());
    WritePrivateProfileStringW(L"Hotkeys",L"Toggle",L"113",path.c_str());
    if (loaded.Load(path) || loaded.keys[0]!=119 || loaded.warning.empty()) return 4;
    DeleteFileW(path.c_str());
    std::cout<<"PASS: config creation, independent preference persistence, custom keys, duplicate rejection\n";
}

#include "settings.h"
#include <windows.h>
namespace cz {
bool Settings::Load(const std::wstring& file) {
    path=file; warning.clear(); keys={0x77,0x78,0x7A}; bhopKey=0x72;
    wchar_t gameDirectory[32768]{};
    GetPrivateProfileStringW(L"Game",L"Path",L"D:\\SteamLibrary\\steamapps\\common\\Half-Life",gameDirectory,32768,path.c_str());
    gamePath=gameDirectory;
    const wchar_t* names[]={L"Toggle",L"Boxes",L"Health"};
    auto candidate=keys;
    for (int i=0;i<3;++i) candidate[i]=static_cast<int>(GetPrivateProfileIntW(L"Hotkeys",names[i],keys[i],path.c_str()));
    // Keyboard keys only: exclude mouse buttons, reserved F12 and Windows keys.
    bool valid=true;
    for (int key:candidate) valid=valid && key>=8 && key<=0xFE && key!=0x7B && key!=0x5B && key!=0x5C;
    valid=valid && candidate[0]!=candidate[1] && candidate[0]!=candidate[2] && candidate[1]!=candidate[2];
    if (valid) keys=candidate;
    else warning=L"快捷键配置无效或重复，已使用 F8 / F9 / F11。";
    const int bhop=static_cast<int>(GetPrivateProfileIntW(L"Hotkeys",L"BhopToggle",bhopKey,path.c_str()));
    const bool bhopValid=bhop>=8 && bhop<=0xFE && bhop!=VK_SPACE && bhop!=VK_F12 && bhop!=VK_LWIN && bhop!=VK_RWIN &&
        bhop!=keys[0] && bhop!=keys[1] && bhop!=keys[2];
    bhopKey=bhopValid?bhop:0;
    if (!bhopValid) {
        if (!warning.empty()) warning+=L"\r\n";
        warning+=L"连跳快捷键无效或冲突，已禁用此快捷键；仍可勾选连跳。";
    }
    options.boxes=GetPrivateProfileIntW(L"Display",L"Boxes",1,path.c_str())!=0;
    options.health=GetPrivateProfileIntW(L"Display",L"Health",1,path.c_str())!=0;
    const auto mode=static_cast<JumpMode>(GetPrivateProfileIntW(L"Movement",L"Mode",0,path.c_str()));
    const bool modeValid=ValidJumpMode(mode);
    jumpMode=modeValid?mode:JumpMode::AutoJump;
    if(!modeValid) {
        if(!warning.empty()) warning+=L"\r\n";
        warning+=L"跳跃模式无效，已恢复为自动跳跃。";
    }
    if (GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES && !Save()) {
        warning=L"无法创建设置文件，请将程序放到可写目录。"; return false;
    }
    return valid && bhopValid && modeValid;
}
bool Settings::Save() const {
    // New settings files use UTF-16 so game paths round-trip through the Win32 INI API.
    HANDLE fresh=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (fresh!=INVALID_HANDLE_VALUE) {
        const WORD bom=0xFEFF; DWORD written{}; const bool initialized=WriteFile(fresh,&bom,sizeof(bom),&written,nullptr) && written==sizeof(bom);
        CloseHandle(fresh); if (!initialized) return false;
    }
    const wchar_t* names[]={L"Toggle",L"Boxes",L"Health"};
    bool ok=true;
    ok=WritePrivateProfileStringW(L"Game",L"Path",gamePath.c_str(),path.c_str())!=FALSE && ok;
    for (int i=0;i<3;++i) ok=WritePrivateProfileStringW(L"Hotkeys",names[i],std::to_wstring(keys[i]).c_str(),path.c_str())!=FALSE && ok;
    ok=WritePrivateProfileStringW(L"Hotkeys",L"BhopToggle",std::to_wstring(bhopKey).c_str(),path.c_str())!=FALSE && ok;
    ok=WritePrivateProfileStringW(L"Movement",L"Mode",std::to_wstring(static_cast<unsigned>(jumpMode)).c_str(),path.c_str())!=FALSE && ok;
    ok=WritePrivateProfileStringW(L"Display",L"Boxes",options.boxes?L"1":L"0",path.c_str())!=FALSE && ok;
    ok=WritePrivateProfileStringW(L"Display",L"Health",options.health?L"1":L"0",path.c_str())!=FALSE && ok;
    return ok;
}
}

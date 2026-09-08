#include "session.h"
#include <shellapi.h>
#include <iostream>

int main() {
    int failures{};
    auto check=[&](bool ok,const char* name) { if (!ok) { ++failures; std::cerr<<"FAIL: "<<name<<'\n'; } };
    check(cz::ParseSetupStatus(L"ready\r\n").state==cz::SetupState::Ready,"ready worker permits use");
    const auto error=cz::ParseSetupStatus(L"error\n配置已修改");
    check(error.state==cz::SetupState::Error && error.detail==L"配置已修改","worker failure remains visible with Unicode detail");
    check(cz::ParseSetupStatus(L"waiting_game_exit\n").state==cz::SetupState::WaitingGameExit,"loaded game delays preparation");
    check(cz::ParseSetupStatus(L"waiting_cleanup\n").state==cz::SetupState::WaitingCleanup,"earlier cleanup cannot appear ready");
    check(cz::ParseSetupStatus(L"ready-extra\n").state==cz::SetupState::Error,"unknown status cannot enable helper");
    check(cz::ParseSetupStatus(L"cleaned\n").state==cz::SetupState::Cleaned,"cleanup is distinct from ready");
    for (const auto& value : {L"D:\\游戏 folder\\",L"a\"b",L"",L"$(`literal`) & space %name%"}) {
        const auto command=L"test.exe "+cz::QuoteWindowsArgument(value);
        int argc{}; auto argv=CommandLineToArgvW(command.c_str(),&argc);
        check(argv && argc==2 && std::wstring(argv[1])==value,"native process arguments round trip without shell expansion");
        if (argv) LocalFree(argv);
    }
    check(cz::NormalizeGamePath(L"D:\\SteamLibrary\\steamapps\\common\\Half-Life\\")==L"D:\\SteamLibrary\\steamapps\\common\\Half-Life","normalize selected game root");
    cz::SessionManager missing;
    check(!missing.Start(L"Z:\\MissingGame",L"Z:\\MissingBundle") && missing.Status().state==cz::SetupState::Error,
        "missing bundle is a visible error and starts no installer");
    std::cout<<"Session client failures: "<<failures<<'\n'; return failures?1:0;
}

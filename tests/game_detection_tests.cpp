#include "game_detection.h"
#include "session.h"
#include <filesystem>
#include <fstream>
#include <iostream>

int wmain(int argc,wchar_t** argv) {
    if(argc>1 && std::wstring(argv[1])==L"--fixture") { Sleep(60000); return 0; }
    int failures{};
    auto check=[&](bool ok,const char* name) { if(!ok) { ++failures; std::cerr<<"FAIL: "<<name<<'\n'; } };
    check(cz::IsConditionZeroCommandLine(L"\"D:\\游戏 空格\\hl.exe\" -steam -game \"czero\" -console"),"quoted CZ launch is recognized");
    check(cz::IsConditionZeroCommandLine(L"hl.exe -GAME CZERO"),"game arguments are case insensitive");
    for(const auto* command:{L"hl.exe",L"hl.exe -game",L"hl.exe -game cstrike",L"hl.exe -game czeror",L"hl.exe +echo czero",
        L"hl.exe -game czero -game cstrike",L"hl.exe -game ../czero",L"hl.exe -game czeroExtra"})
        check(!cz::IsConditionZeroCommandLine(command),"other or ambiguous mods are rejected");
    const cz::GameDiscovery one{{{12,L"E:\\游戏"}},L""};
    check(cz::SelectGamePath(L"F:\\Chosen",L"D:\\Old",one).path==L"F:\\Chosen","explicit override wins");
    const auto detected=cz::SelectGamePath(L"",L"D:\\Old",one);
    check(detected.path==L"E:\\游戏" && detected.source==cz::GamePathSource::RunningGame,"running game beats stale INI");
    check(cz::SelectGamePath(L"",L"D:\\Old",{}).path==L"D:\\Old","no running game retains configured path");
    const cz::GameDiscovery duplicate{{{12,L"E:\\Game"},{13,L"e:\\game\\"}},L""};
    check(cz::SelectGamePath(L"",L"D:\\Old",duplicate).source==cz::GamePathSource::RunningGame,"same directory is deduplicated");
    const cz::GameDiscovery many{{{12,L"E:\\Game"},{13,L"F:\\Game"}},L""};
    const auto ambiguous=cz::SelectGamePath(L"",L"D:\\Old",many);
    check(ambiguous.path.empty() && ambiguous.source==cz::GamePathSource::Ambiguous,"multiple installs cannot silently choose wrong directory");
    check(cz::SelectGamePath(L"",L"f:\\game",many).path==L"F:\\Game","configured running install resolves ambiguity");
    check(!cz::SelectGamePath(L"",L"D:\\Old",{{},L"process unavailable"}).warning.empty(),"query failure remains visible");
    const auto partial=cz::SelectGamePath(L"",L"F:\\Game",{{{12,L"E:\\Game"}},L"scan interrupted",false});
    check(partial.path==L"F:\\Game" && partial.source==cz::GamePathSource::Configuration && !partial.warning.empty(),
        "partial scan cannot overwrite a configured install with its first candidate");

    // Real child process: Windows supplies both the executable path and command line.
    // Removing mod/file validation or using the parent executable path breaks these assertions.
    namespace fs=std::filesystem;
    const auto root=fs::current_path()/L"discovery fixtures"/(L"游戏 & "+std::to_wstring(GetCurrentProcessId()));
    fs::create_directories(root/L"czero"/L"dlls");
    wchar_t self[32768]{}; GetModuleFileNameW(nullptr,self,32768);
    fs::copy_file(self,root/L"hl.exe",fs::copy_options::overwrite_existing);
    std::ofstream(root/L"czero"/L"liblist.gam")<<"game fixture";
    check(!cz::IsGameDirectory(root.wstring()),"incomplete game directory rejected");
    std::ofstream(root/L"czero"/L"dlls"/L"mp.dll")<<"fixture";
    check(cz::IsGameDirectory(root.wstring()),"complete directory accepted");
    for(const auto* mod:{L"czero",L"cstrike"}) {
        auto command=cz::QuoteWindowsArgument((root/L"hl.exe").wstring())+L" --fixture -game "+mod;
        STARTUPINFOW startup{}; startup.cb=sizeof(startup); startup.dwFlags=STARTF_USESHOWWINDOW; startup.wShowWindow=SW_HIDE;
        PROCESS_INFORMATION child{};
        const bool started=CreateProcessW((root/L"hl.exe").c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,root.c_str(),&startup,&child)!=FALSE;
        check(started,"start real isolated game process");
        if(!started) continue;
        CloseHandle(child.hThread);
        const auto scan=cz::DiscoverRunningGames();
        bool found=false;
        for(const auto& game:scan.games) if(game.processId==child.dwProcessId) { found=true; check(game.path==root.wstring(),"detected directory belongs to child process"); }
        check(found==(std::wstring(mod)==L"czero"),"OS discovery distinguishes CZ from CS 1.6 sharing its installation");
        if(!scan.warning.empty()) std::wcerr<<scan.warning<<L'\n';
        TerminateProcess(child.hProcess,0); WaitForSingleObject(child.hProcess,5000); CloseHandle(child.hProcess);
    }
    // root is an owned PID-specific child of current_path()/discovery fixtures.
    fs::remove_all(root);
    std::cout<<"Game discovery failures: "<<failures<<'\n'; return failures?1:0;
}

#include "game_detection.h"
#include "session.h"
#include <shellapi.h>
#include <wbemidl.h>
#include <wrl/client.h>
#include <algorithm>
#include <chrono>
#include <future>
#include <thread>

namespace cz {
namespace {
using Microsoft::WRL::ComPtr;
struct Apartment {
    HRESULT result{CoInitializeEx(nullptr,COINIT_MULTITHREADED)};
    ~Apartment() { if(SUCCEEDED(result)) CoUninitialize(); }
};
struct BString {
    BSTR value;
    explicit BString(const wchar_t* text):value(SysAllocString(text)) {}
    ~BString() { SysFreeString(value); }
};
struct Property {
    VARIANT value{};
    ~Property() { VariantClear(&value); }
};
bool SamePath(const std::wstring& a,const std::wstring& b) { return _wcsicmp(a.c_str(),b.c_str())==0; }
}

bool IsConditionZeroCommandLine(const std::wstring& command) {
    if(command.empty()) return false;
    int count{}; auto args=CommandLineToArgvW(command.c_str(),&count);
    if(!args) return false;
    int gameOptions{}; bool czero=false;
    for(int i=1;i<count;++i) if(_wcsicmp(args[i],L"-game")==0) {
        ++gameOptions;
        if(i+1<count) czero=_wcsicmp(args[++i],L"czero")==0;
        else czero=false;
    }
    LocalFree(args);
    return gameOptions==1 && czero;
}

bool IsGameDirectory(const std::wstring& path) {
    if(path.empty()) return false;
    for(const auto* file:{L"\\hl.exe",L"\\czero\\liblist.gam",L"\\czero\\dlls\\mp.dll"}) {
        const DWORD attributes=GetFileAttributesW((path+file).c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES || (attributes&FILE_ATTRIBUTE_DIRECTORY)) return false;
    }
    return true;
}

static GameDiscovery DiscoverGamesOnWorker() {
    GameDiscovery result;
    auto unavailable=[&]() {
        result.complete=false;
        result.games.clear();
        result.warning=L"无法读取运行中的游戏信息；请检查游戏与辅助的运行权限，或在设置中填写目录。";
        return result;
    };
    // Local WMI supplies public process metadata; no game memory or process writes.
    Apartment apartment;
    if(FAILED(apartment.result) && apartment.result!=RPC_E_CHANGED_MODE) return unavailable();
    ComPtr<IWbemLocator> locator;
    if(FAILED(CoCreateInstance(CLSID_WbemLocator,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&locator)))) return unavailable();
    ComPtr<IWbemServices> services;
    BString nameSpace(L"ROOT\\CIMV2");
    if(!nameSpace.value || FAILED(locator->ConnectServer(nameSpace.value,nullptr,nullptr,nullptr,
        WBEM_FLAG_CONNECT_USE_MAX_WAIT,nullptr,nullptr,&services))) return unavailable();
    if(FAILED(CoSetProxyBlanket(services.Get(),RPC_C_AUTHN_WINNT,RPC_C_AUTHZ_NONE,nullptr,
        RPC_C_AUTHN_LEVEL_CALL,RPC_C_IMP_LEVEL_IMPERSONATE,nullptr,EOAC_NONE))) return unavailable();
    ComPtr<IEnumWbemClassObject> processes;
    BString language(L"WQL"),query(L"SELECT ProcessId, CommandLine FROM Win32_Process WHERE Name = 'hl.exe'");
    if(!language.value || !query.value || FAILED(services->ExecQuery(language.value,query.value,
        WBEM_FLAG_FORWARD_ONLY|WBEM_FLAG_RETURN_IMMEDIATELY,nullptr,&processes))) return unavailable();
    const auto deadline=GetTickCount64()+3000;
    while(GetTickCount64()<deadline) {
        ComPtr<IWbemClassObject> process;
        ULONG returned{};
        const HRESULT next=processes->Next(2000,1,&process,&returned);
        if(FAILED(next) || next==WBEM_S_TIMEDOUT) return unavailable();
        if(!returned) return result;
        Property pid,command;
        if(FAILED(process->Get(L"ProcessId",0,&pid.value,nullptr,nullptr)) ||
            FAILED(process->Get(L"CommandLine",0,&command.value,nullptr,nullptr)) ||
            command.value.vt!=VT_BSTR || !command.value.bstrVal) {
            result.complete=false;
            result.warning=L"部分 hl.exe 进程信息不可读；未确认的进程不会用于自动识别。";
            continue;
        }
        if(!IsConditionZeroCommandLine(command.value.bstrVal)) continue;
        DWORD processId{};
        if(pid.value.vt==VT_I4 && pid.value.lVal>0) processId=static_cast<DWORD>(pid.value.lVal);
        else if(pid.value.vt==VT_UI4) processId=pid.value.ulVal;
        if(!processId) continue;
        HANDLE handle=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,processId);
        if(!handle) { result.complete=false; result.warning=L"无法读取零点行动进程路径；请检查游戏与辅助的运行权限。"; continue; }
        wchar_t executable[32768]{}; DWORD size=32768,exitCode{};
        const bool readable=QueryFullProcessImageNameW(handle,0,executable,&size)!=FALSE;
        const bool alive=GetExitCodeProcess(handle,&exitCode)!=FALSE && exitCode==STILL_ACTIVE;
        CloseHandle(handle);
        if(!alive) continue;
        if(!readable) { result.complete=false; result.warning=L"无法读取零点行动进程路径；请检查运行权限。"; continue; }
        const std::wstring fullPath(executable,size);
        const auto slash=fullPath.find_last_of(L"\\/");
        if(slash==std::wstring::npos || _wcsicmp(fullPath.c_str()+slash+1,L"hl.exe")!=0) continue;
        const auto root=NormalizeGamePath(fullPath.substr(0,slash));
        if(IsGameDirectory(root)) result.games.push_back({processId,root});
        else result.warning=L"检测到游戏进程，但目录缺少零点行动文件；请检查安装目录。";
    }
    return unavailable();
}

GameDiscovery DiscoverRunningGames() {
    // Bound the entire operation, including COM activation and WMI connection.
    // The worker owns its promise and COM state; late results cannot change the
    // selected path, touch the UI or install anything after the caller falls back.
    try {
        std::packaged_task<GameDiscovery()> task(DiscoverGamesOnWorker);
        auto result=task.get_future();
        std::thread(std::move(task)).detach();
        if(result.wait_for(std::chrono::seconds(3))==std::future_status::ready) return result.get();
        return {{},L"自动识别游戏目录超时，已使用配置目录；可填写路径后重启辅助。",false};
    } catch(...) {
        return {{},L"自动识别游戏目录失败，已使用配置目录；可填写路径后重启辅助。",false};
    }
}

GamePathChoice SelectGamePath(const std::wstring& argument,const std::wstring& configured,const GameDiscovery& discovered) {
    if(!argument.empty()) return {NormalizeGamePath(argument),GamePathSource::Argument,{}};
    if(!discovered.complete) return {NormalizeGamePath(configured),GamePathSource::Configuration,discovered.warning};
    std::vector<std::wstring> roots;
    for(const auto& game:discovered.games) {
        const auto root=NormalizeGamePath(game.path);
        if(!root.empty() && std::none_of(roots.begin(),roots.end(),[&](const auto& existing) { return SamePath(existing,root); }))
            roots.push_back(root);
    }
    if(roots.size()==1) return {roots.front(),GamePathSource::RunningGame,discovered.warning};
    const auto fallback=NormalizeGamePath(configured);
    if(roots.size()>1) {
        const auto match=std::find_if(roots.begin(),roots.end(),[&](const auto& root) { return SamePath(root,fallback); });
        if(match!=roots.end()) return {*match,GamePathSource::RunningGame,discovered.warning};
        return {{},GamePathSource::Ambiguous,L"检测到多个零点行动目录，请只保留一个游戏进程，或使用 --game-path 指定目录后重启辅助。"};
    }
    return {fallback,GamePathSource::Configuration,discovered.warning};
}
}

#include "session.h"
#include <algorithm>
#include <filesystem>
#include <vector>
namespace cz {
namespace {
bool FileExists(const std::wstring& path) {
    const auto attributes=GetFileAttributesW(path.c_str());
    return attributes!=INVALID_FILE_ATTRIBUTES && !(attributes&FILE_ATTRIBUTE_DIRECTORY);
}
std::wstring ReadStatus(const std::wstring& path) {
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (file==INVALID_HANDLE_VALUE) return {};
    const auto size=GetFileSize(file,nullptr); std::string bytes;
    if (size>0 && size<=65536) {
        bytes.resize(size); DWORD read{};
        if (!ReadFile(file,bytes.data(),size,&read,nullptr) || read!=size) bytes.clear();
    }
    CloseHandle(file);
    if (bytes.empty()) return {};
    const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data(),static_cast<int>(bytes.size()),nullptr,0);
    if (!count) return L"error\n会话状态编码无效。";
    std::wstring result(count,L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data(),static_cast<int>(bytes.size()),result.data(),count);
    return result;
}
}
SetupStatus ParseSetupStatus(const std::wstring& text) {
    if (text.empty()) return {};
    auto end=text.find_first_of(L"\r\n");
    const auto key=text.substr(0,end);
    std::wstring detail;
    if (end!=std::wstring::npos) {
        end=text.find_first_not_of(L"\r\n",end);
        if (end!=std::wstring::npos) detail=text.substr(end);
    }
    const std::pair<const wchar_t*,SetupState> states[]={
        {L"starting",SetupState::Starting},{L"preparing",SetupState::Preparing},{L"ready",SetupState::Ready},
        {L"waiting_game_exit",SetupState::WaitingGameExit},{L"waiting_cleanup",SetupState::WaitingCleanup},
        {L"cleaning",SetupState::Cleaning},{L"cleaned",SetupState::Cleaned},{L"error",SetupState::Error}};
    for (const auto& entry:states) if (key==entry.first) return {entry.second,detail};
    return {SetupState::Error,L"无法识别自动配置进度，请使用完整的同版程序包。"};
}
std::wstring QuoteWindowsArgument(const std::wstring& value) {
    std::wstring result=L"\""; std::size_t slashes{};
    for (wchar_t c:value) {
        if (c==L'\\') { ++slashes; continue; }
        result.append(c==L'"'?slashes*2+1:slashes,L'\\'); slashes=0; result+=c;
    }
    result.append(slashes*2,L'\\'); result+=L'"'; return result;
}
std::wstring NormalizeGamePath(const std::wstring& path) {
    if (path.empty()) return {};
    const DWORD size=GetFullPathNameW(path.c_str(),0,nullptr,nullptr);
    if (!size) return {};
    std::wstring result(size,L'\0');
    const DWORD count=GetFullPathNameW(path.c_str(),size,result.data(),nullptr);
    if (!count || count>=size) return {};
    result.resize(count); std::replace(result.begin(),result.end(),L'/',L'\\');
    while (result.size()>3 && result.back()==L'\\') result.pop_back();
    return result;
}
SessionManager::~SessionManager() {
    // Never kill the guardian: it owns restoration after this UI exits.
    if (worker_) {
        if (WaitForSingleObject(worker_,0)==WAIT_OBJECT_0 && status_.state!=SetupState::Error) DeleteFileW(statusPath_.c_str());
        CloseHandle(worker_);
    }
}
bool SessionManager::Start(const std::wstring& gamePath,const std::wstring& exeDirectory) {
    if (worker_) return false;
    auto fail=[&](const std::wstring& detail) { status_={SetupState::Error,detail}; return false; };
    std::filesystem::path root=exeDirectory;
    for (int i=0;i<3 && !FileExists((root/L"scripts"/L"Session.ps1").wstring());++i) root=root.parent_path();
    const auto script=(root/L"scripts"/L"Session.ps1").wstring();
    if (!FileExists(script)) return fail(L"缺少自动配置文件，请完整解压发布包后运行 CZ_Help.exe。");
    if (!FileExists(gamePath+L"\\hl.exe") || !FileExists(gamePath+L"\\czero\\liblist.gam"))
        return fail(L"找不到零点行动目录，请在设置文件的 [Game] Path 中填写 Half-Life 路径后重启辅助。");
    wchar_t system[MAX_PATH]{},temp[MAX_PATH]{},statusFile[MAX_PATH]{};
    if (!GetSystemDirectoryW(system,MAX_PATH) || !GetTempPathW(MAX_PATH,temp)) return fail(L"无法取得系统运行目录。");
    const auto powershell=std::wstring(system)+L"\\WindowsPowerShell\\v1.0\\powershell.exe";
    const auto tempDirectory=std::wstring(temp)+L"CZ_Help";
    if ((!CreateDirectoryW(tempDirectory.c_str(),nullptr) && GetLastError()!=ERROR_ALREADY_EXISTS) ||
        !GetTempFileNameW(tempDirectory.c_str(),L"czs",0,statusFile)) return fail(L"无法创建临时会话状态文件。");
    statusPath_=statusFile;
    FILETIME created{},exit{},kernel{},user{};
    if (!GetProcessTimes(GetCurrentProcess(),&created,&exit,&kernel,&user)) {
        DeleteFileW(statusPath_.c_str()); return fail(L"无法验证当前辅助进程身份。");
    }
    const auto start=(static_cast<unsigned long long>(created.dwHighDateTime)<<32)|created.dwLowDateTime;
    const auto configuration=std::filesystem::path(exeDirectory).filename().wstring()==L"Debug"?L"Debug":L"Release";
    std::wstring command=QuoteWindowsArgument(powershell)+L" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "+
        QuoteWindowsArgument(script)+L" -GamePath "+QuoteWindowsArgument(gamePath)+L" -Configuration "+configuration+
        L" -OwnerPid "+std::to_wstring(GetCurrentProcessId())+L" -OwnerStartTime "+std::to_wstring(start)+
        L" -StatusPath "+QuoteWindowsArgument(statusPath_)+L" -RemoveStatusOnSuccess";
    STARTUPINFOW startup{}; startup.cb=sizeof(startup); startup.dwFlags=STARTF_USESHOWWINDOW; startup.wShowWindow=SW_HIDE;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(powershell.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,root.c_str(),&startup,&process)) {
        const auto error=GetLastError(); DeleteFileW(statusPath_.c_str());
        return fail(L"无法启动自动配置进程，Windows 错误码："+std::to_wstring(error));
    }
    CloseHandle(process.hThread); worker_=process.hProcess; status_={}; lastPoll_=0; return true;
}
void SessionManager::Poll() {
    if (!worker_) return;
    const auto now=GetTickCount64(); if (now-lastPoll_<250) return; lastPoll_=now;
    const auto content=ReadStatus(statusPath_);
    if (!content.empty()) status_=ParseSetupStatus(content);
    DWORD code{};
    if (GetExitCodeProcess(worker_,&code) && code!=STILL_ACTIVE && status_.state!=SetupState::Error)
        status_={SetupState::Error,L"自动配置进程已退出（代码 "+std::to_wstring(code)+L"），请重启辅助。详情："+statusPath_};
}
std::wstring SessionManager::Message() const {
    switch (status_.state) {
    case SetupState::Starting: case SetupState::Preparing: return L"正在自动配置，请稍候…";
    case SetupState::Ready: return L"准备就绪 · 等待游戏启动";
    case SetupState::WaitingGameExit: return L"等待游戏退出后自动配置；完成后可重新启动游戏。";
    case SetupState::WaitingCleanup: return L"等待上次会话清理；请退出游戏，随后会自动准备。";
    case SetupState::Cleaning: return L"正在恢复游戏配置并清理插件…";
    case SetupState::Cleaned: return L"插件已清理。";
    case SetupState::Error: return L"自动配置未完成："+status_.detail;
    }
    return {};
}
}

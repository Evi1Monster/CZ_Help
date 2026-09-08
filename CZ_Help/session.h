#pragma once
#include <windows.h>
#include <string>

namespace cz {
enum class SetupState { Starting, Preparing, Ready, WaitingGameExit, WaitingCleanup, Cleaning, Cleaned, Error };
struct SetupStatus { SetupState state{SetupState::Starting}; std::wstring detail; };
SetupStatus ParseSetupStatus(const std::wstring& text);
std::wstring QuoteWindowsArgument(const std::wstring& value);
std::wstring NormalizeGamePath(const std::wstring& path);
class SessionManager {
public:
    SessionManager()=default;
    SessionManager(const SessionManager&)=delete;
    SessionManager& operator=(const SessionManager&)=delete;
    ~SessionManager();
    bool Start(const std::wstring& gamePath,const std::wstring& exeDirectory);
    void Poll();
    const SetupStatus& Status() const { return status_; }
    bool Ready() const { return status_.state==SetupState::Ready; }
    std::wstring Message() const;
private:
    HANDLE worker_{};
    std::wstring statusPath_;
    SetupStatus status_{};
    ULONGLONG lastPoll_{};
};
}

#pragma once
#include "protocol.h"
#include <windows.h>
#include <cstdio>
#include <cstring>

namespace cz {
inline void ChannelNames(std::uint32_t pid, wchar_t (&mapping)[80], wchar_t (&mutex)[80]) {
    swprintf_s(mapping,L"Local\\CZHelp.Snapshot.v1.%lu",static_cast<unsigned long>(pid));
    swprintf_s(mutex,L"Local\\CZHelp.Lock.v1.%lu",static_cast<unsigned long>(pid));
}
class Channel {
public:
    Channel() = default;
    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;
    ~Channel() { Close(); }
    void Close() {
        if (view_) UnmapViewOfFile(view_);
        if (mapping_) CloseHandle(mapping_);
        if (mutex_) CloseHandle(mutex_);
        view_=nullptr; mapping_=mutex_=nullptr;
    }
protected:
    HANDLE mapping_{}, mutex_{};
    void* view_{};
    bool Lock() {
        if (!view_ || !mutex_) return false;
        DWORD result=WaitForSingleObject(mutex_,0);
        if (result==WAIT_ABANDONED) { ReleaseMutex(mutex_); return false; }
        return result==WAIT_OBJECT_0;
    }
};
class WriteChannel final : public Channel {
public:
    bool Open(std::uint32_t pid) {
        Close(); wchar_t mapping[80]{},mutex[80]{}; ChannelNames(pid,mapping,mutex);
        mutex_=CreateMutexW(nullptr,FALSE,mutex);
        mapping_=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Snapshot),mapping);
        if (mapping_) view_=MapViewOfFile(mapping_,FILE_MAP_WRITE,0,0,sizeof(Snapshot));
        if (!mutex_ || !view_) { Close(); return false; }
        return true;
    }
    bool Publish(const Snapshot& snapshot) {
        if (!Lock()) return false;
        std::memcpy(view_,&snapshot,sizeof(snapshot));
        ReleaseMutex(mutex_); return true;
    }
};
class ReadChannel final : public Channel {
public:
    bool Open(std::uint32_t pid) {
        Close(); wchar_t mapping[80]{},mutex[80]{}; ChannelNames(pid,mapping,mutex);
        mutex_=OpenMutexW(SYNCHRONIZE|MUTEX_MODIFY_STATE,FALSE,mutex);
        mapping_=OpenFileMappingW(FILE_MAP_READ,FALSE,mapping);
        if (mapping_) view_=MapViewOfFile(mapping_,FILE_MAP_READ,0,0,sizeof(Snapshot));
        if (!mutex_ || !view_) { Close(); return false; }
        return true;
    }
    bool Read(Snapshot& snapshot) {
        if (!Lock()) return false;
        std::memcpy(&snapshot,view_,sizeof(snapshot));
        ReleaseMutex(mutex_); return true;
    }
};
}

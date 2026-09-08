#pragma once
#include "channel.h"
#include <cstdint>

namespace cz {
inline constexpr std::uint32_t kRenderMagic=0x435A5243;
inline constexpr std::uint32_t kRenderVersion=1;
enum RenderFlags : std::uint32_t { RenderEnabled=1, RenderBoxes=2, RenderHealth=4 };
inline constexpr std::uint32_t kProjectionWidescreen=1;
#pragma pack(push,4)
struct RenderControl {
    std::uint32_t magic{kRenderMagic},version{kRenderVersion},bytes{40},processId{};
    std::uint64_t timestampMs{};
    std::uint32_t flags{},reserved[3]{};
};
struct RenderStatus {
    std::uint32_t magic{kRenderMagic},version{kRenderVersion},bytes{40},processId{};
    std::uint64_t timestampMs{};
    // 1: OpenGL game renderer is presenting, 2: renderer unavailable.
    // Uses the previously reserved word without changing the v1 wire layout.
    std::uint32_t state{},width{},height{},projectionFlags{};
};
#pragma pack(pop)
static_assert(sizeof(RenderControl)==40 && sizeof(RenderStatus)==40);
inline bool ValidRenderControl(const RenderControl& c,std::uint32_t pid,std::uint64_t now) {
    return c.magic==kRenderMagic && c.version==kRenderVersion && c.bytes==sizeof(c) && c.processId==pid &&
        c.timestampMs>0 && c.timestampMs<=now && now-c.timestampMs<=kStaleMs && !(c.flags&~7u);
}
inline bool ValidRenderStatus(const RenderStatus& s,std::uint32_t pid,std::uint64_t now) {
    return s.magic==kRenderMagic && s.version==kRenderVersion && s.bytes==sizeof(s) && s.processId==pid &&
        s.timestampMs>0 && s.timestampMs<=now && now-s.timestampMs<=kStaleMs && (s.state==1 || s.state==2) &&
        !(s.projectionFlags&~kProjectionWidescreen);
}
template<typename T> class RenderMailbox : public Channel {
public:
    bool Open(std::uint32_t pid,const wchar_t* kind,bool create) {
        Close(); wchar_t map[96]{},lock[96]{};
        swprintf_s(map,L"Local\\CZHelp.Render.%s.v1.%lu",kind,static_cast<unsigned long>(pid));
        swprintf_s(lock,L"Local\\CZHelp.RenderLock.%s.v1.%lu",kind,static_cast<unsigned long>(pid));
        mutex_=create?CreateMutexW(nullptr,FALSE,lock):OpenMutexW(SYNCHRONIZE|MUTEX_MODIFY_STATE,FALSE,lock);
        mapping_=create?CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(T),map):OpenFileMappingW(FILE_MAP_READ,FALSE,map);
        if(mapping_) view_=MapViewOfFile(mapping_,create?FILE_MAP_WRITE:FILE_MAP_READ,0,0,sizeof(T));
        writable_=create;
        if(!mutex_ || !view_) { Close(); return false; }
        return true;
    }
    bool Publish(const T& value) {
        if(!writable_ || !Lock()) return false;
        std::memcpy(view_,&value,sizeof(T)); ReleaseMutex(mutex_); return true;
    }
    bool Read(T& value) {
        if(!Lock()) return false;
        std::memcpy(&value,view_,sizeof(T)); ReleaseMutex(mutex_); return true;
    }
private:
    bool writable_{};
};
using RenderControlMailbox=RenderMailbox<RenderControl>;
using RenderStatusMailbox=RenderMailbox<RenderStatus>;
}

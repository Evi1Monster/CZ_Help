#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>

namespace cz::render {
template<class To,class From> To PointerCast(From value) {
    static_assert(sizeof(To)==sizeof(From)); To result{}; std::memcpy(&result,&value,sizeof(result)); return result;
}
// Only operates on a named import of an already loaded image in this process.
// It refuses an unexpected original target and never overwrites a later hook.
class IatHook {
public:
    bool Install(HMODULE module,const char* library,const char* function,void* expected,void* replacement) {
        if(slot_ || !module || !expected || !replacement) return false;
        base_=reinterpret_cast<unsigned char*>(module);
        if(!Readable(0,sizeof(IMAGE_DOS_HEADER))) return false;
        auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base_);
        if(dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<=0 || dos->e_lfanew>1024*1024) return false;
        auto offset=static_cast<std::size_t>(dos->e_lfanew);
        if(!Readable(offset,sizeof(IMAGE_NT_HEADERS))) return false;
        auto nt=reinterpret_cast<const IMAGE_NT_HEADERS*>(base_+offset);
        if(nt->Signature!=IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR_MAGIC ||
            nt->OptionalHeader.NumberOfRvaAndSizes<=IMAGE_DIRECTORY_ENTRY_IMPORT) return false;
        size_=nt->OptionalHeader.SizeOfImage;
        const auto dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if(!dir.VirtualAddress || dir.Size<sizeof(IMAGE_IMPORT_DESCRIPTOR) || !Range(dir.VirtualAddress,dir.Size)) return false;
        for(std::size_t pos=0;pos+sizeof(IMAGE_IMPORT_DESCRIPTOR)<=dir.Size;pos+=sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
            auto descriptor=reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base_+dir.VirtualAddress+pos);
            if(!descriptor->Name) break;
            const char* name=String(descriptor->Name);
            if(!name || _stricmp(name,library)!=0) continue;
            if(!descriptor->OriginalFirstThunk || !descriptor->FirstThunk) return false;
            for(std::size_t i=0;i<size_/sizeof(IMAGE_THUNK_DATA);++i) {
                const std::size_t lookup=descriptor->OriginalFirstThunk+i*sizeof(IMAGE_THUNK_DATA);
                const std::size_t target=descriptor->FirstThunk+i*sizeof(IMAGE_THUNK_DATA);
                if(!Range(lookup,sizeof(IMAGE_THUNK_DATA)) || !Range(target,sizeof(IMAGE_THUNK_DATA))) return false;
                auto thunk=reinterpret_cast<const IMAGE_THUNK_DATA*>(base_+lookup);
                if(!thunk->u1.AddressOfData) return false;
                if(IMAGE_SNAP_BY_ORDINAL(thunk->u1.Ordinal)) continue;
                const char* imported=String(static_cast<std::size_t>(thunk->u1.AddressOfData)+offsetof(IMAGE_IMPORT_BY_NAME,Name));
                if(!imported || std::strcmp(imported,function)!=0) continue;
                auto candidate=reinterpret_cast<void**>(base_+target);
                if(reinterpret_cast<std::uintptr_t>(candidate)%alignof(void*) || *candidate!=expected) return false;
                if(!Exchange(candidate,expected,replacement)) return false;
                slot_=candidate; original_=expected; replacement_=replacement; return true;
            }
        }
        return false;
    }
    bool Restore() {
        if(!slot_) return true;
        if(!Readable(static_cast<std::size_t>(reinterpret_cast<unsigned char*>(slot_)-base_),sizeof(void*))) return false;
        // Keep the record while another hook owns the slot. If that hook later
        // restores our callback, a subsequent Shutdown can restore the original.
        if(*slot_!=replacement_) { if(*slot_==original_) slot_=nullptr; return true; }
        if(!Exchange(slot_,replacement_,original_)) return false;
        slot_=nullptr; return true;
    }
private:
    unsigned char* base_{}; std::size_t size_{}; void** slot_{}; void* original_{}; void* replacement_{};
    bool Readable(std::size_t offset,std::size_t bytes) const {
        const auto start=reinterpret_cast<std::uintptr_t>(base_);
        if(offset>UINTPTR_MAX-start || bytes>UINTPTR_MAX-start-offset) return false;
        auto at=start+offset; const auto end=at+bytes;
        while(at<end) {
            MEMORY_BASIC_INFORMATION info{};
            if(!VirtualQuery(reinterpret_cast<void*>(at),&info,sizeof(info)) || info.AllocationBase!=base_ ||
                info.State!=MEM_COMMIT || (info.Protect&(PAGE_GUARD|PAGE_NOACCESS))) return false;
            auto next=reinterpret_cast<std::uintptr_t>(info.BaseAddress)+info.RegionSize;
            if(next<=at) return false; at=next;
        }
        return true;
    }
    bool Range(std::size_t offset,std::size_t bytes) const { return offset<=size_ && bytes<=size_-offset && Readable(offset,bytes); }
    const char* String(std::size_t offset) const {
        for(std::size_t length=0;length<4096;++length) {
            if(!Range(offset+length,1)) return nullptr;
            if(base_[offset+length]==0) return reinterpret_cast<const char*>(base_+offset);
        }
        return nullptr;
    }
    static bool Exchange(void** slot,void* expected,void* value) {
        DWORD protect{}; if(!VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&protect)) return false;
        void* previous=InterlockedCompareExchangePointer(slot,value,expected);
        DWORD unused{};
        if(!VirtualProtect(slot,sizeof(void*),protect,&unused)) {
            if(previous==expected) InterlockedCompareExchangePointer(slot,expected,value);
            VirtualProtect(slot,sizeof(void*),protect,&unused); return false;
        }
        return previous==expected;
    }
};
}

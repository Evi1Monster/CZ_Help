#include "bhop_control.h"
#include <cstdio>
#include <initializer_list>
int main() {
    const auto pid=GetCurrentProcessId(); cz::BhopControl c{}; c.processId=pid; c.timestampMs=1000; c.flags=3;
    c.mode=static_cast<cz::JumpMode>(99);
    if(cz::ValidBhopControl(c,pid,1100)) { std::puts("FAIL: invalid jump mode accepted"); return 11; }
    for(unsigned removed:{1u,2u,3u,5u}) {
        c.mode=static_cast<cz::JumpMode>(removed);
        if(cz::ValidBhopControl(c,pid,1100)) { std::puts("FAIL: removed mode accepted by control protocol"); return 15; }
        cz::BhopStatus rejected{}; rejected.processId=pid; rejected.timestampMs=1000; rejected.mode=c.mode;
        if(cz::ValidBhopStatus(rejected,pid,1100)) return 16;
    }
    c.mode=cz::JumpMode::BhopJump; c.generation=42;
    if(static_cast<unsigned>(c.mode)!=4) return 17;
    c.version=1; if(cz::ValidBhopControl(c,pid,1100)) { std::puts("FAIL: old mode-less packet accepted"); return 12; }
    c.version=cz::kBhopVersion;
    if(!cz::ValidBhopControl(c,pid,1100)) { std::puts("FAIL: fresh held-space control must be accepted"); return 1; }
    if(cz::ValidBhopControl(c,pid,1151) || cz::ValidBhopControl(c,pid+1,1100) || cz::ValidBhopControl(c,pid,999)) return 2;
    c.flags=4; if(cz::ValidBhopControl(c,pid,1100)) return 3;
    c.flags=0; if(!cz::ValidBhopControl(c,pid,1100)) return 4;
    cz::BhopControlMailbox writer,reader;
    if(!writer.Open(pid,L"TestBhopControl",true) || !reader.Open(pid,L"TestBhopControl",false) || !writer.Publish(c)) return 5;
    cz::BhopControl read{}; if(!reader.Read(read) || read.flags!=0 || reader.Publish(c)) return 6;
    c.flags=3; if(!writer.Publish(c) || !reader.Read(read) || read.flags!=3) return 7;
    if(read.mode!=cz::JumpMode::BhopJump || read.generation!=42) return 13;
    cz::BhopStatus s{}; s.processId=pid; s.timestampMs=1000; s.flags=cz::BhopAvailable;
    if(!cz::ValidBhopStatus(s,pid,1200) || cz::ValidBhopStatus(s,pid,1501)) return 8;
    s.mode=static_cast<cz::JumpMode>(99); if(cz::ValidBhopStatus(s,pid,1200)) return 14;
    s.mode=cz::JumpMode::AutoJump; s.version=1; if(cz::ValidBhopStatus(s,pid,1200)) return 9;
    reader.Close(); writer.Close(); if(reader.Open(pid,L"TestBhopControl",false)) return 10;
    std::puts("PASS: bhop control identity, deadline, off/held states, mapping and status capability");
}

#include "channel.h"
#include "core.h"
#include <iostream>
#include <thread>
#include <atomic>
int main() {
    const auto pid=GetCurrentProcessId(); cz::WriteChannel writer; cz::ReadChannel reader;
    if (reader.Open(pid)) { std::cerr<<"Unexpected pre-existing test channel\n"; return 1; }
    if (!writer.Open(pid) || !reader.Open(pid)) return 2;
    cz::Snapshot s{}; s.bytes=sizeof(s); s.processId=pid; s.timestampMs=GetTickCount64(); s.playerCount=32;
    if (!writer.Publish(s)) return 3;
    cz::Snapshot r{}; if (!reader.Read(r) || !cz::ValidSnapshot(r,pid,GetTickCount64())) return 4;
    std::atomic<bool> done=false; std::atomic<int> bad=0;
    std::thread producer([&]{
        for (int generation=1;generation<=10000;++generation) {
            for (auto& player:s.players) player.health=static_cast<float>(generation);
            writer.Publish(s);
        }
        done=true;
    });
    int reads=0;
    do {
        if (reader.Read(r)) { ++reads; for (const auto& p:r.players) if(p.health!=r.players[0].health) ++bad; }
    } while (!done);
    producer.join();
    if (bad || !reads) { std::cerr<<"Torn or missing read\n"; return 5; }
    // A busy producer lock must not block the reader or make a partial copy.
    wchar_t mapping[80]{},lockName[80]{}; cz::ChannelNames(pid,mapping,lockName);
    HANDLE lock=OpenMutexW(SYNCHRONIZE|MUTEX_MODIFY_STATE,FALSE,lockName);
    if (!lock || WaitForSingleObject(lock,0)!=WAIT_OBJECT_0) return 6;
    bool readWhileLocked=true;
    std::thread blocked([&]{ cz::Snapshot ignored{}; readWhileLocked=reader.Read(ignored); });
    blocked.join(); ReleaseMutex(lock); CloseHandle(lock);
    if (readWhileLocked) return 7;
    s.timestampMs=GetTickCount64()-cz::kStaleMs-1; writer.Publish(s); reader.Read(r);
    if (cz::ValidSnapshot(r,pid,GetTickCount64())) return 8;
    reader.Close(); writer.Close();
    if (reader.Open(pid)) return 9;
    std::cout<<"PASS: complete cross-thread snapshots, nonblocking lock, stale rejection, channel cleanup\n";
}

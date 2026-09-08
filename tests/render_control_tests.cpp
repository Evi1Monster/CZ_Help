#include "render_control.h"
#include <iostream>
int main() {
    const auto pid=GetCurrentProcessId();
    cz::RenderControl c{}; c.processId=pid; c.timestampMs=1000; c.flags=cz::RenderEnabled|cz::RenderHealth;
    if(!cz::ValidRenderControl(c,pid,1200)) return 1;
    if(cz::ValidRenderControl(c,pid,1501) || cz::ValidRenderControl(c,pid+1,1200)) return 2;
    c.flags=128; if(cz::ValidRenderControl(c,pid,1200)) return 3;
    c.flags=cz::RenderHealth;
    cz::RenderControlMailbox writer,reader;
    if(!writer.Open(pid,L"TestControl",true) || !reader.Open(pid,L"TestControl",false) || !writer.Publish(c)) return 4;
    cz::RenderControl read{};
    if(!reader.Read(read) || read.flags!=cz::RenderHealth || (read.flags&cz::RenderEnabled)) return 5;
    if(reader.Publish(c)) return 6;
    c.flags=cz::RenderEnabled|cz::RenderBoxes;
    if(!writer.Publish(c) || !reader.Read(read) || read.flags!=(cz::RenderEnabled|cz::RenderBoxes)) return 7;
    cz::RenderStatus s{}; s.processId=pid; s.timestampMs=1000; s.state=1; s.width=1920; s.height=1080;
    if(!cz::ValidRenderStatus(s,pid,1200) || cz::ValidRenderStatus(s,pid,1600)) return 8;
    s.projectionFlags=cz::kProjectionWidescreen;
    if(!cz::ValidRenderStatus(s,pid,1200)) return 11;
    s.projectionFlags=2;
    if(cz::ValidRenderStatus(s,pid,1200)) return 12;
    s.projectionFlags=0;
    s.version=99; if(cz::ValidRenderStatus(s,pid,1200)) return 9;
    reader.Close(); writer.Close();
    if(reader.Open(pid,L"TestControl",false)) return 10;
    std::cout<<"PASS: independent box/number options, off state, identity, timeout, read-only receiver and version checks\n";
}

#include "render_hook.h"
#include "iat_hook.h"
#include "core.h"
#include "render_control.h"
#include <GL/gl.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace cz::render {
namespace {
using SwapProc=BOOL(WINAPI*)(HDC);
using UseProgramProc=void(APIENTRY*)(GLuint);
using ActiveTextureProc=void(APIENTRY*)(GLenum);
using BindFramebufferProc=void(APIENTRY*)(GLenum,GLuint);
using BindProgramPipelineProc=void(APIENTRY*)(GLuint);
constexpr GLenum kCurrentProgram=0x8B8D,kActiveTexture=0x84E0,kTexture0=0x84C0,kMaxTextureUnits=0x84E2;
constexpr GLenum kFramebuffer=0x8D40,kDrawFramebuffer=0x8CA9,kDrawFramebufferBinding=0x8CA6;
constexpr GLenum kTexture3d=0x806F,kCubeMap=0x8513;
SRWLOCK stateLock=SRWLOCK_INIT,snapshotLock=SRWLOCK_INIT;
Snapshot latest{};
std::atomic_bool widescreenFov{};
RenderControlMailbox controls; RenderStatusMailbox statuses;
RenderControl lastControl{};
bool controlOpen{},statusOpen{},stopped{},installed{};
std::uint64_t nextControlOpen{};
IatHook hook;
std::atomic<SwapProc> originalSwap{};

template<class T> T Proc(const char* name) {
    auto proc=wglGetProcAddress(name); const auto value=PointerCast<std::uintptr_t>(proc);
    return value<=3 || value==UINTPTR_MAX ? nullptr : PointerCast<T>(proc);
}
bool Extension(const char* all,const char* name) {
    if(!all) return false;
    const std::size_t len=std::strlen(name);
    for(const char* match=std::strstr(all,name);match;match=std::strstr(match+len,name))
        if((match==all || match[-1]==' ') && (match[len]==' ' || !match[len])) return true;
    return false;
}
struct GlFunctions {
    UseProgramProc useProgram{}; ActiveTextureProc activeTexture{}; BindFramebufferProc bindFramebuffer{};
    BindProgramPipelineProc bindProgramPipeline{};
    bool splitFramebuffer{},texture3d{},cubeMap{},srgb{},multisample{},rasterizerDiscard{},vertexProgram{},fragmentProgram{};
    bool Init() {
        auto version=reinterpret_cast<const char*>(glGetString(GL_VERSION)); if(!version) return false;
        int major{},minor{}; if(sscanf_s(version,"%d.%d",&major,&minor)!=2) return false;
        if(major>3 || (major==3 && minor>=2)) {
            GLint profile{}; glGetIntegerv(0x9126,&profile); if((profile&1) && !(profile&2)) return false;
        }
        const char* extensions=reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
        if(major>=2) { useProgram=Proc<UseProgramProc>("glUseProgram"); if(!useProgram) return false; }
        if(major>4 || (major==4 && minor>=1) || Extension(extensions,"GL_ARB_separate_shader_objects")) {
            bindProgramPipeline=Proc<BindProgramPipelineProc>("glBindProgramPipeline"); if(!bindProgramPipeline) return false;
        }
        if(major>1 || minor>=3 || Extension(extensions,"GL_ARB_multitexture")) {
            activeTexture=Proc<ActiveTextureProc>("glActiveTexture");
            if(!activeTexture) activeTexture=Proc<ActiveTextureProc>("glActiveTextureARB");
            if(!activeTexture) return false;
        }
        splitFramebuffer=major>=3 || Extension(extensions,"GL_ARB_framebuffer_object");
        if(splitFramebuffer) bindFramebuffer=Proc<BindFramebufferProc>("glBindFramebuffer");
        else if(Extension(extensions,"GL_EXT_framebuffer_object")) bindFramebuffer=Proc<BindFramebufferProc>("glBindFramebufferEXT");
        if((splitFramebuffer || Extension(extensions,"GL_EXT_framebuffer_object")) && !bindFramebuffer) return false;
        texture3d=major>1 || minor>=2 || Extension(extensions,"GL_EXT_texture3D");
        cubeMap=major>1 || minor>=3 || Extension(extensions,"GL_ARB_texture_cube_map");
        srgb=major>=3 || Extension(extensions,"GL_EXT_framebuffer_sRGB") || Extension(extensions,"GL_ARB_framebuffer_sRGB");
        multisample=major>1 || minor>=3 || Extension(extensions,"GL_ARB_multisample");
        rasterizerDiscard=major>=3;
        vertexProgram=Extension(extensions,"GL_ARB_vertex_program"); fragmentProgram=Extension(extensions,"GL_ARB_fragment_program");
        return true;
    }
};
class GlState {
public:
    explicit GlState(const GlFunctions& functions): f(functions) {}
    bool Begin(int width,int height) {
        GLint depth{},maximum{};
        glGetIntegerv(GL_ATTRIB_STACK_DEPTH,&depth); glGetIntegerv(GL_MAX_ATTRIB_STACK_DEPTH,&maximum); if(depth>=maximum) return false;
        glGetIntegerv(GL_MODELVIEW_STACK_DEPTH,&depth); glGetIntegerv(GL_MAX_MODELVIEW_STACK_DEPTH,&maximum); if(depth>=maximum) return false;
        glGetIntegerv(GL_PROJECTION_STACK_DEPTH,&depth); glGetIntegerv(GL_MAX_PROJECTION_STACK_DEPTH,&maximum); if(depth>=maximum) return false;
        glGetIntegerv(GL_MATRIX_MODE,&matrixMode); glGetIntegerv(GL_VIEWPORT,viewport);
        if(f.useProgram) glGetIntegerv(kCurrentProgram,&program);
        if(f.bindProgramPipeline) glGetIntegerv(0x825A,&pipeline);
        if(f.activeTexture) glGetIntegerv(kActiveTexture,&texture);
        if(f.bindFramebuffer) glGetIntegerv(kDrawFramebufferBinding,&framebuffer);
        glPushAttrib(GL_ALL_ATTRIB_BITS);
        if(f.useProgram) f.useProgram(0);
        if(f.bindProgramPipeline) f.bindProgramPipeline(0);
        if(f.bindFramebuffer) f.bindFramebuffer(f.splitFramebuffer?kDrawFramebuffer:kFramebuffer,0);
        GLboolean doubleBuffered{}; glGetBooleanv(GL_DOUBLEBUFFER,&doubleBuffered); glDrawBuffer(doubleBuffered?GL_BACK:GL_FRONT);
        GLint units=1; if(f.activeTexture) glGetIntegerv(kMaxTextureUnits,&units);
        for(GLint unit=0;unit<units;++unit) {
            if(f.activeTexture) f.activeTexture(kTexture0+unit);
            glDisable(GL_TEXTURE_1D); glDisable(GL_TEXTURE_2D);
            if(f.texture3d) glDisable(kTexture3d); if(f.cubeMap) glDisable(kCubeMap);
        }
        glDisable(GL_ALPHA_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE); glDisable(GL_DEPTH_TEST);
        glDisable(GL_STENCIL_TEST); glDisable(GL_FOG); glDisable(GL_LIGHTING); glDisable(GL_SCISSOR_TEST);
        glDisable(GL_DITHER); glDisable(GL_COLOR_LOGIC_OP); glDisable(GL_POLYGON_STIPPLE); glDisable(GL_LINE_STIPPLE);
        glDisable(GL_POLYGON_SMOOTH); glDisable(GL_LINE_SMOOTH);
        GLint clips{}; glGetIntegerv(GL_MAX_CLIP_PLANES,&clips); for(GLint i=0;i<clips;++i) glDisable(GL_CLIP_PLANE0+i);
        if(f.srgb) glDisable(0x8DB9); if(f.multisample) glDisable(0x809D); if(f.rasterizerDiscard) glDisable(0x8C89);
        if(f.vertexProgram) glDisable(0x8620); if(f.fragmentProgram) glDisable(0x8804);
        glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE); glDepthMask(GL_FALSE); glPolygonMode(GL_FRONT_AND_BACK,GL_FILL); glShadeModel(GL_FLAT);
        glViewport(0,0,width,height);
        glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); glOrtho(0,width,height,0,-1,1);
        glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity(); active=true; return true;
    }
    ~GlState() {
        if(!active) return;
        glMatrixMode(GL_MODELVIEW); glPopMatrix(); glMatrixMode(GL_PROJECTION); glPopMatrix();
        // Restore the original framebuffer before its saved draw-buffer selector.
        if(f.bindFramebuffer) f.bindFramebuffer(f.splitFramebuffer?kDrawFramebuffer:kFramebuffer,framebuffer);
        glPopAttrib();
        if(f.useProgram) f.useProgram(program);
        if(f.bindProgramPipeline) f.bindProgramPipeline(pipeline);
        if(f.activeTexture) f.activeTexture(texture);
        glViewport(viewport[0],viewport[1],viewport[2],viewport[3]); glMatrixMode(matrixMode);
    }
private:
    const GlFunctions& f; GLint matrixMode{},program{},pipeline{},texture{},framebuffer{},viewport[4]{}; bool active{};
};
void Quad(float x,float y,float right,float bottom) {
    glVertex2f(x,y); glVertex2f(right,y); glVertex2f(right,bottom); glVertex2f(x,bottom);
}
void Box(const Rect& r) {
    glColor3ub(255,183,74); glBegin(GL_QUADS);
    Quad(r.left,r.top,r.right,r.top+2); Quad(r.left,r.bottom-2,r.right,r.bottom);
    Quad(r.left,r.top,r.left+2,r.bottom); Quad(r.right-2,r.top,r.right,r.bottom); glEnd();
}
void Health(const Player& player,const Rect& r,int width) {
    static constexpr unsigned char glyphs[12][7]={
        {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},{30,1,1,14,1,1,30},{2,6,10,18,31,2,2},
        {31,16,16,30,1,1,30},{14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},{14,17,17,15,1,1,14},
        {17,17,17,31,17,17,17},{30,17,17,30,16,16,16}};
    char text[16]{}; sprintf_s(text,"%d HP",static_cast<int>(std::ceil(std::min(player.health,99999.0f))));
    const float ratio=std::clamp(player.health/player.maxHealth,0.0f,1.0f);
    const float length=static_cast<float>(std::strlen(text))*12-2;
    const float start=std::clamp(r.left,0.0f,std::max(0.0f,width-length)),top=std::max(0.0f,r.top-18);
    for(int pass=0;pass<2;++pass) {
        if(pass==0) glColor3ub(1,1,1); else glColor3ub(static_cast<GLubyte>(255*(1-ratio)),static_cast<GLubyte>(220*ratio),72);
        const float shadow=pass==0?1.0f:0.0f; float x=start+shadow; glBegin(GL_QUADS);
        for(char character:text) {
            if(!character) break;
            int glyph=character>='0' && character<='9'?character-'0':character=='H'?10:character=='P'?11:-1;
            if(glyph>=0) for(int row=0;row<7;++row) for(int column=0;column<5;++column)
                if(glyphs[glyph][row]&(1<<(4-column))) Quad(x+column*2,top+row*2+shadow,x+column*2+2,top+row*2+shadow+2);
            x+=12;
        }
        glEnd();
    }
}
void PublishStatus(std::uint32_t state,int width=0,int height=0) {
    const auto pid=GetCurrentProcessId();
    if(!statusOpen) statusOpen=statuses.Open(pid,L"Status",true);
    if(statusOpen) {
        RenderStatus status{}; status.processId=pid; status.timestampMs=GetTickCount64(); status.state=state;
        status.width=static_cast<std::uint32_t>(width); status.height=static_cast<std::uint32_t>(height);
        if(widescreenFov.load(std::memory_order_relaxed)) status.projectionFlags|=kProjectionWidescreen;
        statuses.Publish(status);
    }
}
BOOL WINAPI OnSwapBuffers(HDC dc) {
    BeforePresent(dc);
    const auto original=originalSwap.load(std::memory_order_acquire); return original?original(dc):FALSE;
}
}

void Update(const Snapshot& snapshot,bool useWidescreenFov) {
    AcquireSRWLockExclusive(&snapshotLock); latest=snapshot;
    widescreenFov.store(useWidescreenFov,std::memory_order_relaxed); ReleaseSRWLockExclusive(&snapshotLock);
}
void BeforePresent(HDC dc) {
    if(!TryAcquireSRWLockExclusive(&stateLock)) return;
    struct Unlock { ~Unlock() { ReleaseSRWLockExclusive(&stateLock); } } unlock;
    if(stopped) return;
    HWND window=WindowFromDC(dc); DWORD windowPid{}; if(window) GetWindowThreadProcessId(window,&windowPid);
    RECT client{}; GlFunctions functions;
    if(!dc || windowPid!=GetCurrentProcessId() || !wglGetCurrentContext() || wglGetCurrentDC()!=dc ||
        !GetClientRect(window,&client) || client.right<=0 || client.bottom<=0 || !functions.Init()) { PublishStatus(2); return; }
    PublishStatus(1,client.right,client.bottom);
    const auto now=GetTickCount64();
    if(!controlOpen && now>=nextControlOpen) { controlOpen=controls.Open(GetCurrentProcessId(),L"Control",false); nextControlOpen=now+250; }
    RenderControl incoming{}; if(controlOpen && controls.Read(incoming)) lastControl=incoming;
    if(!ValidRenderControl(lastControl,GetCurrentProcessId(),GetTickCount64()) || !(lastControl.flags&RenderEnabled)) return;
    HWND foreground=GetForegroundWindow(); DWORD foregroundPid{}; if(foreground) GetWindowThreadProcessId(foreground,&foregroundPid);
    if(foregroundPid!=GetCurrentProcessId() || GetAncestor(window,GA_ROOT)!=GetAncestor(foreground,GA_ROOT)) return;
    if(!TryAcquireSRWLockShared(&snapshotLock)) return;
    const Snapshot snapshot=latest; const bool useWidescreenFov=widescreenFov.load(std::memory_order_relaxed);
    ReleaseSRWLockShared(&snapshotLock);
    if(!ValidSnapshot(snapshot,GetCurrentProcessId(),GetTickCount64()) || snapshot.status!=SessionStatus::Active) return;
    GlState saved(functions); if(!saved.Begin(client.right,client.bottom)) return;
    const auto camera=ResolveProjectionCamera(snapshot.camera,{client.right,client.bottom},useWidescreenFov);
    for(std::uint32_t i=0;i<snapshot.playerCount;++i) {
        const Player& player=snapshot.players[i]; Rect rect{};
        if(!IsTarget(snapshot,player) || !ProjectBox(camera,player,{client.right,client.bottom},rect)) continue;
        if(lastControl.flags&RenderBoxes) Box(rect);
        if(lastControl.flags&RenderHealth) Health(player,rect,client.right);
    }
}
bool Install() {
    AcquireSRWLockExclusive(&stateLock);
    struct Unlock { ~Unlock() { ReleaseSRWLockExclusive(&stateLock); } } unlock;
    if(installed) return true;
    stopped=false;
    HMODULE sdl=GetModuleHandleW(L"SDL2.dll"); HMODULE gdi=GetModuleHandleW(L"gdi32.dll");
    const auto original=gdi?GetProcAddress(gdi,"SwapBuffers"):nullptr;
    if(!sdl || !original) { PublishStatus(2); return false; }
    // IAT restoration cannot revoke a function pointer another thread already read.
    // Pin this DLL before exposing its callback; its code remains valid until exit.
    HMODULE pinned{};
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        PointerCast<LPCWSTR>(&OnSwapBuffers),&pinned)) { PublishStatus(2); return false; }
    originalSwap.store(PointerCast<SwapProc>(original),std::memory_order_release);
    installed=hook.Install(sdl,"GDI32.dll","SwapBuffers",PointerCast<void*>(original),PointerCast<void*>(&OnSwapBuffers));
    if(!installed) PublishStatus(2); return installed;
}
void Shutdown() {
    AcquireSRWLockExclusive(&stateLock); stopped=true;
    if(hook.Restore()) installed=false;
    PublishStatus(2); controls.Close(); statuses.Close(); controlOpen=statusOpen=false; lastControl={}; nextControlOpen=0;
    ReleaseSRWLockExclusive(&stateLock);
}
}

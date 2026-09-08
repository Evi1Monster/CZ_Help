#include "render_hook.h"
#include "render_control.h"
#include "iat_hook.h"
#include <GL/gl.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
void Require(bool value,const char* message) {
    if(!value) { std::fprintf(stderr,"FAIL: %s (Win32=%lu, GL=%u)\n",message,GetLastError(),glGetError()); std::exit(1); }
}
void FocusTestWindow(HWND window) {
    // A CTest process launched from the editor may not own the foreground queue.
    // Join it only while activating our own fixture; never synthesize user input.
    const DWORD current=GetCurrentThreadId();
    const HWND initialForeground=GetForegroundWindow();
    const DWORD foreground=GetWindowThreadProcessId(initialForeground,nullptr);
    const bool attached=foreground && foreground!=current && AttachThreadInput(current,foreground,TRUE);
    SetForegroundWindow(window); SetActiveWindow(window);
    if(attached) AttachThreadInput(current,foreground,FALSE);
    const ULONGLONG deadline=GetTickCount64()+1000;
    while(GetForegroundWindow()!=window && GetTickCount64()<deadline) {
        MSG message{};
        while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        Sleep(10);
    }
    if(GetForegroundWindow()!=window) std::fprintf(stderr,"Focus diagnostic: initial=%p final=%p currentThread=%lu foregroundThread=%lu attached=%d\n",initialForeground,GetForegroundWindow(),current,foreground,attached);
    Require(GetForegroundWindow()==window,"foreground test window");
}
struct Window {
    HWND window{}; HDC dc{}; HGLRC context{};
    Window() {
        WNDCLASSW wc{}; wc.style=CS_OWNDC; wc.lpfnWndProc=DefWindowProcW; wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=L"CZHelpRealWglTests";
        Require(RegisterClassW(&wc)!=0,"register WGL test window");
        window=CreateWindowW(wc.lpszClassName,L"CZ Help renderer verification",WS_POPUP|WS_VISIBLE,32,32,320,240,nullptr,nullptr,wc.hInstance,nullptr);
        Require(window!=nullptr,"create WGL test window"); dc=GetDC(window);
        PIXELFORMATDESCRIPTOR pfd{}; pfd.nSize=sizeof(pfd); pfd.nVersion=1;
        pfd.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER; pfd.iPixelType=PFD_TYPE_RGBA; pfd.cColorBits=24; pfd.cDepthBits=24;
        int format=ChoosePixelFormat(dc,&pfd); Require(format && SetPixelFormat(dc,format,&pfd),"set WGL pixel format");
        context=wglCreateContext(dc); Require(context && wglMakeCurrent(dc,context),"create real WGL context");
        FocusTestWindow(window);
        std::printf("WGL: %s / %s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
    }
    ~Window() { cz::render::Shutdown(); wglMakeCurrent(nullptr,nullptr); if(context) wglDeleteContext(context); if(dc) ReleaseDC(window,dc); if(window) DestroyWindow(window); }
};
cz::Snapshot Snapshot() {
    cz::Snapshot s{}; s.bytes=sizeof(s); s.processId=GetCurrentProcessId(); s.timestampMs=GetTickCount64();
    s.status=cz::SessionStatus::Active; s.localIndex=1; s.localTeam=1; s.playerCount=1;
    s.players[0]={2,2,cz::Alive|cz::Bot,100,100,{100,0,0},{-5,-10,-20},{5,10,20}};
    return s;
}
std::vector<unsigned char> Pixels(int width=320,int height=240) {
    std::vector<unsigned char> pixels(static_cast<size_t>(width)*height*3); glPixelStorei(GL_PACK_ALIGNMENT,1); glReadBuffer(GL_BACK);
    glReadPixels(0,0,width,height,GL_RGB,GL_UNSIGNED_BYTE,pixels.data()); return pixels;
}
bool MatchesColor(const unsigned char* p,int kind) {
    return kind==0 ? (p[0]>150 && p[1]>90 && p[2]<110) : kind==1 ? (p[1]>150 && p[0]<60) :
        kind==2 ? (p[0]>150 && p[1]<70) : (p[2]>150 && p[0]<60 && p[1]<60);
}
int ColorCount(const std::vector<unsigned char>& pixels,int x0,int y0,int x1,int y1,int kind,int width=320,int height=240) {
    int count=0;
    for(int y=y0;y<y1;++y) for(int x=x0;x<x1;++x) {
        auto p=&pixels[(static_cast<size_t>(height-1-y)*width+x)*3];
        if(MatchesColor(p,kind)) ++count;
    }
    return count;
}
void Clear() { glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE); glDisable(GL_SCISSOR_TEST); glClearColor(0,0,0,0); glClear(GL_COLOR_BUFFER_BIT); }

struct PixelBounds { int left,top,right,bottom,count; };
PixelBounds Bounds(const std::vector<unsigned char>& pixels,int kind,int width,int height) {
    PixelBounds result{width,height,-1,-1,0};
    for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
        if(!MatchesColor(&pixels[(static_cast<size_t>(height-1-y)*width+x)*3],kind)) continue;
        result.left=std::min(result.left,x); result.right=std::max(result.right,x);
        result.top=std::min(result.top,y); result.bottom=std::max(result.bottom,y); ++result.count;
    }
    return result;
}
PixelBounds ReferenceBox(const cz::Player& player,bool widescreen) {
    // Independent GPU projection: a 90-degree 4:3 camera has vertical tangent 3/4.
    // Preserve that vertical FOV in widescreen mode; legacy mode preserves horizontal FOV.
    const double horizontal=widescreen?4.0/3.0:1.0,vertical=widescreen?0.75:180.0/320.0;
    GLint matrixMode{}; glGetIntegerv(GL_MATRIX_MODE,&matrixMode); glPushAttrib(GL_ALL_ATTRIB_BITS);
    Clear(); glDrawBuffer(GL_BACK); glViewport(0,0,320,180); glDisable(GL_DEPTH_TEST); glDisable(GL_TEXTURE_2D);
    glDisable(GL_POINT_SMOOTH); glPointSize(1); glColor3ub(0,0,255);
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); glFrustum(-horizontal,horizontal,-vertical,vertical,1,1000);
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity(); glBegin(GL_POINTS);
    for(int i=0;i<8;++i) {
        const float x=player.origin.x+(i&1?player.maxs.x:player.mins.x);
        const float y=player.origin.y+(i&2?player.maxs.y:player.mins.y);
        const float z=player.origin.z+(i&4?player.maxs.z:player.mins.z);
        glVertex3f(-y,z,-x); // GoldSrc forward/right/up -> OpenGL camera coordinates.
    }
    glEnd(); const auto result=Bounds(Pixels(320,180),3,320,180);
    glPopMatrix(); glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(matrixMode); glPopAttrib();
    Require(result.count>=4,"independent OpenGL projection must rasterize reference corners"); return result;
}
void WidescreenProjection(Window& window,cz::RenderControlMailbox& writer,cz::RenderControl& control) {
    Require(SetWindowPos(window.window,nullptr,0,0,320,180,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE,"resize real drawable to 16:9");
    RECT client{}; Require(GetClientRect(window.window,&client) && client.right==320 && client.bottom==180,"16:9 drawable dimensions");
    PixelBounds rendered[2]{};
    for(int mode=0;mode<2;++mode) {
        auto snapshot=Snapshot(); snapshot.players[0].origin.y=-40;
        const auto expected=ReferenceBox(snapshot.players[0],mode!=0);
        control.flags=cz::RenderEnabled|cz::RenderBoxes|cz::RenderHealth; control.timestampMs=GetTickCount64();
        Require(writer.Publish(control),"publish widescreen test control");
        snapshot.timestampMs=GetTickCount64(); cz::render::Update(snapshot,mode!=0); Clear(); cz::render::BeforePresent(window.dc);
        const auto pixels=Pixels(320,180); const auto box=Bounds(pixels,0,320,180),hp=Bounds(pixels,1,320,180);
        std::printf("FOV mode %d: reference=(%d,%d)-(%d,%d), box=(%d,%d)-(%d,%d), HP=(%d,%d)-(%d,%d), HP pixels=%d\n",
            mode,expected.left,expected.top,expected.right,expected.bottom,box.left,box.top,box.right,box.bottom,
            hp.left,hp.top,hp.right,hp.bottom,hp.count);
        Require(box.count>100 && std::abs(box.left-expected.left)<=2 && std::abs(box.top-expected.top)<=2 &&
            std::abs(box.right-expected.right)<=2 && std::abs(box.bottom-expected.bottom)<=2,
            "off-centre 16:9 box must match independent OpenGL projection in both FOV modes");
        // The leading '1' has a one-cell (2 px) ink bearing inside the label origin.
        Require(hp.count>30 && std::abs(hp.left-(expected.left+2))<=2 && std::abs(hp.top-(expected.top-18))<=2 && hp.bottom<expected.top,
            "green HP digits must track the independently projected off-centre box");
        rendered[mode]=box;
    }
    Require(rendered[0].left-rendered[1].left>=10 && rendered[0].right-rendered[1].right>=15,
        "enabling widescreen FOV must move off-centre markers inward");
    Require(SetWindowPos(window.window,nullptr,0,0,320,240,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE,"restore 4:3 test drawable");
    cz::render::Update(Snapshot());
    std::puts("PASS: real 16:9 off-centre box and HP pixels match independent OpenGL projection with widescreen FOV on/off");
}

template<class T> T GlProc(const char* name) {
    const auto proc=wglGetProcAddress(name); const auto address=cz::render::PointerCast<std::uintptr_t>(proc);
    return address<=3 || address==UINTPTR_MAX?nullptr:cz::render::PointerCast<T>(proc);
}
void StateRestoration(Window& window,cz::RenderControlMailbox& writer,cz::RenderControl& control) {
    auto createShader=GlProc<GLuint(APIENTRY*)(GLenum)>("glCreateShader");
    auto shaderSource=GlProc<void(APIENTRY*)(GLuint,GLsizei,const char* const*,const GLint*)>("glShaderSource");
    auto compileShader=GlProc<void(APIENTRY*)(GLuint)>("glCompileShader");
    auto getShader=GlProc<void(APIENTRY*)(GLuint,GLenum,GLint*)>("glGetShaderiv");
    auto createProgram=GlProc<GLuint(APIENTRY*)()>("glCreateProgram");
    auto attachShader=GlProc<void(APIENTRY*)(GLuint,GLuint)>("glAttachShader");
    auto linkProgram=GlProc<void(APIENTRY*)(GLuint)>("glLinkProgram");
    auto getProgram=GlProc<void(APIENTRY*)(GLuint,GLenum,GLint*)>("glGetProgramiv");
    auto useProgram=GlProc<void(APIENTRY*)(GLuint)>("glUseProgram");
    auto deleteShader=GlProc<void(APIENTRY*)(GLuint)>("glDeleteShader");
    auto deleteProgram=GlProc<void(APIENTRY*)(GLuint)>("glDeleteProgram");
    auto activeTexture=GlProc<void(APIENTRY*)(GLenum)>("glActiveTexture");
    auto genFramebuffers=GlProc<void(APIENTRY*)(GLsizei,GLuint*)>("glGenFramebuffers");
    auto bindFramebuffer=GlProc<void(APIENTRY*)(GLenum,GLuint)>("glBindFramebuffer");
    auto framebufferTexture=GlProc<void(APIENTRY*)(GLenum,GLenum,GLenum,GLuint,GLint)>("glFramebufferTexture2D");
    auto checkFramebuffer=GlProc<GLenum(APIENTRY*)(GLenum)>("glCheckFramebufferStatus");
    auto deleteFramebuffers=GlProc<void(APIENTRY*)(GLsizei,const GLuint*)>("glDeleteFramebuffers");
    auto genPipelines=GlProc<void(APIENTRY*)(GLsizei,GLuint*)>("glGenProgramPipelines");
    auto bindPipeline=GlProc<void(APIENTRY*)(GLuint)>("glBindProgramPipeline");
    auto deletePipelines=GlProc<void(APIENTRY*)(GLsizei,const GLuint*)>("glDeleteProgramPipelines");
    auto programParameteri=GlProc<void(APIENTRY*)(GLuint,GLenum,GLint)>("glProgramParameteri");
    auto useProgramStages=GlProc<void(APIENTRY*)(GLuint,GLbitfield,GLuint)>("glUseProgramStages");
    Require(createShader && shaderSource && compileShader && getShader && createProgram && attachShader && linkProgram && getProgram && useProgram && deleteShader && deleteProgram && activeTexture && genFramebuffers && bindFramebuffer && framebufferTexture && checkFramebuffer && deleteFramebuffers,"OpenGL shader/FBO test entry points available");
    const char* sources[]={"#version 120\nvoid main(){gl_Position=ftransform();}","#version 120\nvoid main(){gl_FragColor=vec4(1,0,1,1);}"};
    GLuint program=createProgram();
    for(int i=0;i<2;++i) {
        GLuint shader=createShader(i==0?0x8B31:0x8B30); shaderSource(shader,1,&sources[i],nullptr); compileShader(shader);
        GLint ok{}; getShader(shader,0x8B81,&ok); Require(ok==1,"compile real GLSL test shader"); attachShader(program,shader); deleteShader(shader);
    }
    const bool pipelinesAvailable=genPipelines && bindPipeline && deletePipelines && programParameteri && useProgramStages;
    if(pipelinesAvailable) programParameteri(program,0x8258,GL_TRUE);
    linkProgram(program); GLint ok{}; getProgram(program,0x8B82,&ok); Require(ok==1,"link real GLSL test program");
    Clear(); GLuint framebuffer{},texture{}; glGenTextures(1,&texture); glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,16,16,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
    genFramebuffers(1,&framebuffer); bindFramebuffer(0x8D40,framebuffer); framebufferTexture(0x8D40,0x8CE0,GL_TEXTURE_2D,texture,0);
    glDrawBuffer(0x8CE0); glReadBuffer(0x8CE0); Require(checkFramebuffer(0x8D40)==0x8CD5,"complete real framebuffer");
    GLuint pipeline{}; if(pipelinesAvailable) { genPipelines(1,&pipeline); bindPipeline(pipeline); useProgramStages(pipeline,3,program); }
    useProgram(program); activeTexture(0x84C1); glEnable(GL_TEXTURE_2D);
    glViewport(3,5,29,31); glEnable(GL_SCISSOR_TEST); glScissor(0,0,1,1); glEnable(GL_DEPTH_TEST);
    glColorMask(GL_FALSE,GL_TRUE,GL_FALSE,GL_FALSE); glPolygonMode(GL_FRONT_AND_BACK,GL_LINE);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glTranslatef(2,3,4);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity(); glScalef(2,3,4); glMatrixMode(GL_TEXTURE);
    GLfloat projection[16]{},modelview[16]{}; glGetFloatv(GL_PROJECTION_MATRIX,projection); glGetFloatv(GL_MODELVIEW_MATRIX,modelview);
    control.timestampMs=GetTickCount64(); writer.Publish(control); cz::render::Update(Snapshot());
    cz::render::BeforePresent(window.dc);
    GLint integer{},viewport[4]{}; glGetIntegerv(0x8B8D,&integer); Require(static_cast<GLuint>(integer)==program,"restore current GLSL program");
    glGetIntegerv(0x8CA6,&integer); Require(static_cast<GLuint>(integer)==framebuffer,"restore draw framebuffer");
    glGetIntegerv(0x8CAA,&integer); Require(static_cast<GLuint>(integer)==framebuffer,"preserve read framebuffer");
    glGetIntegerv(0x84E0,&integer); Require(integer==0x84C1,"restore active texture");
    if(pipeline) { glGetIntegerv(0x825A,&integer); Require(static_cast<GLuint>(integer)==pipeline,"restore shader pipeline binding"); }
    glGetIntegerv(GL_MATRIX_MODE,&integer); Require(integer==GL_TEXTURE,"restore matrix mode");
    glGetIntegerv(GL_VIEWPORT,viewport); Require(viewport[0]==3 && viewport[1]==5 && viewport[2]==29 && viewport[3]==31,"restore viewport");
    GLfloat matrix[16]{}; glGetFloatv(GL_PROJECTION_MATRIX,matrix); Require(std::memcmp(matrix,projection,sizeof(matrix))==0,"restore projection matrix");
    glGetFloatv(GL_MODELVIEW_MATRIX,matrix); Require(std::memcmp(matrix,modelview,sizeof(matrix))==0,"restore modelview matrix");
    GLboolean mask[4]{}; glGetBooleanv(GL_COLOR_WRITEMASK,mask); Require(!mask[0] && mask[1] && !mask[2] && !mask[3],"restore color mask");
    Require(glIsEnabled(GL_TEXTURE_2D) && glIsEnabled(GL_DEPTH_TEST) && glIsEnabled(GL_SCISSOR_TEST),"restore texture/depth/scissor enables");
    glGetIntegerv(GL_DRAW_BUFFER,&integer); Require(integer==0x8CE0,"restore framebuffer draw buffer");
    Require(glGetError()==GL_NO_ERROR,"state preservation must not introduce GL errors");
    bindFramebuffer(0x8D40,0); auto pixels=Pixels();
    Require(ColorCount(pixels,135,80,185,160,0)>100,"draw into default backbuffer despite active shader/FBO/masks/scissor");
    useProgram(0); deleteProgram(program); deleteFramebuffers(1,&framebuffer); glDeleteTextures(1,&texture);
    if(pipeline) { bindPipeline(0); deletePipelines(1,&pipeline); }
    glDisable(GL_TEXTURE_2D); activeTexture(0x84C0); glDisable(GL_DEPTH_TEST); glDisable(GL_SCISSOR_TEST);
    glPolygonMode(GL_FRONT_AND_BACK,GL_FILL); glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    std::puts("PASS: GLSL, framebuffer, active texture, viewport, matrix and fixed-function state restoration");
}
using SwapProc=BOOL(WINAPI*)(HDC);
SwapProc originalSwap{}; int swaps{},laterSwaps{};
BOOL WINAPI CountSwap(HDC dc) { ++swaps; return originalSwap(dc); }
BOOL WINAPI LaterSwap(HDC dc) { ++laterSwaps; return CountSwap(dc); }
// Each present enters SDL's swap routine anew. Keep separate imported calls here:
// MSVC otherwise hoists a single IAT read across the deliberate table mutations.
__declspec(noinline) BOOL ImportedSwap(HDC dc) { return SwapBuffers(dc); }
void IatRestoration(Window& window) {
    originalSwap=cz::render::PointerCast<SwapProc>(GetProcAddress(GetModuleHandleW(L"gdi32.dll"),"SwapBuffers"));
    cz::render::IatHook iat;
    Require(!iat.Install(GetModuleHandleW(nullptr),"GDI32.dll","SwapBuffers",cz::render::PointerCast<void*>(&CountSwap),cz::render::PointerCast<void*>(originalSwap)),"reject mismatched IAT target");
    Require(iat.Install(GetModuleHandleW(nullptr),"GDI32.dll","SwapBuffers",cz::render::PointerCast<void*>(originalSwap),cz::render::PointerCast<void*>(&CountSwap)),"install hook into real executable import table");
    Require(ImportedSwap(window.dc)!=FALSE && swaps==1,"real imported SwapBuffers reaches installed callback and original");
    Require(iat.Restore(),"restore real executable import slot");
    Require(ImportedSwap(window.dc)!=FALSE && swaps==1,"restored import bypasses callback");
    Require(iat.Install(GetModuleHandleW(nullptr),"GDI32.dll","SwapBuffers",cz::render::PointerCast<void*>(originalSwap),cz::render::PointerCast<void*>(&CountSwap)),"reinstall first import hook");
    cz::render::IatHook later;
    Require(later.Install(GetModuleHandleW(nullptr),"GDI32.dll","SwapBuffers",cz::render::PointerCast<void*>(&CountSwap),cz::render::PointerCast<void*>(&LaterSwap)),"install subsequent import hook");
    Require(iat.Restore(),"shutdown must leave a later hook intact");
    Require(ImportedSwap(window.dc)!=FALSE && swaps==2 && laterSwaps==1,"later hook must still forward through original chain");
    Require(later.Restore() && iat.Restore(),"restore both import hooks after ownership returns");
    Require(ImportedSwap(window.dc)!=FALSE && swaps==2 && laterSwaps==1,"fully restored import no longer invokes either callback");
    std::puts("PASS: validated IAT installation, callback/original invocation and restoration");
}
}
int main() {
    Window window;
    cz::RenderControlMailbox writer; Require(writer.Open(GetCurrentProcessId(),L"Control",true),"open control writer");
    cz::RenderControl control{}; control.processId=GetCurrentProcessId(); control.timestampMs=GetTickCount64();
    control.flags=cz::RenderEnabled|cz::RenderBoxes|cz::RenderHealth; Require(writer.Publish(control),"publish enabled control");
    auto snapshot=Snapshot(); cz::render::Update(snapshot); Clear();
    cz::render::BeforePresent(window.dc);
    auto pixels=Pixels();
    Require(ColorCount(pixels,135,80,185,160,0)>100,"enabled control must draw projected orange box into real backbuffer");
    Require(ColorCount(pixels,135,60,230,85,1)>30,"100 HP must draw green bitmap digits above box");
    Require(ColorCount(pixels,148,95,170,145,0)==0 && ColorCount(pixels,130,90,141,150,1)==0,"box interior stays transparent and no green health bar is drawn");
    WidescreenProjection(window,writer,control);
    StateRestoration(window,writer,control);
    control.flags=0; control.timestampMs=GetTickCount64(); writer.Publish(control); Clear(); cz::render::BeforePresent(window.dc);
    pixels=Pixels(); Require(ColorCount(pixels,0,0,320,240,0)==0 && ColorCount(pixels,0,0,320,240,1)==0,"disabled control must leave backbuffer clear");
    cz::RenderStatusMailbox statusReader; Require(statusReader.Open(GetCurrentProcessId(),L"Status",false),"renderer status channel exists when disabled");
    cz::RenderStatus status{}; Require(statusReader.Read(status) && cz::ValidRenderStatus(status,GetCurrentProcessId(),GetTickCount64()) && status.state==1 && status.width==320 && status.height==240,"disabled renderer must still report live present dimensions");
    control.flags=7; control.timestampMs=GetTickCount64()-501; writer.Publish(control); Clear(); cz::render::BeforePresent(window.dc);
    pixels=Pixels(); Require(ColorCount(pixels,0,0,320,240,0)==0,"stale control must leave backbuffer clear");
    control.timestampMs=GetTickCount64(); writer.Publish(control); snapshot=Snapshot(); snapshot.players[0].health=1; cz::render::Update(snapshot);
    Clear(); cz::render::BeforePresent(window.dc); pixels=Pixels();
    Require(ColorCount(pixels,135,60,230,85,2)>30,"1 HP must draw red bitmap digits");
    Require(glGetError()==GL_NO_ERROR,"renderer must not introduce OpenGL errors");
    HWND other=CreateWindowW(L"CZHelpRealWglTests",L"CZ Help foreground gate test",WS_POPUP|WS_VISIBLE,400,32,160,100,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    Require(other!=nullptr,"create separate foreground window"); FocusTestWindow(other);
    control.timestampMs=GetTickCount64(); writer.Publish(control); cz::render::Update(Snapshot()); Clear(); cz::render::BeforePresent(window.dc);
    pixels=Pixels(); Require(ColorCount(pixels,0,0,320,240,0)==0,"background game drawable must not receive overlay");
    DestroyWindow(other); FocusTestWindow(window.window);
    Require(GetForegroundWindow()==window.window,"restore test foreground window");
    snapshot.timestampMs=GetTickCount64()-501; cz::render::Update(snapshot); Clear(); cz::render::BeforePresent(window.dc);
    pixels=Pixels(); Require(ColorCount(pixels,0,0,320,240,0)==0,"stale snapshot must not draw");
    snapshot=Snapshot(); snapshot.status=cz::SessionStatus::NotLocal; cz::render::Update(snapshot); Clear(); cz::render::BeforePresent(window.dc);
    pixels=Pixels(); Require(ColorCount(pixels,0,0,320,240,0)==0,"nonlocal snapshot must not draw");
    IatRestoration(window);
    cz::render::Shutdown(); cz::render::Update(Snapshot()); Clear(); cz::render::BeforePresent(window.dc);
    pixels=Pixels(); Require(ColorCount(pixels,0,0,320,240,0)==0,"shutdown must prevent future callbacks from drawing");
    std::puts("PASS: real WGL box, HP colors, disabled and stale control");
}

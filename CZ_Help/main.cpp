#include "core.h"
#include "channel.h"
#include "render_control.h"
#include "bhop_control.h"
#include "settings.h"
#include "session.h"
#include "game_detection.h"
#include <windows.h>
#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>

namespace {
constexpr int kMaster=101,kBoxes=102,kHealth=103,kReload=104,kEdit=105,kBhop=106,kLaunch=107;
constexpr int kModeFirst=201,kModeCount=2;
const cz::JumpMode modeValues[]={cz::JumpMode::AutoJump,cz::JumpMode::BhopJump};
const wchar_t* modeNames[]={L"自动跳跃",L"BHopJump"};
const wchar_t* modeHelp[]={
    L"按住空格连续跳跃；移动和视角由你控制。",
    L"按住空格连跳；空中手动左右转视角，自动配合对应的侧移。"};
int ModeIndex(cz::JumpMode mode) { return mode==cz::JumpMode::BhopJump?1:0; }
std::uint32_t movementGeneration{};
bool movementWasActive{};
HWND modeDescription{};
HWND panel{},overlay{},game{},statusLabel{},checks[3]{},bhopCheck{};
HWND launchButton{};
cz::SessionManager managedSession;
std::wstring selectedGamePath;
cz::GamePathChoice gamePathChoice;
bool launchRequested{},forceWindowed{};
ULONGLONG gameStartingUntil{};
std::wstring launchError;
HFONT font{};
cz::Settings settings;
cz::Switches switches;
cz::KeyToggle bhopSwitch;
cz::BhopControlMailbox bhopControl;
cz::BhopStatusMailbox bhopStatus;
cz::BhopStatus lastBhopStatus{};
bool bhopControlConnected{},bhopStatusConnected{};
cz::ReadChannel channel;
cz::RenderControlMailbox renderControl;
cz::RenderStatusMailbox renderStatus;
cz::RenderStatus lastBackend{};
cz::RenderControl lastPublishedControl{};
bool controlConnected{},statusConnected{};
cz::Snapshot snapshot{};
DWORD gamePid{};
ULONGLONG lastFind{};
bool connected=false,valid=false;
std::wstring lastStatus;
HDC canvas{};
HBITMAP bitmap{},oldBitmap{};
int canvasWidth{},canvasHeight{};

std::wstring ExeDirectory() {
    wchar_t path[32768]{}; const DWORD n=GetModuleFileNameW(nullptr,path,32768);
    if (!n || n>=32768) return L".";
    std::wstring result(path,n); return result.substr(0,result.find_last_of(L"\\/"));
}
std::wstring KeyName(int key) {
    if (!key) return L"快捷键禁用";
    wchar_t name[80]{};
    if (GetKeyNameTextW(static_cast<LONG>(MapVirtualKeyW(key,MAPVK_VK_TO_VSC)<<16),name,80)) return name;
    return std::to_wstring(key);
}
void SyncPanel(bool save) {
    if (save) {
        settings.options=switches.options;
        if (!settings.Save()) settings.warning=L"设置保存失败，请检查程序目录写入权限。";
    }
    const bool values[]={switches.enabled,switches.options.boxes,switches.options.health};
    const wchar_t* labels[]={L"启用透视",L"显示方框",L"显示血量数字"};
    for(int i=0;i<3;++i) {
        const auto text=std::wstring(labels[i])+L"  ["+KeyName(settings.keys[i])+L"]";
        SetWindowTextW(checks[i],text.c_str());
        SendMessageW(checks[i],BM_SETCHECK,values[i]?BST_CHECKED:BST_UNCHECKED,0);
    }
    const auto bhopText=L"启用跳跃辅助（按住空格）  ["+KeyName(settings.bhopKey)+L"]";
    SetWindowTextW(bhopCheck,bhopText.c_str());
    SendMessageW(bhopCheck,BM_SETCHECK,bhopSwitch.enabled?BST_CHECKED:BST_UNCHECKED,0);
    const int selected=ModeIndex(settings.jumpMode);
    CheckRadioButton(panel,kModeFirst,kModeFirst+kModeCount-1,kModeFirst+selected);
    SetWindowTextW(modeDescription,modeHelp[selected]);
}
BOOL CALLBACK FindGame(HWND window,LPARAM result) {
    if (!IsWindowVisible(window) || GetWindow(window,GW_OWNER)) return TRUE;
    DWORD pid{}; GetWindowThreadProcessId(window,&pid);
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if (!process) return TRUE;
    wchar_t path[32768]{}; DWORD size=32768;
    const bool read=QueryFullProcessImageNameW(process,0,path,&size)!=FALSE;
    CloseHandle(process);
    if (!read) return TRUE;
    if (_wcsicmp(path,(selectedGamePath+L"\\hl.exe").c_str())!=0) return TRUE;
    const wchar_t* file=wcsrchr(path,L'\\'); file=file?file+1:path;
    if (_wcsicmp(file,L"hl.exe")!=0) return TRUE;
    wchar_t className[128]{}; GetClassNameW(window,className,128);
    if (_wcsicmp(className,L"Valve001")!=0 && _wcsicmp(className,L"SDL_app")!=0) return TRUE;
    *reinterpret_cast<HWND*>(result)=window; return FALSE;
}
void FreeCanvas() {
    if (canvas && oldBitmap) SelectObject(canvas,oldBitmap);
    if (bitmap) DeleteObject(bitmap);
    if (canvas) DeleteDC(canvas);
    canvas=nullptr; bitmap=oldBitmap=nullptr; canvasWidth=canvasHeight=0;
}
void Solid(HDC dc,RECT rect,COLORREF color) {
    SetDCBrushColor(dc,color); FillRect(dc,&rect,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}
void Label(HDC dc,int x,int y,const std::wstring& text,COLORREF color) {
    SetTextColor(dc,RGB(1,1,1)); TextOutW(dc,x+1,y+1,text.c_str(),static_cast<int>(text.size()));
    SetTextColor(dc,color); TextOutW(dc,x,y,text.c_str(),static_cast<int>(text.size()));
}
void Draw(HDC dc,int width,int height) {
    RECT bounds{0,0,width,height}; Solid(dc,bounds,RGB(0,0,0));
    if (!valid || !switches.enabled) return;
    SelectObject(dc,font); SetBkMode(dc,TRANSPARENT);
    const bool wide=cz::ValidRenderStatus(lastBackend,gamePid,GetTickCount64()) &&
        (lastBackend.projectionFlags&cz::kProjectionWidescreen)!=0;
    const auto camera=cz::ResolveProjectionCamera(snapshot.camera,{width,height},wide);
    Label(dc,12,12,L"CZ Help | "+KeyName(settings.keys[0])+L" 开关",RGB(135,235,200));
    for(std::uint32_t i=0;i<snapshot.playerCount;++i) {
        const auto& player=snapshot.players[i]; cz::Rect box{};
        if (!cz::IsTarget(snapshot,player) || !cz::ProjectBox(camera,player,{width,height},box)) continue;
        int l=static_cast<int>(std::floor(box.left)),t=static_cast<int>(std::floor(box.top));
        int r=static_cast<int>(std::ceil(box.right)),b=static_cast<int>(std::ceil(box.bottom));
        if (switches.options.boxes) {
            SetDCBrushColor(dc,RGB(255,183,74)); RECT frame{l,t,r,b};
            FrameRect(dc,&frame,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        }
        if (switches.options.health) {
            const float ratio=std::clamp(player.health/player.maxHealth,0.0f,1.0f);
            COLORREF color=RGB(static_cast<int>(255*(1-ratio)),static_cast<int>(220*ratio),72);
            const auto hp=std::to_wstring(static_cast<int>(std::ceil(std::min(player.health,99999.0f))))+L" HP";
            SIZE size{}; GetTextExtentPoint32W(dc,hp.c_str(),static_cast<int>(hp.size()),&size);
            const int textX=std::clamp(l,0,std::max(0,width-static_cast<int>(size.cx)));
            const int textY=t>=20?t-20:std::min(b+2,height-20);
            Label(dc,textX,std::max(0,textY),hp,color);
        }
    }
}
LRESULT CALLBACK OverlayProc(HWND window,UINT message,WPARAM w,LPARAM l) {
    if (message==WM_NCHITTEST) return HTTRANSPARENT;
    if (message==WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (message==WM_ERASEBKGND) return 1;
    if (message==WM_PAINT) {
        PAINTSTRUCT paint{}; HDC dc=BeginPaint(window,&paint); RECT rect{}; GetClientRect(window,&rect);
        if (rect.right>0 && rect.bottom>0) {
            if (!canvas || canvasWidth!=rect.right || canvasHeight!=rect.bottom) {
                FreeCanvas(); canvas=CreateCompatibleDC(dc); bitmap=CreateCompatibleBitmap(dc,rect.right,rect.bottom);
                if (canvas && bitmap) { oldBitmap=static_cast<HBITMAP>(SelectObject(canvas,bitmap)); canvasWidth=rect.right; canvasHeight=rect.bottom; }
                else { FreeCanvas(); }
            }
            if (canvas) { Draw(canvas,rect.right,rect.bottom); BitBlt(dc,0,0,rect.right,rect.bottom,canvas,0,0,SRCCOPY); }
        }
        EndPaint(window,&paint); return 0;
    }
    return DefWindowProcW(window,message,w,l);
}
void LaunchGame() {
    launchRequested=false; launchError.clear();
    const auto exe=selectedGamePath+L"\\hl.exe";
    std::wstring command=cz::QuoteWindowsArgument(exe)+L" -game czero -insecure -console -gl +sv_lan 1";
    if (forceWindowed) command+=L" -windowed -w 1280 -h 960";
    STARTUPINFOW startup{}; startup.cb=sizeof(startup); PROCESS_INFORMATION process{};
    if (!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,selectedGamePath.c_str(),&startup,&process)) {
        launchError=L"游戏启动失败，Windows 错误码："+std::to_wstring(GetLastError()); return;
    }
    CloseHandle(process.hThread); CloseHandle(process.hProcess); gameStartingUntil=GetTickCount64()+10000;
}
void Tick() {
    const ULONGLONG now=GetTickCount64();
    managedSession.Poll();
    if (now-lastFind>=1000) {
        lastFind=now; HWND found{}; EnumWindows(FindGame,reinterpret_cast<LPARAM>(&found));
        DWORD pid{}; if(found) GetWindowThreadProcessId(found,&pid);
        if (found!=game || pid!=gamePid) {
            channel.Close(); renderControl.Close(); renderStatus.Close();
            bhopControl.Close(); bhopStatus.Close(); bhopControlConnected=bhopStatusConnected=false;
            lastBhopStatus={};
            connected=controlConnected=statusConnected=false; game=found; gamePid=pid; snapshot={};
            lastBackend={}; lastPublishedControl={};
        }
        if (game && !connected) connected=channel.Open(gamePid);
        if (game && !controlConnected) controlConnected=renderControl.Open(gamePid,L"Control",true);
        if (game && !statusConnected) statusConnected=renderStatus.Open(gamePid,L"Status",false);
        if (game && !bhopControlConnected) bhopControlConnected=bhopControl.Open(gamePid,L"BhopControl",true);
        if (game && !bhopStatusConnected) bhopStatusConnected=bhopStatus.Open(gamePid,L"BhopStatus",false);
    }
    const bool foreground=game && GetForegroundWindow()==game && !IsIconic(game);
    EnableWindow(launchButton,managedSession.Ready() && !game && now>=gameStartingUntil);
    if (launchRequested && managedSession.Ready() && !game && now>=gameStartingUntil) LaunchGame();
    if (game) launchRequested=false;
    std::array<bool,3> down{};
    for (int i=0;i<3;++i) down[i]=(GetAsyncKeyState(settings.keys[i])&0x8000)!=0;
    if (switches.Update(foreground,down)) SyncPanel(true);
    if (bhopSwitch.Update(foreground,settings.bhopKey && (GetAsyncKeyState(settings.bhopKey)&0x8000)!=0)) { ++movementGeneration; SyncPanel(false); }
    cz::Snapshot next{};
    if (connected && channel.Read(next)) snapshot=next;
    const bool fresh=connected && cz::ValidSnapshot(snapshot,gamePid,GetTickCount64());
    valid=managedSession.Ready() && fresh && snapshot.status==cz::SessionStatus::Active;
    cz::BhopControl bhop{}; bhop.processId=gamePid; bhop.timestampMs=GetTickCount64();
    bhop.mode=settings.jumpMode;
    if (foreground && valid && bhopSwitch.enabled) {
        bhop.flags|=cz::BhopEnabled;
        if (GetAsyncKeyState(VK_SPACE)&0x8000) bhop.flags|=cz::BhopSpaceHeld;
    }
    const bool movementActive=bhop.flags==3;
    if(movementActive!=movementWasActive) { ++movementGeneration; movementWasActive=movementActive; }
    bhop.generation=movementGeneration;
    if (bhopControlConnected) bhopControl.Publish(bhop);
    cz::BhopStatus bhopBackend{};
    if (bhopStatusConnected && bhopStatus.Read(bhopBackend)) lastBhopStatus=bhopBackend;
    cz::RenderControl control{}; control.processId=gamePid; control.timestampMs=now;
    if(foreground && valid && switches.enabled) control.flags|=cz::RenderEnabled;
    if(switches.options.boxes) control.flags|=cz::RenderBoxes;
    if(switches.options.health) control.flags|=cz::RenderHealth;
    if(controlConnected && renderControl.Publish(control)) lastPublishedControl=control;
    cz::RenderStatus backend{};
    if(statusConnected && renderStatus.Read(backend)) lastBackend=backend;
    const auto checkTime=GetTickCount64();
    const bool backendFresh=cz::ValidRenderStatus(lastBackend,gamePid,checkTime);
    const bool nativeActive=cz::ValidRenderControl(lastPublishedControl,gamePid,checkTime) &&
        backendFresh && lastBackend.state==1;
    std::wstring text;
    if (gamePathChoice.source==cz::GamePathSource::Ambiguous) text=gamePathChoice.warning;
    else if (!managedSession.Ready()) text=managedSession.Message();
    else if (!game) text=now<gameStartingUntil?L"准备就绪 · 正在启动游戏…":L"准备就绪 · 等待游戏启动";
    else if (!fresh) text=L"准备就绪 · 游戏已启动，等待进入本地 BOT 对局。";
    else if (snapshot.status==cz::SessionStatus::NotLocal) text=L"当前不是单人本地 BOT 对局（需 sv_lan 1）。";
    else if (snapshot.status==cz::SessionStatus::Spectating) text=L"等待本地玩家复活并回到第一人称。";
    else if (!valid) text=L"等待进入队伍及本地对局数据。";
    else text=L"已连接 · "+std::to_wstring(snapshot.playerCount)+L" 个敌方 BOT · 透视"+(switches.enabled?L"已开启":L"已关闭")+
        (nativeActive?L" · 游戏内绘制":backendFresh && lastBackend.state==2?
            L" · 桌面绘制（请使用 OpenGL 并重启游戏）":L" · 桌面绘制（全屏请更新插件）");
    if (fresh && managedSession.Ready()) {
        const bool bhopReady=cz::ValidBhopStatus(lastBhopStatus,gamePid,GetTickCount64()) && (lastBhopStatus.flags&cz::BhopAvailable);
        if (!bhopReady) text+=L"\r\n等待连跳数据；若持续无响应，请退出游戏后重新启动。";
        else if (bhopSwitch.enabled) text+=L"\r\n跳跃辅助已开启 · "+std::wstring(modeNames[ModeIndex(settings.jumpMode)])+L" · 按住空格";
        else text+=L"\r\n跳跃辅助已关闭 · "+KeyName(settings.bhopKey)+L" / 勾选开启";
    }
    if (gamePathChoice.source!=cz::GamePathSource::Ambiguous && !gamePathChoice.warning.empty()) text+=L"\r\n"+gamePathChoice.warning;
    if (!settings.warning.empty()) text+=L"\r\n"+settings.warning;
    if (!launchError.empty()) text+=L"\r\n"+launchError;
    if (text!=lastStatus) { lastStatus=text; SetWindowTextW(statusLabel,text.c_str()); }
    if (foreground && valid && switches.enabled && !nativeActive) {
        RECT rect{}; GetClientRect(game,&rect); POINT start{}; ClientToScreen(game,&start);
        if (rect.right>0 && rect.bottom>0) {
            SetWindowPos(overlay,HWND_TOPMOST,start.x,start.y,rect.right,rect.bottom,SWP_NOACTIVATE|SWP_SHOWWINDOW);
            InvalidateRect(overlay,nullptr,FALSE); return;
        }
    }
    ShowWindow(overlay,SW_HIDE);
}
LRESULT CALLBACK PanelProc(HWND window,UINT message,WPARAM w,LPARAM l) {
    switch(message) {
    case WM_TIMER: Tick(); return 0;
    case WM_COMMAND:
        if(LOWORD(w)>=kModeFirst && LOWORD(w)<kModeFirst+kModeCount && HIWORD(w)==BN_CLICKED) {
            settings.jumpMode=modeValues[LOWORD(w)-kModeFirst];
            ++movementGeneration; SyncPanel(true); return 0;
        }
        switch(LOWORD(w)) {
        case kMaster: switches.enabled=!switches.enabled; SyncPanel(false); break;
        case kBoxes: switches.options.boxes=!switches.options.boxes; SyncPanel(true); break;
        case kHealth: switches.options.health=!switches.options.health; SyncPanel(true); break;
        case kBhop: bhopSwitch.enabled=!bhopSwitch.enabled; ++movementGeneration; SyncPanel(false); break;
        case kReload:
            settings.Load(settings.path); switches.options=settings.options;
            ++movementGeneration;
            if (_wcsicmp(cz::NormalizeGamePath(settings.gamePath).c_str(),selectedGamePath.c_str())!=0)
                settings.warning+=L"\r\n游戏目录已更改，请关闭并重新启动辅助后生效。";
            SyncPanel(false); break;
        case kLaunch: launchRequested=true; break;
        case kEdit: ShellExecuteW(window,L"open",L"notepad.exe",(L"\""+settings.path+L"\"").c_str(),nullptr,SW_SHOW); break;
        } return 0;
    case WM_DESTROY: {
        KillTimer(window,1);
        cz::RenderControl off{}; off.processId=gamePid; off.timestampMs=GetTickCount64();
        if(controlConnected) renderControl.Publish(off);
        cz::BhopControl bhopOff{}; bhopOff.processId=gamePid; bhopOff.timestampMs=GetTickCount64();
        if (bhopControlConnected) bhopControl.Publish(bhopOff);
        PostQuitMessage(0); return 0;
    }
    }
    return DefWindowProcW(window,message,w,l);
}
HWND Control(const wchar_t* type,const wchar_t* text,DWORD style,int x,int y,int width,int height,int id) {
    HWND h=CreateWindowExW(0,type,text,WS_CHILD|WS_VISIBLE|style,x,y,width,height,panel,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
    SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE); return h;
}
int Probe(DWORD pid) {
    cz::ReadChannel reader; cz::Snapshot s{};
    if(!pid || !reader.Open(pid) || !reader.Read(s) || !cz::ValidSnapshot(s,pid,GetTickCount64())) {
        std::cout<<"No fresh plugin snapshot for PID "<<pid<<"\n"; return 2;
    }
    std::cout<<"pid="<<pid<<" status="<<static_cast<unsigned>(s.status)<<" count="<<s.playerCount<<" local="<<s.localIndex<<" team="<<s.localTeam<<"\n";
    cz::RenderStatusMailbox status; cz::RenderStatus backend{};
    if(status.Open(pid,L"Status",false) && status.Read(backend) && cz::ValidRenderStatus(backend,pid,GetTickCount64()))
        std::cout<<"renderer="<<backend.state<<" viewport="<<backend.width<<"x"<<backend.height<<" widescreen_fov="<<((backend.projectionFlags&cz::kProjectionWidescreen)!=0)<<"\n";
    std::cout<<"camera="<<s.camera.origin.x<<","<<s.camera.origin.y<<","<<s.camera.origin.z<<" angles="<<s.camera.angles.x<<","<<s.camera.angles.y<<","<<s.camera.angles.z<<" fov="<<s.camera.horizontalFov<<"\n";
    for(std::uint32_t i=0;i<s.playerCount;++i) {
        const auto& p=s.players[i];
        std::cout<<"player="<<p.index<<" team="<<p.team<<" flags="<<p.flags<<" hp="<<p.health<<"/"<<p.maxHealth<<" pos="<<p.origin.x<<","<<p.origin.y<<","<<p.origin.z<<" target="<<cz::IsTarget(s,p)<<"\n";
    }
    cz::BhopStatusMailbox bhopReader; cz::BhopStatus bhop{};
    if (bhopReader.Open(pid,L"BhopStatus",false) && bhopReader.Read(bhop) && cz::ValidBhopStatus(bhop,pid,GetTickCount64()))
        std::cout<<"bhop_available="<<((bhop.flags&cz::BhopAvailable)!=0)<<" armed="<<((bhop.flags&cz::BhopArmed)!=0)<<
            " rearmed_commands="<<bhop.rearmedCommands<<"\n";
    return s.status==cz::SessionStatus::Active?0:3;
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show) {
    int argc{}; LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(argv && argc==3 && wcscmp(argv[1],L"--probe")==0) {
        const DWORD pid=wcstoul(argv[2],nullptr,10); LocalFree(argv); return Probe(pid);
    }
    bool smoke=argv && argc==2 && wcscmp(argv[1],L"--smoke-test")==0;
    std::wstring gameOverride;
    bool argumentsValid=true;
    if (!smoke) for (int i=1;argv && i<argc;++i) {
        if (wcscmp(argv[i],L"--game-path")==0 && i+1<argc) gameOverride=argv[++i];
        else if (wcscmp(argv[i],L"--launch-game")==0) launchRequested=true;
        else if (wcscmp(argv[i],L"--windowed")==0) forceWindowed=true;
        else argumentsValid=false;
    }
    if(argv) LocalFree(argv);
    if (!argumentsValid) return 2;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto mutexName=smoke?L"Local\\CZHelp.Smoke."+std::to_wstring(GetCurrentProcessId()):std::wstring(L"Local\\CZHelp.UI.v1");
    HANDLE single=CreateMutexW(nullptr,FALSE,mutexName.c_str());
    if (!single || GetLastError()==ERROR_ALREADY_EXISTS) { if(single) CloseHandle(single); return 1; }
    settings.Load(ExeDirectory()+L"\\CZ_Help.ini"); switches.options=settings.options;
    movementGeneration=static_cast<std::uint32_t>(GetTickCount64());
    const auto discovery=(!smoke && gameOverride.empty())?cz::DiscoverRunningGames():cz::GameDiscovery{};
    gamePathChoice=cz::SelectGamePath(gameOverride,settings.gamePath,discovery);
    selectedGamePath=gamePathChoice.path;
    if (!smoke && gamePathChoice.source==cz::GamePathSource::RunningGame) {
        settings.gamePath=selectedGamePath;
        if (!settings.Save()) settings.warning+=L"\r\n已识别游戏目录，但保存失败；本次仍使用识别到的目录。";
    }
    font=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei UI");
    WNDCLASSW wc{}; wc.hInstance=instance; wc.hCursor=LoadCursorW(nullptr,IDC_ARROW); wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
    wc.lpszClassName=L"CZHelpPanel"; wc.lpfnWndProc=PanelProc;
    if (!RegisterClassW(&wc)) return 1;
    wc.lpszClassName=L"CZHelpOverlay"; wc.lpfnWndProc=OverlayProc; wc.hbrBackground=nullptr;
    if (!RegisterClassW(&wc)) return 1;
    panel=CreateWindowExW(0,L"CZHelpPanel",L"CZ Help · 单机 BOT 辅助",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,690,600,nullptr,nullptr,instance,nullptr);
    overlay=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_TOPMOST|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"CZHelpOverlay",L"CZ Help Overlay",WS_POPUP,0,0,1,1,nullptr,nullptr,instance,nullptr);
    if(!panel || !overlay || !SetLayeredWindowAttributes(overlay,RGB(0,0,0),255,LWA_COLORKEY)) return 1;
    Control(L"STATIC",L"本地 BOT 对局 · OpenGL 全屏 / 窗口 · 快捷键仅游戏前台响应",0,22,18,520,25,0);
    for(int i=0;i<3;++i) checks[i]=Control(L"BUTTON",L"",BS_AUTOCHECKBOX|WS_TABSTOP,22,53+i*34,480,28,kMaster+i);
    bhopCheck=Control(L"BUTTON",L"",BS_AUTOCHECKBOX|WS_TABSTOP,22,155,530,28,kBhop);
    Control(L"BUTTON",L"跳跃模式（单选）",BS_GROUPBOX,22,190,640,60,0);
    for(int i=0;i<kModeCount;++i) Control(L"BUTTON",modeNames[i],BS_AUTORADIOBUTTON|WS_TABSTOP|(i==0?WS_GROUP:0),
        36+(i%2)*308,216+(i/2)*29,295,25,kModeFirst+i);
    modeDescription=Control(L"STATIC",L"",WS_GROUP,22,260,640,45,0);
    Control(L"BUTTON",L"编辑设置",BS_PUSHBUTTON|WS_TABSTOP|WS_GROUP,22,315,155,30,kEdit);
    Control(L"BUTTON",L"重新加载设置",BS_PUSHBUTTON|WS_TABSTOP,190,315,145,30,kReload);
    launchButton=Control(L"BUTTON",L"启动本地游戏",BS_PUSHBUTTON|WS_TABSTOP,352,315,155,30,kLaunch);
    EnableWindow(launchButton,FALSE);
    statusLabel=Control(L"STATIC",L"正在自动配置…",0,22,361,640,116,109);
    const auto pathPrefix=gamePathChoice.source==cz::GamePathSource::RunningGame?L"自动识别：":
        gamePathChoice.source==cz::GamePathSource::Argument?L"指定目录：":L"配置目录：";
    const auto pathText=selectedGamePath.empty()?L"游戏目录未确定":std::wstring(pathPrefix)+selectedGamePath;
    Control(L"EDIT",pathText.c_str(),WS_BORDER|WS_TABSTOP|ES_READONLY|ES_AUTOHSCROLL,22,486,640,27,108);
    Control(L"STATIC",L"关闭辅助后自动清理；若游戏仍开着，将在游戏退出后完成。",0,22,528,640,25,0);
    SyncPanel(false);
    ShowWindow(panel,smoke?SW_HIDE:show);
    if (smoke) { UpdateWindow(panel); DestroyWindow(panel); }
    else {
        if(gamePathChoice.source!=cz::GamePathSource::Ambiguous) managedSession.Start(selectedGamePath,ExeDirectory());
        SetTimer(panel,1,16,nullptr);
    }
    MSG msg{}; while(GetMessageW(&msg,nullptr,0,0)>0) {
        if(!IsDialogMessageW(panel,&msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    DestroyWindow(overlay); FreeCanvas(); DeleteObject(font); channel.Close(); CloseHandle(single); return 0;
}

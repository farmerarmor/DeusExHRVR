#include "GamepadBridge.h"
#include "DirectionConfig.h"
#include "EngineCamera.h"
#include <Xinput.h>
#include <mutex>
#include <cstring>
#include <cstdio>
#include <cstdarg>
#include <cmath>
#include <algorithm>
#include <cstdlib>
namespace GamepadBridge {
namespace {
std::mutex guard;
Transport::Header* channel{};
Transport::Tracking latest{};
using GetState=DWORD(WINAPI*)(DWORD,XINPUT_STATE*);
using SetState=DWORD(WINAPI*)(DWORD,XINPUT_VIBRATION*);
GetState originalGet{};SetState originalSet{};
bool enabled{},configured{},installed{},connected{};
DWORD packet{};XINPUT_GAMEPAD previous{};
uint64_t polls{},changes{},retryTick{};
wchar_t configPath[MAX_PATH]{};

void InputLog(const char* format,...) {
    FILE* f{};if(fopen_s(&f,"DeusExHRVR-input.log","a"))return;
    va_list args;va_start(args,format);vfprintf(f,format,args);va_end(args);
    fputc('\n',f);fclose(f);
}

// ---- Local patch: configurable button layout ([Buttons] in DeusExHRVR.ini) ----
// Targets: low 16 bits are XInput button bits; these flags select analog triggers.
constexpr uint32_t TargetLT=1u<<16, TargetRT=1u<<17;
struct ButtonSource {const wchar_t* key;uint16_t bit;};
// "bit" is what the stock mapper emits for that physical control.
constexpr ButtonSource buttonSources[]={
    {L"RightA",XINPUT_GAMEPAD_A},{L"RightB",XINPUT_GAMEPAD_Y},
    {L"LeftX",XINPUT_GAMEPAD_X},{L"LeftY",XINPUT_GAMEPAD_B},
    {L"LeftGrip",XINPUT_GAMEPAD_LEFT_SHOULDER},{L"RightGrip",XINPUT_GAMEPAD_RIGHT_SHOULDER},
    {L"LeftStickClick",XINPUT_GAMEPAD_LEFT_THUMB},{L"RightStickClick",XINPUT_GAMEPAD_RIGHT_THUMB},
};
constexpr size_t buttonCount=sizeof(buttonSources)/sizeof(buttonSources[0]);
uint32_t buttonTarget[buttonCount]{};
// Optional hold-to-activate per input (<Key>HoldMs): the output starts only after
// the input has been held that long, so accidental taps send nothing.
int buttonHoldMs[buttonCount]{};uint64_t buttonDownAt[buttonCount]{};bool buttonWasDown[buttonCount]{};
// With a hold delay, <Key>Tap sends another button when the input is released
// before the delay (e.g. tap = jump, hold = grenade). Sent as a 120 ms pulse.
uint32_t buttonTapTarget[buttonCount]{};uint64_t buttonTapUntil[buttonCount]{};
uint32_t leftTriggerTarget=TargetLT,rightTriggerTarget=TargetRT;
bool remapActive{};
// [ScreenButtons]: layout for everything that is not gameplay (menus, readers,
// terminals, hacking, videos, game over, loading screens).
uint32_t screenTarget[buttonCount]{};uint32_t screenLeftTrigger=TargetLT,screenRightTrigger=TargetRT;
bool screenRemapActive{};
uint32_t stickUpTarget{},stickDownTarget{}; // right stick up/down during gameplay (needs SnapTurn=1)

uint32_t ParseTarget(const wchar_t* key,uint32_t fallback,const wchar_t* section=L"Buttons") {
    wchar_t value[32]{};GetPrivateProfileStringW(section,key,L"",value,32,configPath);
    if(!*value)return fallback;
    struct Name {const wchar_t* name;uint32_t target;};
    static constexpr Name names[]={
        {L"A",XINPUT_GAMEPAD_A},{L"B",XINPUT_GAMEPAD_B},{L"X",XINPUT_GAMEPAD_X},{L"Y",XINPUT_GAMEPAD_Y},
        {L"LB",XINPUT_GAMEPAD_LEFT_SHOULDER},{L"RB",XINPUT_GAMEPAD_RIGHT_SHOULDER},
        {L"LS",XINPUT_GAMEPAD_LEFT_THUMB},{L"L3",XINPUT_GAMEPAD_LEFT_THUMB},
        {L"RS",XINPUT_GAMEPAD_RIGHT_THUMB},{L"R3",XINPUT_GAMEPAD_RIGHT_THUMB},
        {L"LT",TargetLT},{L"RT",TargetRT},
        {L"Back",XINPUT_GAMEPAD_BACK},{L"Start",XINPUT_GAMEPAD_START},
        {L"DPadUp",XINPUT_GAMEPAD_DPAD_UP},{L"DPadDown",XINPUT_GAMEPAD_DPAD_DOWN},
        {L"DPadLeft",XINPUT_GAMEPAD_DPAD_LEFT},{L"DPadRight",XINPUT_GAMEPAD_DPAD_RIGHT},
        {L"None",0},
    };
    for(const auto& n:names)if(!_wcsicmp(value,n.name))return n.target;
    InputLog("[%ls] %ls=%ls not recognized; keeping default",section,key,value);
    return fallback;
}
void LoadButtons() {
    for(size_t i=0;i<buttonCount;i++) {
        buttonTarget[i]=ParseTarget(buttonSources[i].key,buttonSources[i].bit);
        if(buttonTarget[i]!=buttonSources[i].bit)remapActive=true;
        wchar_t key[48]{};swprintf_s(key,L"%lsHoldMs",buttonSources[i].key);
        buttonHoldMs[i]=std::clamp(static_cast<int>(GetPrivateProfileIntW(L"Buttons",key,0,configPath)),0,5000);
        if(buttonHoldMs[i]>0){remapActive=true;InputLog("hold %ls=%dms",key,buttonHoldMs[i]);}
        swprintf_s(key,L"%lsTap",buttonSources[i].key);
        buttonTapTarget[i]=buttonHoldMs[i]>0?ParseTarget(key,0):0;
        if(buttonTapTarget[i])InputLog("tap %ls=%x",key,buttonTapTarget[i]);
    }
    leftTriggerTarget=ParseTarget(L"LeftTrigger",TargetLT);
    rightTriggerTarget=ParseTarget(L"RightTrigger",TargetRT);
    if(leftTriggerTarget!=TargetLT || rightTriggerTarget!=TargetRT)remapActive=true;
    for(size_t i=0;i<buttonCount;i++) {
        screenTarget[i]=ParseTarget(buttonSources[i].key,buttonSources[i].bit,L"ScreenButtons");
        if(screenTarget[i]!=buttonSources[i].bit)screenRemapActive=true;
    }
    screenLeftTrigger=ParseTarget(L"LeftTrigger",TargetLT,L"ScreenButtons");
    screenRightTrigger=ParseTarget(L"RightTrigger",TargetRT,L"ScreenButtons");
    if(screenLeftTrigger!=TargetLT || screenRightTrigger!=TargetRT)screenRemapActive=true;
    InputLog("screen buttons remap=%d RightA=%x RightB=%x LeftX=%x LeftY=%x LeftTrigger=%x RightTrigger=%x",
        screenRemapActive,screenTarget[0],screenTarget[1],screenTarget[2],screenTarget[3],screenLeftTrigger,screenRightTrigger);
    stickUpTarget=ParseTarget(L"RightStickUp",0);
    stickDownTarget=ParseTarget(L"RightStickDown",0);
    InputLog("right stick up=%x down=%x",stickUpTarget,stickDownTarget);
    InputLog("buttons remap=%d RightA=%x RightB=%x LeftX=%x LeftY=%x LeftGrip=%x RightGrip=%x LeftStickClick=%x RightStickClick=%x LeftTrigger=%x RightTrigger=%x",
        remapActive,buttonTarget[0],buttonTarget[1],buttonTarget[2],buttonTarget[3],buttonTarget[4],buttonTarget[5],
        buttonTarget[6],buttonTarget[7],leftTriggerTarget,rightTriggerTarget);
}
void Emit(XINPUT_GAMEPAD& out,uint32_t target,bool pressed,BYTE analog) {
    if(pressed && (target&0xffff))out.wButtons|=static_cast<WORD>(target&0xffff);
    if(target&TargetLT)out.bLeftTrigger=std::max(out.bLeftTrigger,analog);
    if(target&TargetRT)out.bRightTrigger=std::max(out.bRightTrigger,analog);
}
void ApplyScreenButtons(XINPUT_GAMEPAD& pad) {
    if(!screenRemapActive)return;
    const XINPUT_GAMEPAD in=pad;
    WORD owned=0;for(const auto& s:buttonSources)owned|=s.bit;
    pad.wButtons=in.wButtons&~owned;
    pad.bLeftTrigger=pad.bRightTrigger=0;
    for(size_t i=0;i<buttonCount;i++) {
        bool on=(in.wButtons&buttonSources[i].bit)!=0;
        Emit(pad,screenTarget[i],on,on?BYTE(255):BYTE(0));
    }
    Emit(pad,screenLeftTrigger,in.bLeftTrigger>127,in.bLeftTrigger);
    Emit(pad,screenRightTrigger,in.bRightTrigger>127,in.bRightTrigger);
}
void ApplyButtons(XINPUT_GAMEPAD& pad) {
    // Gameplay (and scoped aiming) use [Buttons]; everything else uses
    // [ScreenButtons]: menus (including the in-game menu), the e-reader and news
    // reader, terminals, hacking, videos, game over, and any state not recognized
    // as gameplay (loading screens, cutscenes). A reader sits over live gameplay,
    // so it takes [ScreenButtons] even though snap turn still works. A wheel screen
    // (bit 128) is the reverse: it pauses the game with the camera stopped, which
    // looks like a loading screen, but the layout must stay on [Buttons] or the
    // button being held to show the wheel would stop being held. The wheel's own
    // prompt asks for a left stick click to deselect the consumable, and nothing
    // else in the wheel uses that click, so it is sent as the game expects it
    // whatever [Buttons] maps it to; otherwise the consumable stays selected and is
    // used along with the weapon.
    float gameplayYaw=0;
    unsigned allReasons=EngineCamera::CurrentScreenReasons();
    bool gameplay=(EngineCamera::SnapTurnView(gameplayYaw) && !(allReasons&64)) || (allReasons&128)!=0;
    unsigned reasons=gameplay?0:allReasons;
    if(!gameplay && reasons!=16) {
        for(size_t i=0;i<buttonCount;i++){buttonWasDown[i]=false;buttonTapUntil[i]=0;}
        ApplyScreenButtons(pad);
        return;
    }
    if(!remapActive)return;
    const XINPUT_GAMEPAD in=pad;
    WORD owned=0;for(const auto& s:buttonSources)owned|=s.bit;
    pad.wButtons=in.wButtons&~owned; // D-pad gestures, Back and Start pulses stay as-is
    pad.bLeftTrigger=pad.bRightTrigger=0;
    auto now=GetTickCount64();
    bool wheel=(allReasons&128)!=0;
    for(size_t i=0;i<buttonCount;i++) {
        bool on=(in.wButtons&buttonSources[i].bit)!=0;
        if(wheel && buttonSources[i].bit==XINPUT_GAMEPAD_LEFT_THUMB) { // the wheel's deselect
            buttonWasDown[i]=on;buttonTapUntil[i]=0;
            Emit(pad,XINPUT_GAMEPAD_LEFT_THUMB,on,on?BYTE(255):BYTE(0));
            continue;
        }
        if(on && !buttonWasDown[i])buttonDownAt[i]=now;
        if(!on && buttonWasDown[i] && buttonTapTarget[i] && now-buttonDownAt[i]<uint64_t(buttonHoldMs[i]))
            buttonTapUntil[i]=now+120; // released before the hold delay: it was a tap
        buttonWasDown[i]=on;
        if(now<buttonTapUntil[i])Emit(pad,buttonTapTarget[i],true,BYTE(255));
        if(on && buttonHoldMs[i]>0 && now-buttonDownAt[i]<uint64_t(buttonHoldMs[i]))on=false;
        Emit(pad,buttonTarget[i],on,on?BYTE(255):BYTE(0));
    }
    Emit(pad,leftTriggerTarget,in.bLeftTrigger>127,in.bLeftTrigger);
    Emit(pad,rightTriggerTarget,in.bRightTrigger>127,in.bRightTrigger);
}

// ---- Local patch: snap turn via one injected relative mouse move ----
// Mouse look turns by a fixed angle per count, so one move gives an instant,
// repeatable snap without the right stick's acceleration ramp. The count is
// calibrated from the game camera's measured heading change and saved back.
constexpr float Pi=3.14159265f;
bool snapEnabled{},snapArmed{},snapPending{},snapMoving{};
float snapDegrees=30.f,snapYaw{};
double snapCounts=300.;
int snapDir{},snapMisses{};
uint64_t snapAt{},snapCooldown{},snapCount{},snapHoldUntil{};
int snapPauseMs=60;
bool snapHoldAdaptive{};float snapHoldYaw{};uint64_t snapHoldStart{};
void LoadSnap() {
    snapEnabled=GetPrivateProfileIntW(L"VR",L"SnapTurn",0,configPath)!=0;
    wchar_t text[32]{};
    GetPrivateProfileStringW(L"VR",L"SnapTurnDegrees",L"30",text,32,configPath);
    float degrees=static_cast<float>(_wtof(text));
    if(std::isfinite(degrees) && degrees>=5 && degrees<=180)snapDegrees=degrees;
    GetPrivateProfileStringW(L"VR",L"SnapTurnMouseCounts",L"300",text,32,configPath);
    double counts=_wtof(text);
    if(std::isfinite(counts) && counts>=1 && counts<=20000)snapCounts=counts;
    // The game appears to drop mouse look while the gamepad left stick is
    // deflected, so walking is released until the injected turn shows up.
    snapPauseMs=std::clamp(static_cast<int>(GetPrivateProfileIntW(L"VR",L"SnapTurnPauseMs",60,configPath)),0,500);
    // SnapTurnPauseMs is the maximum; the pause ends as soon as the turn is seen.
    InputLog("snapTurn enabled=%d degrees=%.1f mouseCounts=%.0f pauseMs=%d",snapEnabled,snapDegrees,snapCounts,snapPauseMs);
}
bool GameInForeground() {
    DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    return pid==GetCurrentProcessId();
}
void SnapTurn(XINPUT_GAMEPAD& pad) {
    auto now=GetTickCount64();
    float yaw=0;
    bool view=EngineCamera::SnapTurnView(yaw);
    if(snapPending && now>=snapAt+400) {
        snapPending=false;
        if(view) {
            float delta=yaw-snapYaw;
            while(delta>Pi)delta-=2*Pi;
            while(delta<-Pi)delta+=2*Pi;
            float measured=std::abs(delta)*180/Pi;
            double used=snapCounts;
            if(measured<1) {
                ++snapMisses;
                InputLog("snap #%llu dir=%d moving=%d counts=%.0f measured=%.2f deg: no turn detected (misses=%d)",snapCount,snapDir,snapMoving,used,measured,snapMisses);
            } else {
                snapMisses=0;
                double next=std::clamp(snapCounts*snapDegrees/measured,snapCounts/4,snapCounts*4);
                next=std::clamp(next,1.,20000.);
                if(std::abs(next-snapCounts)>snapCounts*.02) {
                    snapCounts=next;
                    wchar_t text[32]{};swprintf_s(text,L"%.0f",snapCounts);
                    WritePrivateProfileStringW(L"VR",L"SnapTurnMouseCounts",text,configPath);
                }
                InputLog("snap #%llu dir=%d moving=%d counts=%.0f measured=%.2f deg signed=%.2f target=%.1f nextCounts=%.0f",
                    snapCount,snapDir,snapMoving,used,measured,delta*180/Pi,snapDegrees,snapCounts);
            }
        } else {
            InputLog("snap #%llu dir=%d: view left gameplay before measurement",snapCount,snapDir);
        }
    }
    if(!view){snapArmed=false;snapHoldUntil=0;return;} // menus/terminals/scope keep the native right stick
    // During gameplay the right stick is snap-only (vertical look did nothing useful with LockVerticalCamera).
    double rx=pad.sThumbRX/32767.,ry=pad.sThumbRY/32767.;
    double magnitude=std::hypot(rx,ry);
    pad.sThumbRX=pad.sThumbRY=0;
    if(now<snapHoldUntil && snapHoldAdaptive) {
        // Release walking on the first read after the camera has visibly turned,
        // so the pause is hidden behind the snap itself.
        float d=yaw-snapHoldYaw;
        while(d>Pi)d-=2*Pi;
        while(d<-Pi)d+=2*Pi;
        if(std::abs(d)*180/Pi>=snapDegrees*.5f) {
            InputLog("snap #%llu walking resumed after %llums (turn seen)",snapCount,now-snapHoldStart);
            snapHoldUntil=0;snapHoldAdaptive=false;
        }
    }
    if(snapHoldUntil && now>=snapHoldUntil && snapHoldAdaptive) {
        InputLog("snap #%llu walking resumed after %llums (cap reached, turn not seen)",snapCount,now-snapHoldStart);
        snapHoldAdaptive=false;
    }
    if(now<snapHoldUntil){pad.sThumbLX=pad.sThumbLY=0;} // brief walking pause around the mouse move
    auto inject=[&] {
        snapYaw=yaw;snapAt=now;snapPending=true;
        INPUT input{};input.type=INPUT_MOUSE;input.mi.dwFlags=MOUSEEVENTF_MOVE;
        input.mi.dx=static_cast<LONG>(std::lround(snapCounts))*snapDir;
        if(SendInput(1,&input,sizeof(input))!=1)InputLog("snap #%llu SendInput failed error=%lu",snapCount,GetLastError());
    };
    // Up/down within 30 degrees of vertical: hold the configured button (e.g. jump/crouch).
    if(magnitude>=.6 && std::abs(ry)>=magnitude*.866) {
        auto target=ry>0?stickUpTarget:stickDownTarget;
        if(target)Emit(pad,target,true,BYTE(255));
    }
    if(magnitude<.35){snapArmed=true;return;}
    // Any flick pointing within 60 degrees of horizontal counts (diagonals included).
    if(!snapArmed || now<snapCooldown || magnitude<.6 || std::abs(rx)<magnitude*.5 || !GameInForeground())return;
    const int dead=32767/4;
    snapMoving=std::abs(int(pad.sThumbLX))>dead || std::abs(int(pad.sThumbLY))>dead;
    snapArmed=false;snapDir=rx>0?1:-1;++snapCount;
    snapPending=false; // a newer snap replaces any measurement still pending
    snapCooldown=now+60;
    if(snapMoving) { // release walking until the turn is seen (capped at SnapTurnPauseMs)
        pad.sThumbLX=pad.sThumbLY=0;
        snapHoldUntil=now+snapPauseMs;
        snapHoldAdaptive=true;snapHoldYaw=yaw;snapHoldStart=now;
    }
    inject();
}

bool Read(XINPUT_GAMEPAD& pad) {
    if(!enabled || !channel)return false;
    Transport::Tracking sample{};
    if(Transport::ReadTracking(channel,sample))latest=sample;
    auto now=GetTickCount64();
    bool fresh=latest.tick && now>=latest.tick && now-latest.tick<250;
    if(fresh && latest.gamepad.valid) {
        static_assert(sizeof(pad)==sizeof(Transport::Gamepad)-sizeof(uint32_t));
        std::memcpy(&pad,&latest.gamepad.buttons,sizeof(pad));connected=true;
        ApplyButtons(pad);
        if(snapEnabled)SnapTurn(pad);
    }
    // Keep the device connected after first activation, but release every
    // button/axis on focus loss, sleeping controllers or a stalled companion.
    return connected;
}
SHORT Stronger(SHORT a,SHORT b){return std::abs(int(a))>=std::abs(int(b))?a:b;}
DWORD WINAPI GetHook(DWORD index,XINPUT_STATE* state) {
    DWORD result=originalGet(index,state);
    if(index || !state)return result;
    std::lock_guard lock(guard);XINPUT_GAMEPAD pad{};
    if(!Read(pad))return result;
    ++polls;
    if(result==ERROR_SUCCESS) {
        const auto& real=state->Gamepad;
        pad.wButtons|=real.wButtons;
        pad.bLeftTrigger=std::max(pad.bLeftTrigger,real.bLeftTrigger);
        pad.bRightTrigger=std::max(pad.bRightTrigger,real.bRightTrigger);
        pad.sThumbLX=Stronger(pad.sThumbLX,real.sThumbLX);pad.sThumbLY=Stronger(pad.sThumbLY,real.sThumbLY);
        pad.sThumbRX=Stronger(pad.sThumbRX,real.sThumbRX);pad.sThumbRY=Stronger(pad.sThumbRY,real.sThumbRY);
    }
    if(std::memcmp(&pad,&previous,sizeof(pad))){previous=pad;++packet;++changes;}
    state->Gamepad=pad;state->dwPacketNumber=packet;
    return ERROR_SUCCESS;
}
DWORD WINAPI SetHook(DWORD index,XINPUT_VIBRATION* vibration) {
    DWORD result=originalSet(index,vibration);
    if(index || !vibration || result==ERROR_SUCCESS)return result;
    std::lock_guard lock(guard);
    return connected && enabled && channel?ERROR_SUCCESS:result; // Native rumble has no haptic mapping yet.
}
void Install() {
    if(!enabled || installed || !channel || GetTickCount64()<retryTick)return;
    retryTick=GetTickCount64()+1000;
    auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS*>(base+dos->e_lfanew);
    if(nt->FileHeader.TimeDateStamp!=0x52840914 || nt->OptionalHeader.SizeOfImage!=0x1c54000)return;
    auto va=[&](uintptr_t p){return base+p-0x400000;};
    // Supported game's PCXInputProducer resolves just GetState/SetState at
    // 0x4b3a20 and polls all four slots, including disconnected devices.
    auto module=*reinterpret_cast<HMODULE*>(va(0xe514d0));if(!module)return;
    auto get=reinterpret_cast<GetState>(GetProcAddress(module,"XInputGetState"));
    auto set=reinterpret_cast<SetState>(GetProcAddress(module,"XInputSetState"));
    if(!get || !set || *reinterpret_cast<GetState*>(va(0xe514c4))!=get || *reinterpret_cast<SetState*>(va(0xe514c8))!=set)return;
    originalGet=get;originalSet=set;
    InterlockedExchangePointer(reinterpret_cast<void* volatile*>(va(0xe514c4)),reinterpret_cast<void*>(&GetHook));
    InterlockedExchangePointer(reinterpret_cast<void* volatile*>(va(0xe514c8)),reinterpret_cast<void*>(&SetHook));
    installed=true;
    InputLog("Xbox input bridge installed: slot 0, motionControls=1");
}
}
void SetChannel(Transport::Header* header) {
    std::lock_guard lock(guard);
    if(!configured) {
        GetFullPathNameW(L"DeusExHRVR.ini",MAX_PATH,configPath,nullptr);
        enabled=DirectionConfig::MotionEnabled(configPath);configured=true;
        if(enabled){LoadButtons();LoadSnap();}
    }
    channel=header;latest={};connected=false;
}
void OnPresent(bool capture) {
    std::lock_guard lock(guard);Install();
    if(capture) {
        InputLog("enabled=%d installed=%d connected=%d polls=%llu changes=%llu packet=%lu lastButtons=%04x sticks=%d,%d/%d,%d triggers=%u,%u",
            enabled,installed,connected,polls,changes,packet,previous.wButtons,previous.sThumbLX,previous.sThumbLY,
            previous.sThumbRX,previous.sThumbRY,previous.bLeftTrigger,previous.bRightTrigger);
    }
}
}

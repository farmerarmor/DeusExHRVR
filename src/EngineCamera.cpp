#include "EngineCamera.h"
#include "CameraMath.h"
#include "DirectionConfig.h"
#include "NativeBounds.h"
#include "SceneCache.h"
#include "EngineShaderTrace.h"
#include "EffectShader.h"
#include "ShaderSwap.h"
#include "LumaPasses.h"
#include "LumaSettingsCB.h"
#include "DisplaySettings.h"
#include "ScreenMode.h"
#include <windows.h>
#include <MinHook.h>
#include <atomic>
#include <mutex>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <intrin.h>
#include "ImmersiveScope.h"
#include "WeaponCalibration.h"

// Supported executable only. All preferred addresses are rebased for ASLR.
// F6 toggles tracking. Camera poses are attached to the scene and then to the
// completed native pair, never replaced with the companion's newer predicted pose.
namespace EngineCamera {
namespace {
uintptr_t base{};
std::atomic<int> budget{};
std::atomic<uint64_t> frameId{};
std::mutex output;
std::mutex stateMutex;
Transport::Header* channel{};
Transport::TrackingReader trackingReader;
bool requested=true,referenceValid{},recenterRequested{},f6Down{},f9Down{},f7Down{},effectsFix=true,f4Down{},eyeViewFix=true,f3Down{},instanceFix=true;
bool interactionScreen{},scopeScreen{};
bool immersiveScope{},immersiveScopeActive{};
float scopeMagnification=4.f;
ImmersiveScope::Gesture scopeGesture;
std::atomic<uint64_t> scopePulseUntil{};
uint64_t scopeToggleAfter{};
bool autoScopeOwned{};
WeaponCalibration::Session calibration;
WeaponCalibration::Store calibrationStore;
bool lockVerticalCamera{};
bool experimentalMotionControls{};
bool motionControls=true;
bool controllerHideArms=true;
DirectionConfig::Source interactionAim{},movementDirection{};
thread_local bool senseQueries{};
std::atomic<uint64_t> interactionQueries{},movementAxes{};
std::atomic<void*> walkAction{},strafeAction{};
float lastMoveInput[2]{},lastMoveOutput[2]{},lastMoveHeading{};
float controllerMuzzleForward=.25f;
uint64_t removedFlareSprites{};
bool skyFix=true,f11Down{},f12Down{};
uint64_t skyCorrections{};
ScreenMode screenMode;
unsigned screenReasons{};
void RefreshScreenMode() {
    unsigned reasons=(interactionScreen?1u:0u)|(ScreenMode::Menu(base)?2u:0u)|(screenMode.Video()?4u:0u)|
        (ScreenMode::GameOver(base)?8u:0u)|(scopeScreen?16u:0u);
    if(reasons!=screenReasons) {
        screenReasons=reasons;
        FILE* f{};if(!fopen_s(&f,"DeusExHRVR-camera.log","a")) {
            fprintf(f,"automaticScreen reasons=%u requested=%d frame=%llu\n",reasons,requested,frameId.load());fclose(f);
        }
    }
}
Transport::Pose reference{};
bool levelRecenter=true;
bool yawOnlyCamera=false; // Local patch: drop game camera pitch/roll (head-bob tilt) from the VR base
CameraMath::Matrix RenderBase(const CameraMath::Matrix& game);
// Local patch: keep only heading (yaw) from the recenter pose so head pitch/roll
// at load or F9 does not tilt the world. Position (incl. height) is kept.
// Heading only: the same facing direction with pitch and roll removed.
Transport::Quaternion LevelHeading(Transport::Quaternion q) {
    auto f=CameraMath::Rotate(q,{0,0,-1});
    float yaw;
    if(std::hypot(f.x,f.z)<.001f){auto r=CameraMath::Rotate(q,{1,0,0});yaw=std::atan2(-r.z,r.x);}
    else yaw=std::atan2(-f.x,-f.z);
    return {0,std::sin(yaw*.5f),0,std::cos(yaw*.5f)};
}
Transport::Pose LevelReference(Transport::Pose p) {
    if(!levelRecenter)return p;
    p.orientation=LevelHeading(p.orientation);
    return p;
}
// Local patch (level in-game menu): LevelMenu=1 levels the head pose the view is
// frozen at while the in-game menu (map/objectives/inventory) is open.
bool levelMenu=true;
float worldScale=100.f;
// Local patch: optional per-frame camera trace for head-bob analysis.
// Buffered in memory (4 MB stdio buffer) so it rarely touches the disk.
bool bobTrace=false;FILE* bobFile{};uint32_t bobLines{};
bool sleepFix=false; // [VR] SleepFix applied (see Install)
void BobTrace(double ms,const CameraMath::Matrix& g,const float out[3],const Transport::Tracking& t) {
    if(bobLines>=36000)return;
    if(!bobFile) {
        if(fopen_s(&bobFile,"DeusExHRVR-bob.csv","w") || !bobFile){bobTrace=false;bobFile=nullptr;return;}
        setvbuf(bobFile,nullptr,_IOFBF,4<<20);
        fprintf(bobFile,"ms,x,y,z,fx,fy,fz,outX,outY,outZ,lx,ly,headX,headY,headZ,headQx,headQy,headQz,headQw\n");
    }
    fprintf(bobFile,"%.3f,%.4f,%.4f,%.4f,%.5f,%.5f,%.5f,%.4f,%.4f,%.4f,%d,%d,%.4f,%.4f,%.4f,%.5f,%.5f,%.5f,%.5f\n",
        ms,g.m[12],g.m[13],g.m[14],g.m[8],g.m[9],g.m[10],out[0],out[1],out[2],
        int(t.gamepad.leftX),int(t.gamepad.leftY),t.head.position.x,t.head.position.y,t.head.position.z,
        t.head.orientation.x,t.head.orientation.y,t.head.orientation.z,t.head.orientation.w);
    if(++bobLines>=36000){fclose(bobFile);bobFile=nullptr;}
}
// Local patch: time-average filter, used by YawSwing below for the walk
// animation's heading swing (heading is fed in as component 0).
// A(t) = time-average of the input over the last W ms. For steady motion,
// A(now) + (A(now)-A(now-W))/2 equals the current value exactly, while a
// periodic swing whose period divides W averages out. When the difference from
// the raw input exceeds the limit the model has broken down (start, stop, a
// turn): the correction is dropped and averaging restarts, rather than being
// clipped to the limit and held there by history that no longer applies.
struct BobSample {double t;float p[3];};
struct BobFilter {
    int windowMs[2]{700,333};      // [horizontal, vertical]
    float limit[2]{6.f,5.f};       // max deviation from raw camera, game units
    bool enabled{};
    float rate[2]{60.f,300.f};     // [horizontal, vertical] max change of the correction per second
    float dev[3]{};double lastT{};double verticalFrom{-1e18},horizontalFrom{-1e18};
    std::array<BobSample,1024> ring{};size_t head{},size{};
    void Reset(){size=0;dev[0]=dev[1]=dev[2]=0;}
    const BobSample& At(size_t back) const {return ring[(head+ring.size()-1-back)%ring.size()];}
    // Mean of component c over [t0,t1], linearly interpolating between samples.
    bool Average(double t0,double t1,int c,double& out) const {
        if(size<2 || At(0).t<t1-0.5 )return false;
        double sum=0,covered=0;
        for(size_t i=0;i+1<size;i++) {
            const auto& n=At(i);const auto& o=At(i+1); // newer, older
            if(n.t<=t0)break;
            double a=std::max(o.t,t0),b=std::min(n.t,t1);
            if(b<=a)continue;
            double span=n.t-o.t;
            auto val=[&](double t){return span>0?o.p[c]+(n.p[c]-o.p[c])*(t-o.t)/span:double(n.p[c]);};
            sum+=(val(a)+val(b))*.5*(b-a);covered+=b-a;
        }
        if(covered<(t1-t0)-1.)return false; // not enough history yet
        out=sum/covered;return true;
    }
    void Apply(float p[3],double now) {
        if(!enabled)return;
        if(size) {
            const auto& last=At(0);
            float jump=std::abs(p[0]-last.p[0])+std::abs(p[1]-last.p[1])+std::abs(p[2]-last.p[2]);
            if(jump>600 || now<last.t || now-last.t>500)Reset(); // load, teleport, pause
        }
        ring[head]={now,{p[0],p[1],p[2]}};head=(head+1)%ring.size();size=std::min(size+1,ring.size());
        float out[3]{p[0],p[1],p[2]};
        for(int c=0;c<3;c++) {
            int axis=c<2?0:1;double W=windowMs[axis];
            if(W<=0)continue;
            double a1,a0;
            if(c==2 && now-2*W<verticalFrom)continue;
            if(c<2 && now-2*W<horizontalFrom)continue;
            if(!Average(now-W,now,c,a1) || !Average(now-2*W,now-W,c,a0))continue;
            out[c]=float(a1+(a1-a0)/2);
        }
        // A deviation bigger than the limit means the average no longer
        // describes the input: drop the correction and restart averaging.
        float dx=out[0]-p[0],dy=out[1]-p[1],h=std::hypot(dx,dy);
        if(h>limit[0]){horizontalFrom=now;dx=dy=0;}
        float dz=out[2]-p[2];
        if(std::abs(dz)>limit[1]) {
            // Bigger than any bounce: a real height change (crouch, jump, landing).
            // Follow it now and restart vertical averaging from here.
            verticalFrom=now;dz=0;
        }
        // Rate-limit the correction so start/stop/crouch never pop in one frame.
        double dt=size>1?std::clamp((now-lastT)/1000.,0.,0.1):0.;lastT=now;
        float target[3]{dx,dy,dz};
        for(int c=0;c<3;c++){float step=float(rate[c<2?0:1]*dt);dev[c]+=std::clamp(target[c]-dev[c],-step,step);}
        p[0]+=dev[0];p[1]+=dev[1];p[2]+=dev[2];
    }
};
// Local patch: hold the eye height instead of following the game camera's
// vertical motion.
//
// Measured against the player entity's own origin (9000 frames of walking,
// crouch-walking and sprinting): the camera's X/Y equal the entity's to 0.2
// units, so this game has no lateral bob. Its height above the entity is a
// *stance* height, not a smooth signal - the game raises it ~15 units while
// crouch-walking and lowers it ~25 while sprinting, then steps back when you
// stop, which reads as a bounce. An averaging filter can smooth such a step but
// can never cancel a sustained offset.
//
// So hold the height and follow only real stance changes. Crouched and standing
// differ by ~300 units, while gait offsets are 15-25 and walking bob is about 2,
// so a trigger of 60 separates them cleanly. A real change is followed at the
// game's own speed: its crouch and stand move the camera at about 1000 units/s
// (measured over the middle 80% of the transition on the supported build).
struct StanceHold {
    bool enabled{};
    float trigger=60.f;
    static constexpr float rate=1000.f;
    float held{};bool have{},following{};
    double lastT{};
    struct Sample {double t;float v;};
    std::array<Sample,128> ring{};size_t head{},size{};
    void Reset(){have=false;following=false;size=0;}
    // True once the camera has been within band for at least 100 ms.
    bool Settled(double now,float band) const {
        if(size<4)return false;
        float lo=1e30f,hi=-1e30f;double oldest=now;
        for(size_t i=0;i<size;i++) {
            const auto& s=ring[(head+ring.size()-1-i)%ring.size()];
            if(now-s.t>120)break;
            lo=std::min(lo,s.v);hi=std::max(hi,s.v);oldest=s.t;
        }
        return now-oldest>=100 && hi-lo<band;
    }
    float Apply(float eye,double now) {
        if(!std::isfinite(eye))return eye;
        if(!have || now<lastT || now-lastT>500) { // first frame, load, teleport, pause
            held=eye;have=true;following=false;lastT=now;
            head=0;size=1;ring[0]={now,eye};
            return held;
        }
        double dt=std::clamp((now-lastT)/1000.,0.,0.1);lastT=now;
        ring[head]={now,eye};head=(head+1)%ring.size();size=std::min(size+1,ring.size());
        if(!following && std::abs(eye-held)>trigger)following=true;
        if(following) {
            float step=float(rate*dt);
            held+=std::clamp(eye-held,-step,step);
            if(std::abs(eye-held)<5.f && Settled(now,5.f)){held=eye;following=false;}
        }
        return held;
    }
} stanceHold;
// Hold the camera's horizontal position on the player's own path.
//
// Measured while running (two 46 s runs at 5.2 m/s): the game sways the camera
// sideways relative to the player entity at the stride (667 ms) and step
// (333 ms) rates, about 1 cm peak to peak, while the entity itself travels
// straight. Standing, the camera sits exactly on the entity origin; running
// leans it about 3 cm forward. So place the camera at the entity origin plus
// its offset averaged over one stride: that offset barely changes while
// moving, so the average removes the sway without lag. Running and sprinting
// offsets stay under 7 cm; anything past the limit (camera cuts, cover,
// takedowns) passes through, and the correction eases out rather than popping.
struct SwayHold {
    int windowMs{};                      // [VR] SideSwayHoldMs; 0 = off
    static constexpr float limit=45.f;   // game units (15 cm)
    static constexpr float rate=150.f;   // how fast the correction eases out, units/s
    // Never more than 4 cm from the game's camera, so deliberate camera moves
    // (cover, pull-backs) can't lag behind. Running corrections stay under it:
    // 99th percentile 3.4 cm; the cap binds on 0.2% of running frames.
    static constexpr float cap=12.f;
    struct Sample {double t;float x,y;};
    std::array<Sample,512> ring{};size_t head{},size{};
    double sumX{},sumY{},lastT{};float cx{},cy{},lastX{},lastY{};bool have{};
    void Clear(){size=0;sumX=sumY=0;}
    void Reset(){Clear();cx=cy=0;have=false;}
    const Sample& Oldest() const {return ring[(head+ring.size()-size)%ring.size()];}
    void Apply(float& camX,float& camY,float entX,float entY,double now) {
        float dx=camX-entX,dy=camY-entY;
        bool jump=!have || now<lastT || now-lastT>500 || std::hypot(dx-lastX,dy-lastY)>300; // load, teleport, pause
        double dt=have?std::clamp((now-lastT)/1000.,0.,0.1):0.;
        if(jump)Clear();
        float tx=0,ty=0;
        bool inside=std::hypot(dx,dy)<=limit;
        if(inside) {
            if(size==ring.size()){sumX-=Oldest().x;sumY-=Oldest().y;size--;}
            ring[head]={now,dx,dy};head=(head+1)%ring.size();size++;sumX+=dx;sumY+=dy;
            while(size>1 && now-Oldest().t>windowMs){sumX-=Oldest().x;sumY-=Oldest().y;size--;}
            tx=float(sumX/double(size))-dx;ty=float(sumY/double(size))-dy;
        } else Clear();
        // Follow the target while holding; ease toward it when it would jump.
        float step=float(rate*dt),ex=tx-cx,ey=ty-cy,d=std::hypot(ex,ey);
        if(inside && d<=step*4){cx=tx;cy=ty;}
        else if(d>step && d>0){cx+=ex/d*step;cy+=ey/d*step;}
        else {cx=tx;cy=ty;}
        if(float m=std::hypot(cx,cy);m>cap){cx*=cap/m;cy*=cap/m;}
        camX+=cx;camY+=cy;
        lastT=now;lastX=dx;lastY=dy;have=true;
    }
} swayHold;
// Heading (yaw) swing filter: two cascaded stages; snap turns and other jumps
// (> 3 deg in one frame) are tracked as an offset so they pass through instantly.
struct YawSwing {
    BobFilter a,b;double offset{},last{},unwrapped{};bool have{};
    YawSwing(){a.windowMs[1]=b.windowMs[1]=0;a.limit[0]=b.limit[0]=1.5f;a.rate[0]=b.rate[0]=10.f;}
    double Apply(double yaw,double ms) {
        if(!a.enabled && !b.enabled)return yaw;
        if(have){double d=std::remainder(yaw-last,360.);unwrapped+=d;if(std::abs(d)>3)offset+=d;}
        else {unwrapped=offset=yaw;}
        last=yaw;have=true;
        double in=unwrapped-offset; // swing and slow turning only
        if(std::abs(in)>720){offset=unwrapped;in=0;a.Reset();b.Reset();}
        float p[3]{float(in),0,0};
        a.Apply(p,ms);b.Apply(p,ms);
        return p[0]+offset;
    }
} yawSwing;
double PreciseMs() {
    static LARGE_INTEGER frequency{};
    if(!frequency.QuadPart)QueryPerformanceFrequency(&frequency);
    LARGE_INTEGER now{};QueryPerformanceCounter(&now);
    return double(now.QuadPart)*1000./double(frequency.QuadPart);
}
CameraMath::Matrix RenderBase(const CameraMath::Matrix& game) {
    // HorizontalDirection keeps only heading and position in PlayerCamera's
    // right/down/forward Z-up basis; the headset supplies all pitch and roll.
    if(yawOnlyCamera)return CameraMath::HorizontalDirection(game,game);
    return lockVerticalCamera?CameraMath::WithoutLookPitch(game):game;
}
struct Snapshot {
    CameraMath::Matrix originalWorld,world,view,manager;
    Transport::Tracking tracking{};
    Transport::Tracking presentationTracking{};
    bool immersiveScoped{};
    uintptr_t managerAddress{};
    void* playerInstance{};
    uint32_t inputIndex=~0u;
    bool active{};
    CameraMath::Matrix tiltedWorld; // world before LevelFrozenView, still accepted by CreateHook
    bool leveled{};
};
Snapshot current;
// Local patch (level in-game menu): the game stops updating its camera while the
// GameMenu screen is open, so the VR view freezes at the last head pose. These are
// the inputs of the last tracked update, used to rebuild that pose level.
std::atomic<bool> gameMenuOpen{};
CameraMath::Matrix lastRenderBase,lastManagerRaw,lastOldView;
uint32_t menuUpdates{};
// Frames presented since the last tracked camera update. The game also stops its
// camera on loading/briefing screens; after frozenLevelFrames the view is leveled
// there too. The in-game menu, whose opening is hooked, is leveled at once.
bool updatedSincePresent{};uint32_t presentsWithoutUpdate{};
constexpr uint32_t frozenLevelFrames=10;
// Local patch (reader buttons): the e-reader and news reader are Scaleform screens
// shown over live gameplay. While one is open, [ScreenButtons] applies; the camera
// and snap turn are unchanged.
std::atomic<bool> readerOpen{};
struct WeaponPose {void* weapon{};void* instance{};CameraMath::Matrix muzzle;uint64_t tick{};bool active{};void* owner{};};
WeaponPose weaponPose;
std::atomic<uint64_t> controllerDraws{},controllerMuzzles{};
std::atomic<uint64_t> controllerAimQueries{},controllerHiddenArms{};
std::atomic<uint64_t> controllerAttachments{},controllerBounds{};
thread_local bool nativeWeaponQuery{};
struct AttachmentTrace {uintptr_t caller{};int bone{};bool owner{};uint64_t queries{},corrected{};};
std::array<AttachmentTrace,96> attachmentTrace{};
std::mutex attachmentTraceMutex;
thread_local bool controllerDrawing{};
thread_local CameraMath::Matrix controllerDelta;
SceneCache<Snapshot> scenes;
Transport::RenderInfo pairInfo{};
uint64_t taggedScenes{},stereoCalls{};
uint64_t hudDraws{},hudMatrices{};
thread_local bool insideUpdate{};
thread_local Snapshot drawing;
thread_local unsigned drawnEyes{};
thread_local Snapshot lastWorld;
thread_local Snapshot uiSnapshot;
thread_local bool uiDrawing{};
bool effectsCapture{};
EngineShaderTrace shaderTrace;
struct EffectTrace {
    uintptr_t primitive{},material{};
    uint32_t flags{},eye{},active{},stereo{},overrideStereo{};
    float params[8]{};
    CameraMath::Matrix projection,view,world;
};
std::array<EffectTrace,2048> effectTrace{};
size_t effectCount{};
void SaveEffects(uint64_t frame) {
    std::error_code ec;std::filesystem::create_directory("DeusExHRVR-captures",ec);if(ec)return;
    char name[180];sprintf_s(name,"DeusExHRVR-captures/effects-%lu-%llu.csv",GetCurrentProcessId(),frame);
    std::ofstream out(name);out<<"primitive,material,flags,eye,active,stereo,overrideStereo";
    for(int i=0;i<8;i++)out<<",param"<<i;
    for(const char* name:{"projection","view","world"})for(int i=0;i<16;i++)out<<','<<name<<i;out<<'\n';
    for(size_t i=0;i<effectCount;i++) {
        const auto& t=effectTrace[i];out<<t.primitive<<','<<t.material<<','<<t.flags<<','<<t.eye<<','<<t.active<<','<<t.stereo<<','<<t.overrideStereo;
        for(float v:t.params)out<<','<<v;
        for(const auto* m:{&t.projection,&t.view,&t.world})for(float v:m->m)out<<','<<v;out<<'\n';
    }
}
struct SceneTrace {
    uint64_t frame{},tick{},pose{};
    uintptr_t scene{};
    uint32_t event{},reason{},cached{};
    float fov{},nearZ{},farZ{};
    CameraMath::Matrix viewport,original,tracked;
};
std::array<SceneTrace,32768> sceneTrace{};
size_t traceNext{},traceCount{};
void Trace(SceneTrace t){sceneTrace[traceNext]=t;traceNext=(traceNext+1)%sceneTrace.size();traceCount=std::min(traceCount+1,sceneTrace.size());}
void SaveTrace(uint64_t frame) {
    std::error_code ec;std::filesystem::create_directory("DeusExHRVR-captures",ec);if(ec)return;
    char name[180];sprintf_s(name,"DeusExHRVR-captures/camera-history-%lu-%llu.csv",GetCurrentProcessId(),frame);
    std::ofstream out(name);out<<"frame,tick,pose,scene,event,reason,cached,fov,near,far";
    for(const char* name:{"viewport","original","tracked"})for(int i=0;i<16;i++)out<<','<<name<<i;out<<'\n';
    for(size_t i=0;i<traceCount;i++) {
        const auto& t=sceneTrace[(traceNext+sceneTrace.size()-traceCount+i)%sceneTrace.size()];
        out<<t.frame<<','<<t.tick<<','<<t.pose<<','<<t.scene<<','<<t.event<<','<<t.reason<<','<<t.cached<<','<<t.fov<<','<<t.nearZ<<','<<t.farZ;
        for(const auto* m:{&t.viewport,&t.original,&t.tracked})for(float v:m->m)out<<','<<v;out<<'\n';
    }
}
using Update = void(__thiscall*)(void*);
using CreateScene = void*(__thiscall*)(void*,void*,void*,void*,void*,void*,uint32_t);
using Getter = void*(__thiscall*)(void*);
using Draw = void(__thiscall*)(void*,uint32_t,void*);
using Stereo = void(__cdecl*)(float*,bool,float,float);
using Primitive = void(__thiscall*)(void*,void*,bool,uint32_t);
Update originalUpdate{};
CreateScene originalCreate{};
Getter originalWorld{},originalView{},originalManager{};
Draw originalDraw{};Stereo originalStereo{};
Primitive originalPrimitive{};Update originalMatrices{};
Update originalUniforms{};
Update originalRenderState{};
using WeaponMuzzle=uintptr_t(__thiscall*)(void*,CameraMath::Matrix*,int,bool);
using WeaponAim=CameraMath::Matrix*(__thiscall*)(void*,CameraMath::Matrix*,bool,bool);
using WeaponDraw=void(__thiscall*)(void*,void*,void*);
using Skeleton=void(__cdecl*)(void*,void*,uint32_t,uint32_t,void*);
using Attachment=uintptr_t(__cdecl*)(void*,void*,int,CameraMath::Matrix*,bool);
using Bounds=void(__thiscall*)(void*,void*);
WeaponMuzzle originalWeaponMuzzle{};
WeaponAim originalWeaponAim{};
WeaponDraw originalWeaponDraw{};
WeaponDraw originalActorDraw{};
Skeleton originalSkeleton{};
Attachment originalAttachment{};
Bounds originalBounds{};
using SenseUpdate=void(__thiscall*)(void*,float);
using InputAxis=float(__thiscall*)(void*,uint32_t,bool);
SenseUpdate originalSenseUpdate{};
Getter originalPlayerWorld{};
InputAxis originalInputAxis{};
bool DirectionWorld(DirectionConfig::Source source,CameraMath::Matrix& result,CameraMath::Matrix* native=nullptr,uintptr_t manager=0) {
    std::lock_guard lock(stateMutex);
    auto now=GetTickCount64();
    if(source==DirectionConfig::Source::Mouse || !requested || !current.active || !current.playerInstance || screenReasons ||
       (manager && manager!=current.managerAddress) || now<current.tracking.tick || now-current.tracking.tick>=250)return false;
    if(native)*native=current.originalWorld;
    if(source==DirectionConfig::Source::Headset){result=current.world;return true;}
    if(!motionControls || !current.tracking.rightController.valid)return false;
    auto baseWorld=RenderBase(current.originalWorld);
    result=CameraMath::HeadWorld(baseWorld,reference,current.tracking.rightController.aim,worldScale);
    return true;
}
void __fastcall SenseUpdateHook(void* self,void*,float dt) {
    auto previous=senseQueries;
    {std::lock_guard lock(stateMutex);
        senseQueries=current.active && current.playerInstance &&
            *reinterpret_cast<void**>(static_cast<unsigned char*>(self)+8)==current.playerInstance;
    }
    originalSenseUpdate(self,dt);senseQueries=previous;
}
void* __fastcall PlayerWorldHook(void* self,void*) {
    if(senseQueries) {
        thread_local CameraMath::Matrix target;
        if(DirectionWorld(interactionAim,target)){++interactionQueries;return target.m;}
    }
    return originalPlayerWorld(self);
}
float __fastcall InputAxisHook(void* self,void*,uint32_t index,bool requireActive) {
    float value=originalInputAxis(self,index,requireActive);
    auto walk=walkAction.load(),strafe=strafeAction.load();
    if(movementDirection==DirectionConfig::Source::Mouse || !walk || !strafe || (self!=walk && self!=strafe))return value;
    {std::lock_guard lock(stateMutex);if(index!=current.inputIndex)return value;}
    CameraMath::Matrix target,native;
    if(!DirectionWorld(movementDirection,target,&native))return value;
    // Both locomotion actions must be evaluated: W can become a strafe even
    // when the original strafe action is zero/inactive. Call the trampoline
    // for the partner axis so the pair is rotated exactly once.
    float x=self==strafe?value:originalInputAxis(strafe,index,requireActive);
    float y=self==walk?value:originalInputAxis(walk,index,requireActive);
    if(!std::isfinite(x) || !std::isfinite(y))return value;
    auto axes=CameraMath::ReorientMovement(x,y,native,target);
    ++movementAxes;
    if(std::abs(x)+std::abs(y)>.01f) {
        std::lock_guard lock(stateMutex);
        lastMoveInput[0]=x;lastMoveInput[1]=y;
        lastMoveOutput[0]=axes.strafe;lastMoveOutput[1]=axes.walk;
        lastMoveHeading=CameraMath::HeadingDelta(native,target);
    }
    return self==strafe?axes.strafe:axes.walk;
}
WeaponPose ReadWeaponPose() {
    std::lock_guard lock(stateMutex);auto pose=weaponPose;
    auto now=GetTickCount64();
    pose.active=pose.active && current.active && requested && !screenReasons && now>=pose.tick && now-pose.tick<250;
    return pose;
}
bool RigidWeaponMatrix(const CameraMath::Matrix& m) {
    for(float v:m.m)if(!std::isfinite(v))return false;
    if(std::abs(m.m[15]-1.f)>.01f)return false;
    for(int r=0;r<3;r++)for(int c=r;c<3;c++) {
        float dot=0;for(int j=0;j<3;j++)dot+=m.m[r*4+j]*m.m[c*4+j];
        if(std::abs(dot-(r==c?1.f:0.f))>.02f)return false;
    }
    return true;
}
uintptr_t NativeWeaponMuzzle(void* weapon,CameraMath::Matrix* out,int barrel,bool firstPerson) {
    auto previous=nativeWeaponQuery;nativeWeaponQuery=true;
    auto result=originalWeaponMuzzle(weapon,out,barrel,firstPerson);
    nativeWeaponQuery=previous;return result;
}
uintptr_t __cdecl AttachmentHook(void* source,void* instance,int bone,CameraMath::Matrix* out,bool previousFrame) {
    auto result=originalAttachment(source,instance,bone,out,previousFrame);
    if(experimentalMotionControls && !nativeWeaponQuery && instance && out) {
        auto pose=ReadWeaponPose();
        bool corrected=false;
        if(pose.active && instance==pose.instance && RigidWeaponMatrix(*out)) {
            CameraMath::Matrix nativeMuzzle;
            NativeWeaponMuzzle(pose.weapon,&nativeMuzzle,0,true);
            if(RigidWeaponMatrix(nativeMuzzle)) {
                // Effects ask the Instance attachment getter directly, rather
                // than PrimaryWeapon's firing matrix. Preserve each attachment's
                // offset (flash, shell ejection, etc.) relative to the barrel.
                auto delta=CameraMath::Multiply(CameraMath::InverseRigid(nativeMuzzle),pose.muzzle);
                *out=CameraMath::Multiply(*out,delta);++controllerAttachments;corrected=true;
            }
        }
        if(pose.active && (instance==pose.instance || instance==pose.owner)) {
            auto caller=reinterpret_cast<uintptr_t>(_ReturnAddress())-base+0x400000;
            std::lock_guard lock(attachmentTraceMutex);
            for(auto& t:attachmentTrace)if(!t.queries || (t.caller==caller && t.bone==bone && t.owner==(instance==pose.owner))) {
                t.caller=caller;t.bone=bone;t.owner=instance==pose.owner;++t.queries;t.corrected+=corrected;break;
            }
        }
    }
    return result;
}
void __fastcall BoundsHook(void* self,void*,void* volume) {
    originalBounds(self,volume);
    if(!experimentalMotionControls || !volume || *reinterpret_cast<uintptr_t*>(self)!=base+0xaaf854-0x400000)return;
    auto pose=ReadWeaponPose();
    if(pose.active && *reinterpret_cast<void**>(static_cast<unsigned char*>(self)+8)==pose.instance) {
        // Cell lookup (0x5c8950) has no callback for type 10 (Everything):
        // forcing it here caused an indirect call through zero while loading.
        // Enlarge only finite bounds, preserving their native world centre.
        if(NativeBounds::ExpandWeapon(volume,4.f*worldScale))++controllerBounds;
    }
}
uintptr_t __fastcall WeaponMuzzleHook(void* self,void*,CameraMath::Matrix* out,int barrel,bool firstPerson) {
    auto result=NativeWeaponMuzzle(self,out,barrel,firstPerson);
    if(senseQueries)return result;
    if(experimentalMotionControls && out) {
        auto pose=ReadWeaponPose();
        if(pose.active && pose.weapon==self){*out=pose.muzzle;++controllerMuzzles;}
    }
    return result;
}
CameraMath::Matrix* __fastcall WeaponAimHook(void* self,void*,CameraMath::Matrix* out,bool firstPerson,bool forceCamera) {
    auto result=originalWeaponAim(self,out,firstPerson,forceCamera);
    // DXSense may also ask the weapon for an aim ray. Interaction selection
    // must use its own setting even when the gun follows the controller.
    if(senseQueries) {
        CameraMath::Matrix target;
        if(out && DirectionWorld(interactionAim,target)){*out=target;++interactionQueries;}
        return result;
    }
    if(experimentalMotionControls && out) {
        auto pose=ReadWeaponPose();
        // Player hit calculation 0x763370 and aim-ray getter 0x750ef0 both
        // use this matrix, bypassing WeaponMuzzle for ordinary player shots.
        // Keep the game's spread, collision and damage calculation intact.
        if(pose.active && pose.weapon==self){*out=CameraMath::FiringFromMuzzle(pose.muzzle);++controllerAimQueries;}
    }
    return result;
}
void __fastcall ActorDrawHook(void* self,void*,void* matrix,void* args) {
    if(experimentalMotionControls && controllerHideArms) {
        auto pose=ReadWeaponPose();
        // Suppress only the equipped weapon owner's actor mesh in tracked
        // player view. Animation, physics and other actors still run normally.
        if(pose.active && pose.owner && *reinterpret_cast<void**>(static_cast<unsigned char*>(self)+8)==pose.owner) {
            ++controllerHiddenArms;return;
        }
    }
    originalActorDraw(self,matrix,args);
}
void __cdecl SkeletonHook(void* model,void* bones,uint32_t flags,uint32_t count,void* state) {
    originalSkeleton(model,bones,flags,count,state);
    if(!controllerDrawing || !state)return;
    // Verified native PCDX11MatrixState: poseData is +0x10, count at +0,
    // followed by aligned world-space matrices at +0x10. This is a render
    // allocation, not the simulation's animation/bone buffer.
    auto s=static_cast<unsigned char*>(state);
    if(*reinterpret_cast<uintptr_t*>(s)!=base+0xa97524-0x400000)return;
    auto data=*reinterpret_cast<unsigned char**>(s+0x10);if(!data)return;
    auto n=*reinterpret_cast<uint32_t*>(data);if(!n || n>512 || n!=count)return;
    auto matrices=reinterpret_cast<CameraMath::Matrix*>(data+0x10);
    for(uint32_t i=0;i<n;i++)matrices[i]=CameraMath::MoveSkinMatrix(matrices[i],controllerDelta);
    ++controllerDraws;
}
void __fastcall WeaponDrawHook(void* self,void*,void* matrix,void* args) {
    auto previous=controllerDrawing;auto savedDelta=controllerDelta;controllerDrawing=false;
    if(experimentalMotionControls) {
        auto pose=ReadWeaponPose();auto drawable=static_cast<unsigned char*>(self);
        if(pose.active && *reinterpret_cast<void**>(drawable+8)==pose.instance) {
            CameraMath::Matrix nativeMuzzle;
            NativeWeaponMuzzle(pose.weapon,&nativeMuzzle,0,true);
            if(RigidWeaponMatrix(nativeMuzzle)) {
                controllerDelta=CameraMath::Multiply(CameraMath::InverseRigid(nativeMuzzle),pose.muzzle);
                controllerDrawing=true;
            }
        }
    }
    originalWeaponDraw(self,matrix,args);
    controllerDrawing=previous;controllerDelta=savedDelta;
}
struct BillboardTint {float x,y,z,w;};
// Native stack: matrix +8, sprite-list +12, sprite-count +16, tint +20.
// Both call sites push count, then list, then matrix (right-to-left).
using Billboards=void(__cdecl*)(void*,void*,uint32_t,BillboardTint,void*,float);
Billboards originalBillboards{};
void __cdecl BillboardsHook(void* matrix,void* items,uint32_t count,BillboardTint tint,void* params,float z) {
    // Only LensFlareAndCoronaID's call site. Keep the native centre/depth
    // calculation (used by its remaining light meshes), but emit no sprites.
    if(reinterpret_cast<uintptr_t>(_ReturnAddress())==base+0x722708-0x400000) {
        removedFlareSprites+=count;count=0;
    }
    originalBillboards(matrix,items,count,tint,params,z);
}
using VideoCreate=void*(__thiscall*)(void*,void*);
VideoCreate originalVideoCreate{};
Update originalVideoDestroy{};
void* __fastcall VideoCreateHook(void* self,void*,void* heap) {
    auto result=originalVideoCreate(self,heap);if(result)screenMode.Add(result);return result;
}
void __fastcall VideoDestroyHook(void* self,void*) {screenMode.Remove(self);originalVideoDestroy(self);}
EffectShader effectShaders;
ShaderSwap shaderSwap; // Luma fix port: hash-keyed native shader swap (sidesteps ReShade)
LumaPasses lumaPasses; // Luma fix port: per-eye injected passes (XeGTAO/SMAA/ModulateLighting)
LumaSettingsCB::Manager lumaSettingsCB; // Luma fix port: LumaSettings cbuffer (b13)
uint64_t instanceCorrections{};
void __fastcall RenderStateHook(void* self,void*) {
    lumaPasses.RestoreDrawOverrides();
    shaderSwap.RestoreDrawOverrides();
    auto state=static_cast<unsigned char*>(self);
    float* instance{};CameraMath::Matrix saved;
    auto shader=effectShaders.Identify(*reinterpret_cast<uintptr_t*>(state+0x198));
    // Luma port: identify the bound pixel shader in ReShade's hash space so
    // the swap table can report (and later replace) Luma-targeted passes.
    // NoteBound returns the hash (0 if unreadable/not in table) for Phase 2's
    // TrySubstitutePS call below.
    uint32_t lumaHash=0;
    if(shaderSwap.Active())
        lumaHash=shaderSwap.NoteBound(static_cast<uint32_t>(frameId.load()),
                             ShaderSwap::Stage::Pixel,
                             *reinterpret_cast<uintptr_t*>(state+0x198));
    float* skyConstants{};CameraMath::Matrix savedSky;
    // Use the native sky-layer marker, shared by sky materials across levels.
    // Verified in this executable: model draws select +5a5 from depthLayer;
    // immediate sprites clear it at 0x532d86. Cached depth constants alone
    // can still describe the preceding sky draw, so require both live flags.
    if(drawing.active && skyFix && state[0x5a4] && state[0x5a5]) {
        auto sceneCB=*reinterpret_cast<unsigned char**>(state+0x5ac);
        auto worldCB=*reinterpret_cast<unsigned char**>(state+0x5a8);
        auto sceneData=sceneCB?*reinterpret_cast<float**>(sceneCB+8):nullptr;
        if(sceneData && *reinterpret_cast<unsigned*>(sceneCB+12)>22 && sceneData[22*4+1]>.95f &&
            worldCB && *reinterpret_cast<unsigned*>(worldCB+12)>=12) {
            skyConstants=*reinterpret_cast<float**>(worldCB+8);
            if(skyConstants) {
                savedSky=CameraMath::Load(skyConstants);
                auto centre=drawing.world;for(int i=4;i<7;i++)centre.m[i]=-centre.m[i];
                auto overrideP=*reinterpret_cast<float**>(state+0x540);
                auto projection=CameraMath::Load(overrideP?static_cast<void*>(overrideP):state+0x440);
                auto corrected=CameraMath::SkyProjection(CameraMath::Load(skyConstants+16),centre,
                    projection,drawing.tracking,state[0x5ea]?0:1);
                memcpy(skyConstants,corrected.m,64);state[0x5c4]=1;state[0x1c]=1;++skyCorrections;
            }
        }
    }
    if(drawing.active && effectsFix && instanceFix && EffectShader::UsesCentreViewMatrix(shader)) {
        auto cb=*reinterpret_cast<unsigned char**>(state+0x5b8);
        if(cb && *reinterpret_cast<unsigned*>(cb+12)>=4) {
            instance=*reinterpret_cast<float**>(cb+8);
            if(instance) {
                saved=CameraMath::Load(instance);
                auto eye=CameraMath::EyeWorld(drawing.tracking,state[0x5ea]?0:1,worldScale);
                auto corrected=CameraMath::Multiply(eye,saved);memcpy(instance,corrected.m,64);
                state[0x5c8]=1;state[0x1c]=1;++instanceCorrections;
            }
        }
    }
    originalRenderState(self);
    if(effectsCapture && shaderSwap.Active())
        shaderSwap.CaptureDraw(static_cast<uint32_t>(frameId.load()), lumaHash, state[0x5ea]?0:1);
    // Luma port: the engine has just bound its pixel shader. If substitution
    // is enabled (F12 or [Luma] SubstituteShaders=1) and this draw's PS has a
    // Luma replacement, override it now so the imminent draw uses the fix.
    // Runs per-draw on the render thread; TrySubstitutePS is a no-op when
    // disabled or when no replacement exists for this hash.
    if(lumaHash && shaderSwap.SubstitutionEnabled())
        shaderSwap.TrySubstitutePS(lumaHash,
            EffectShader::UsesCentreViewMatrix(shader));
    // Luma port Phase 3: injected passes (XeGTAO/SMAA/ModulateLighting).
    // Evaluate triggers against the bound PS hash; OnDraw is a no-op unless a
    // trigger matches and the pass is enabled. Per-eye is automatic: the hook
    // fires per draw, and the engine renders each eye separately.
    if(lumaPasses.Loaded())
        lumaPasses.OnDraw(static_cast<uint32_t>(frameId.load()), lumaHash,
                          state[0x5ea]?0:1);
    if(effectsCapture)shaderTrace.Record(state,drawing.active);
    // Upload copied the corrected constants. Restore the engine's centre-view
    // copy so subsequent draws/eyes cannot accumulate the eye transform.
    if(instance){memcpy(instance,saved.m,64);state[0x5c8]=1;state[0x1c]=1;}
    if(skyConstants){memcpy(skyConstants,savedSky.m,64);state[0x5c4]=1;state[0x1c]=1;}
}
void __fastcall UniformsHook(void* self,void*) {
    originalUniforms(self);
    if(!drawing.active)return;
    auto scene=static_cast<unsigned char*>(self);
    auto device=*reinterpret_cast<unsigned char**>(base+0x12ab940-0x400000);
    auto state=*reinterpret_cast<unsigned char**>(device+0x150);
    unsigned eye=state[0x5ea]?0:1;
    auto cb=*reinterpret_cast<unsigned char**>(state+0x5ac);
    auto data=*reinterpret_cast<float**>(cb+8);
    float before[20];memcpy(before,data+15*4,sizeof(before));
    if(effectsFix) {
        auto world=CameraMath::Load(scene+0x40);
        if(eyeViewFix) {
            // Keep V * eyeInverse * P unchanged for geometry, but expose the
            // actual eye view and camera position to all shared shader inputs.
            auto eyeWorld=CameraMath::EyeWorld(drawing.tracking,eye,worldScale);
            auto view=CameraMath::Multiply(CameraMath::Load(scene+0x2b0),CameraMath::InverseRigid(eyeWorld));
            memcpy(state+0x480,view.m,64);state[0x545]=1;
            auto eyeWorldAbsolute=CameraMath::Multiply(eyeWorld,world);
            for(int i=0;i<3;i++){data[10*4+i]=eyeWorldAbsolute.m[12+i];data[11*4+i]=eyeWorldAbsolute.m[8+i];}
        }
        auto depth=CameraMath::DepthToWorld(world,drawing.tracking,eye,worldScale);
        // SceneBuffer stores the three output components as transposed rows.
        for(int col=0;col<3;col++)for(int row=0;row<4;row++)data[(15+col)*4+row]=depth.m[row*4+col];
        const auto& e=drawing.tracking.eyes[eye];
        float l=std::tan(e.left),r=std::tan(e.right),u=std::tan(e.up),d=std::tan(e.down);
        float view[]={r-l,d-u,l,u,1/(r-l),1/(d-u),0,0};memcpy(data+18*4,view,sizeof(view));
        state[0x5c5]=1;state[0x1c]=1;
    }
    if(effectsCapture) {
        FILE* f{};if(!fopen_s(&f,"DeusExHRVR-effects.log","a")) {
            fprintf(f,"depth frame=%llu eye=%u fix=%d eyeView=%d scene=%p\n",frameId.load()+1,eye,effectsFix,eyeViewFix,self);
            fprintf(f,"before");for(float v:before)fprintf(f," %.8g",v);fprintf(f,"\nafter");
            for(int i=0;i<20;i++)fprintf(f," %.8g",data[15*4+i]);fprintf(f,"\n");fclose(f);
        }
    }
}
uintptr_t VA(uintptr_t preferred) { return base+preferred-0x400000; }
void Matrix(FILE* f,const char* name,const void* data) {
    const float* m=static_cast<const float*>(data);
    fprintf(f,"%s",name);
    for(int i=0;i<16;i++)fprintf(f," %.7g",m[i]);
    fputc('\n',f);
}
void __fastcall UpdateHook(void* self,void*) {
    insideUpdate=true;
    originalUpdate(self);
    insideUpdate=false;
    if(movementDirection!=DirectionConfig::Source::Mouse && (!walkAction.load() || !strafeAction.load())) {
        using FindAction=void*(__cdecl*)(const char*);
        auto find=reinterpret_cast<FindAction>(base+0x4ae340-0x400000);
        walkAction=find("locomotionwalk");strafeAction=find("locomotionstrafe");
    }
    {
        std::lock_guard lock(stateMutex);
        bool calibrationUpdated=false;
        if(recenterRequested || !requested || screenReasons || gameMenuOpen)calibration.Cancel();
        current.active=false;
        current.leveled=false;
        current.inputIndex=~0u;
        weaponPose={};
        auto manager=static_cast<unsigned char*>(self);
        auto active=*reinterpret_cast<unsigned char**>(manager+0x30);
        const bool nativeScope=ScreenMode::Scope(base,manager);
        Transport::Tracking scopeTracking{};
        const auto now=GetTickCount64();
        bool scopeWeapon=false;
        if(active==manager+0x6f0) {
            auto entity=*reinterpret_cast<void**>(active+0xaa0);
            using Equipped=unsigned char*(__cdecl*)(void*);
            auto holder=entity?reinterpret_cast<Equipped>(base+0x66af40-0x400000)(entity):nullptr;
            auto weapon=holder?*reinterpret_cast<unsigned char**>(holder+0x14):nullptr;
            auto data=weapon?*reinterpret_cast<unsigned char**>(weapon+0x7c):nullptr;
            scopeWeapon=data && data[0x425]!=0;
        }
        const bool scopeAllowed=immersiveScope && motionControls && experimentalMotionControls && requested &&
            scopeWeapon && !(screenReasons&~16u) && !gameMenuOpen && trackingReader.Read(channel,scopeTracking,now) && scopeTracking.rightController.valid;
        const bool calibrating=scopeTracking.controllerButtons==3 || calibration.Busy();
        if(calibrating)scopePulseUntil=0;
        const bool nearEye=scopeGesture.Update(scopeTracking,scopeAllowed&&!calibrating,now);
        if(scopeAllowed && !calibrating && now>=scopeToggleAfter && ((nearEye&&!nativeScope)||(!nearEye&&nativeScope&&autoScopeOwned))) {
            scopePulseUntil=now+120;scopeToggleAfter=now+650;
            autoScopeOwned=nearEye;
        }
        if(!scopeWeapon){autoScopeOwned=false;scopePulseUntil=0;}
        immersiveScopeActive=scopeAllowed && nativeScope;
        scopeScreen=nativeScope&&!immersiveScopeActive;
        // Supported build: PlayerCamera embeds CameraMode_Hacking at +0x430.
        // Its enter/leave methods (0x6a1c50 / 0x6a1dc0) set/clear +0x104.
        // Check the exact class before reading its active flag.
        auto hacking=manager+0x6f0+0x430;
        bool screen=*reinterpret_cast<uintptr_t*>(hacking)==base+0xaa774c-0x400000 && hacking[0x104]!=0;
        if(screen!=interactionScreen) {
            interactionScreen=screen;
            FILE* f{};if(!fopen_s(&f,"DeusExHRVR-camera.log","a")) {
                fprintf(f,"interactionScreen=%d requested=%d frame=%llu\n",screen,requested,frameId.load());fclose(f);
            }
        }
        Transport::Tracking t{};
        RefreshScreenMode();
        if(requested && !screenReasons && active && trackingReader.Read(channel,t,GetTickCount64())) {
            if(!referenceValid || recenterRequested){reference=LevelReference(t.head);referenceValid=true;recenterRequested=false;}
            // Interaction cameras implement the same virtual getters as the
            // player camera; use that interface instead of its private layout.
            current.originalWorld=CameraMath::Load(originalWorld(self));
            // Keep native aiming/input intact. Level only the VR rendering
            // base, then add the headset's complete orientation and position.
            auto renderBase=RenderBase(current.originalWorld);
            // Other camera modes skip the filter; its jump/gap checks reset it when gameplay resumes.
            if(active==manager+0x6f0) {
                double ms=PreciseMs();
                if(stanceHold.enabled) {
                    // Player entity origin: three floats at +0x20 (measured on the supported
                    // build; the camera's X/Y match it exactly, its Z sits an eye height below).
                    auto entity=*reinterpret_cast<const unsigned char* const*>(active+0xaa0);
                    float entZ=entity?*reinterpret_cast<const float*>(entity+0x28):0.f;
                    float eye=entity?renderBase.m[14]-entZ:0.f;
                    if(entity && std::isfinite(eye) && eye>1.f && eye<900.f)
                        renderBase.m[14]=entZ+stanceHold.Apply(eye,ms);
                    else stanceHold.Reset(); // no player (menus, cutscenes): leave the camera alone
                }
                if(swayHold.windowMs>0) {
                    auto entity=*reinterpret_cast<const unsigned char* const*>(active+0xaa0);
                    float entX=entity?*reinterpret_cast<const float*>(entity+0x20):0.f;
                    float entY=entity?*reinterpret_cast<const float*>(entity+0x24):0.f;
                    if(entity && std::isfinite(entX) && std::isfinite(entY))
                        swayHold.Apply(renderBase.m[12],renderBase.m[13],entX,entY,ms);
                    else swayHold.Reset();
                }
                if(yawOnlyCamera && (yawSwing.a.enabled||yawSwing.b.enabled)) {
                    // renderBase is pure heading here: rebuild it from the filtered yaw.
                    double yaw=std::atan2(renderBase.m[9],renderBase.m[8])*180/3.14159265358979;
                    double f=yawSwing.Apply(yaw,ms)*3.14159265358979/180;
                    float x=float(std::cos(f)),y=float(std::sin(f));
                    renderBase.m[0]=y;renderBase.m[1]=-x;renderBase.m[2]=0;
                    renderBase.m[8]=x;renderBase.m[9]=y;renderBase.m[10]=0;
                }
                if(bobTrace)BobTrace(ms,current.originalWorld,renderBase.m+12,t);
            }
            if(experimentalMotionControls && t.rightController.valid && active==manager+0x6f0) {
                auto entity=*reinterpret_cast<void**>(active+0xaa0);
                using Equipped=unsigned char*(__cdecl*)(void*);
                auto holder=entity?reinterpret_cast<Equipped>(base+0x66af40-0x400000)(entity):nullptr;
                auto weapon=holder?*reinterpret_cast<unsigned char**>(holder+0x14):nullptr;
                if(weapon && *reinterpret_cast<uintptr_t*>(weapon)==base+0xab1944-0x400000) {
                    using FindInstance=void*(__cdecl*)(uint32_t);
                    auto handle=*reinterpret_cast<uint32_t*>(weapon+0x6c);
                    auto instance=handle!=0x7fffffffu?reinterpret_cast<FindInstance>(base+0x6082a0-0x400000)(handle):nullptr;
                    if(instance) {
                        auto raw=CameraMath::ControllerMuzzle(renderBase,reference,t.rightController.aim,worldScale,controllerMuzzleForward);
                        // Native weapon constructor 0x761130 stores the asset-table ID at +0x58.
                        // 0x777040 resolves its model through that ID, independent of instance handles.
                        auto key=*reinterpret_cast<uint32_t*>(weapon+0x58);
                        if(key<0x18000) {
                            auto offset=calibrationStore.Get(key);
                            auto result=calibration.Update(WeaponCalibration::Metres(raw,worldScale),offset,key,
                                reinterpret_cast<uintptr_t>(instance),t.controllerButtons,
                                t.gamepad.valid && !nativeScope && !gameMenuOpen,now);
                            calibrationUpdated=true;
                            if(result.event==WeaponCalibration::Event::Frozen)MessageBeep(MB_OK);
                            if(result.event==WeaponCalibration::Event::Save) {
                                bool saved=calibrationStore.Save(key,result.offset);
                                FILE* f{};if(!fopen_s(&f,"DeusExHRVR-camera.log","a")){fprintf(f,"weaponCalibration key=%08x saved=%d\n",key,saved);fclose(f);}
                                MessageBeep(saved?MB_OK:MB_ICONERROR);
                                if(!saved)result.muzzle=CameraMath::Multiply(offset,WeaponCalibration::Metres(raw,worldScale));
                            }
                            raw=WeaponCalibration::Units(result.muzzle,worldScale);
                        }
                        weaponPose={weapon,instance,raw,t.tick,true,*reinterpret_cast<void**>(weapon+0x64)};
                    }
                }
            }
            current.world=CameraMath::HeadWorld(renderBase,reference,t.head,worldScale);
            if(immersiveScopeActive) {
                // Use the same calibrated firing line as the rendered rifle and shots.
                if(weaponPose.active)current.world=WeaponCalibration::Camera(weaponPose.muzzle);
            }
            current.view=CameraMath::InverseRigid(current.world);
            current.manager=CameraMath::Load(manager+0x13b0);
            auto oldView=CameraMath::Load(originalView(self));
            lastRenderBase=renderBase;lastManagerRaw=current.manager;lastOldView=oldView;
            updatedSincePresent=true;
            // The camera never runs while the in-game menu is open (it stops before the
            // menu's open call). If it keeps running, the close call was missed.
            if(!gameMenuOpen)menuUpdates=0;
            else if(++menuUpdates==30) {
                gameMenuOpen=false;
                FILE* f{};if(!fopen_s(&f,"DeusExHRVR-camera.log","a")){fprintf(f,"gameMenu flag cleared: camera running frame=%llu\n",frameId.load());fclose(f);}
            }
            for(int col=0;col<3;col++) {
                float scale=0;for(int row=0;row<3;row++)scale+=current.manager.m[row*4+col]*oldView.m[row*4+col];
                for(int row=0;row<3;row++)current.manager.m[row*4+col]=current.view.m[row*4+col]*scale;
                current.manager.m[12+col]+=(current.view.m[12+col]-oldView.m[12+col])*scale;
            }
            current.presentationTracking=t;
            current.immersiveScoped=immersiveScopeActive;
            current.tracking=immersiveScopeActive?ImmersiveScope::Zoom(t,scopeMagnification):t;
            current.active=true;current.managerAddress=reinterpret_cast<uintptr_t>(self);
            current.playerInstance=active==manager+0x6f0?*reinterpret_cast<void**>(active+0xaa0):nullptr;
            if(current.playerInstance && movementDirection!=DirectionConfig::Source::Mouse) {
                using FindPlayer=unsigned char*(__cdecl*)(void*);
                auto player=reinterpret_cast<FindPlayer>(base+0x6032c0-0x400000)(current.playerInstance);
                if(player)current.inputIndex=*reinterpret_cast<uint32_t*>(player+0x1c);
            }

        }
        if(!calibrationUpdated)calibration.Cancel();
    }
    if(budget.load()<=0)return;
    std::lock_guard lock(output);
    FILE* f{};if(fopen_s(&f,"DeusExHRVR-camera.log","a"))return;
    auto manager=static_cast<unsigned char*>(self);
    auto active=*reinterpret_cast<unsigned char**>(manager+0x30);
    fprintf(f,"camera frame=%llu manager=%p active=%p typeVtable=%08x player=%d\n",frameId.load(),self,active,
        active?unsigned(*reinterpret_cast<uintptr_t*>(active)-base+0x400000):0,active==manager+0x6f0);
    Matrix(f,"manager",manager+0x13b0);
    if(active==manager+0x6f0) {Matrix(f,"playerWorld",active+0x40);Matrix(f,"playerView",active+0x80);}
    fclose(f);
}
void* Get(void* self,Getter original,int kind,uintptr_t caller) {
    if(senseQueries && kind<2) {
        CameraMath::Matrix target;
        thread_local CameraMath::Matrix targetMatrices[2];
        if(DirectionWorld(interactionAim,target,nullptr,reinterpret_cast<uintptr_t>(self))) {
            targetMatrices[kind]=kind?CameraMath::InverseRigid(target):target;
            ++interactionQueries;return targetMatrices[kind].m;
        }
        return original(self);
    }
    // Locomotion already receives reoriented axes; its secondary camera
    // consumers must keep the native basis to avoid applying head yaw twice.
    if(kind<2 && caller>=base+0x77a930-0x400000 && caller<base+0x77bd80-0x400000)return original(self);
    if(insideUpdate)return original(self);
    thread_local CameraMath::Matrix values[3];
    {
        std::lock_guard lock(stateMutex);
        if(current.active && current.managerAddress==reinterpret_cast<uintptr_t>(self)) {
            values[kind]=kind==0?current.world:kind==1?current.view:current.manager;
            return values[kind].m;
        }
    }
    return original(self);
}
void* __fastcall WorldHook(void* self,void*){return Get(self,originalWorld,0,reinterpret_cast<uintptr_t>(_ReturnAddress()));}
void* __fastcall ViewHook(void* self,void*){return Get(self,originalView,1,reinterpret_cast<uintptr_t>(_ReturnAddress()));}
void* __fastcall ManagerHook(void* self,void*){return Get(self,originalManager,2,reinterpret_cast<uintptr_t>(_ReturnAddress()));}
bool Match(const CameraMath::Matrix& viewport,const CameraMath::Matrix& player) {
    for(int i=0;i<16;i++) {
        float value=(i>=4&&i<7)?-player.m[i]:player.m[i];
        if(std::abs(viewport.m[i]-value)>(i>=12?2.f:0.02f))return false;
    }
    return true;
}
void* __fastcall CreateHook(void* self,void*,void* viewport,void* target,void* depth,void* source,void* sourceDepth,uint32_t flags) {
    Snapshot snapshot;
    {std::lock_guard lock(stateMutex);snapshot=current;}
    alignas(16) unsigned char adjusted[0xf0];
    SceneTrace trace{};trace.event=1;trace.reason=1;
    if(viewport){auto v=static_cast<float*>(viewport);trace.fov=v[8];trace.nearZ=v[6];trace.farZ=v[7];trace.viewport=CameraMath::Load(v+12);}
    trace.original=snapshot.originalWorld;trace.tracked=snapshot.world;trace.pose=snapshot.tracking.id;
    if(viewport && snapshot.active) {
        auto p=static_cast<float*>(viewport);auto matrix=CameraMath::Load(p+12);
        if(p[8]>0 && p[7]>1000 && (Match(matrix,snapshot.originalWorld)||Match(matrix,snapshot.world)||
            (snapshot.leveled && Match(matrix,snapshot.tiltedWorld)))) {
            trace.reason=Match(matrix,snapshot.originalWorld)?5:6;
            memcpy(adjusted,viewport,sizeof(adjusted));auto v=reinterpret_cast<float*>(adjusted);
            memcpy(v+12,snapshot.world.m,64);for(int i=4;i<7;i++)v[12+i]=-v[12+i];
            float maxX=0,maxY=0;
            for(const auto& eye:snapshot.tracking.eyes) {
                maxX=std::max(maxX,std::max(std::abs(std::tan(eye.left)),std::abs(std::tan(eye.right))));
                maxY=std::max(maxY,std::max(std::abs(std::tan(eye.up)),std::abs(std::tan(eye.down))));
            }
            v[8]=2*std::atan(maxY);v[9]=maxX/maxY;viewport=adjusted;
        } else {trace.reason=(p[8]>0 && p[7]>1000)?2:3;snapshot.active=false;}
    }
    void* result=originalCreate(self,viewport,target,depth,source,sourceDepth,flags);
    {
        std::lock_guard lock(stateMutex);
        if(snapshot.active && result) {scenes.Store(result,snapshot,frameId.load()+1);++taggedScenes;}
        else scenes.Erase(result);
        if(trace.fov>0 && trace.farZ>1000){trace.frame=frameId.load()+1;trace.tick=GetTickCount64();trace.scene=reinterpret_cast<uintptr_t>(result);trace.cached=uint32_t(scenes.Size());Trace(trace);}
    }
    if(budget.load()>0 && budget.fetch_sub(1)>0 && viewport && result) {
        std::lock_guard lock(output);
        FILE* f{};if(!fopen_s(&f,"DeusExHRVR-camera.log","a")) {
            auto vp=static_cast<unsigned char*>(viewport);auto s=static_cast<unsigned char*>(result);
            auto v=reinterpret_cast<float*>(vp);
            fprintf(f,"scene frame=%llu scene=%p target=%p depth=%p flags=%08x parent=%p near=%g far=%g fov=%g aspect=%g width=%g height=%g\n",
                frameId.load(),result,target,depth,flags,*reinterpret_cast<void**>(s+0x404),v[6],v[7],v[8],v[9],v[10],v[11]);
            Matrix(f,"viewport",vp+0x30);Matrix(f,"view",s+0x2b0);Matrix(f,"projection",s+0x2f0);
            fclose(f);
        }
    }
    return result;
}
void __fastcall DrawHook(void* self,void*,uint32_t pass,void* other) {
    auto previous=drawing;auto previousEyes=drawnEyes;
    {
        std::lock_guard lock(stateMutex);auto scene=static_cast<unsigned char*>(self)-4;drawing=scenes.Find(scene);
        auto v=reinterpret_cast<float*>(scene+0x10);
        if(v[8]>0 && v[7]>1000){
            SceneTrace trace{};trace.frame=frameId.load()+1;trace.tick=GetTickCount64();trace.pose=drawing.tracking.id;trace.scene=reinterpret_cast<uintptr_t>(scene);
            trace.event=2;trace.reason=drawing.active?5:1;trace.cached=uint32_t(scenes.Size());trace.fov=v[8];trace.nearZ=v[6];trace.farZ=v[7];
            trace.viewport=CameraMath::Load(v+12);trace.original=drawing.originalWorld;trace.tracked=drawing.world;Trace(trace);
        }
    }
    drawnEyes=0;
    originalDraw(self,pass,other);
    if(drawing.active && drawnEyes) {
        lastWorld=drawing;
        std::lock_guard lock(stateMutex);
        if(pairInfo.mode==0){pairInfo.mode=1;pairInfo.tracking=drawing.immersiveScoped?drawing.presentationTracking:drawing.tracking;}
        if(pairInfo.tracking.id!=drawing.tracking.id)pairInfo.mode=2;
        pairInfo.eyeMask|=drawnEyes;
    }
    drawing=previous;drawnEyes=previousEyes;
}
void __fastcall MatricesHook(void* self,void*) {
    if(!uiDrawing){originalMatrices(self);return;}
    auto state=static_cast<unsigned char*>(self);
    auto& overrideMatrix=*reinterpret_cast<float**>(state+0x540);
    auto saved=overrideMatrix;
    auto source=CameraMath::Load(saved?saved:reinterpret_cast<float*>(state+0x440));
    // Scaleform uses a perspective override here too. The correction operates
    // on its resulting clip coordinates, so the source projection may be either.
    unsigned eye=state[0x5ea]?0:1;
    auto projected=CameraMath::Multiply(source,CameraMath::HudClipTransform(uiSnapshot.tracking,eye));
    auto stereoEnabled=state[0x5e9];
    overrideMatrix=projected.m;state[0x5e9]=0;state[0x546]=1;
    originalMatrices(self);
    overrideMatrix=saved;state[0x5e9]=stereoEnabled;
    ++hudMatrices;
}
void __fastcall PrimitiveHook(void* self,void*,void* stream,bool backBeforeFront,uint32_t flags) {
    auto primitive=static_cast<unsigned char*>(self);
    auto primitiveState=*reinterpret_cast<unsigned char**>(primitive+0x10);
    auto snapshot=drawing.active?drawing:lastWorld;
    // scaleformData is consumed only by the engine's UI shader path at 0x532d3c.
    bool isUI=stream && snapshot.active && primitiveState && *reinterpret_cast<void**>(primitiveState+0x20);
    if(effectsCapture && stream && primitiveState && !*reinterpret_cast<void**>(primitiveState+0x20) && effectCount<effectTrace.size()) {
        auto device=*reinterpret_cast<unsigned char**>(VA(0x12ab940));auto state=*reinterpret_cast<unsigned char**>(device+0x150);
        auto& t=effectTrace[effectCount++];t={};t.primitive=reinterpret_cast<uintptr_t>(self);t.material=*reinterpret_cast<uintptr_t*>(primitiveState+0xc);
        t.flags=*reinterpret_cast<uint32_t*>(primitiveState);t.eye=state[0x5ea]?0:1;t.active=snapshot.active;t.stereo=state[0x5e9];t.overrideStereo=state[0x5e8];
        auto params=*reinterpret_cast<float**>(primitiveState+0x1c);if(params)memcpy(t.params,params,sizeof(t.params));
        auto projection=*reinterpret_cast<float**>(state+0x540);t.projection=CameraMath::Load(projection?projection:reinterpret_cast<float*>(state+0x440));
        t.view=CameraMath::Load(state+0x480);t.world=CameraMath::Load(state+0x500);
    }
    if(!isUI){originalPrimitive(self,stream,backBeforeFront,flags);return;}
    auto device=*reinterpret_cast<unsigned char**>(VA(0x12ab940));
    auto state=*reinterpret_cast<unsigned char**>(device+0x150);
    auto previousUI=uiDrawing;auto previousSnapshot=uiSnapshot;
    uiDrawing=true;uiSnapshot=snapshot;
    MatricesHook(state,nullptr);
    originalPrimitive(self,stream,backBeforeFront,flags);
    uiDrawing=previousUI;uiSnapshot=previousSnapshot;
    state[0x546]=1;originalMatrices(state);
    ++hudDraws;
}
void __cdecl StereoHook(float* projection,bool firstEye,float width,float plane) {
    if(drawing.active && std::abs(projection[11]-1.f)<0.001f && std::abs(projection[15])<0.001f) {
        unsigned eye=firstEye?0:1;
        auto source=CameraMath::Load(projection);
        auto p=effectsFix&&eyeViewFix?CameraMath::EyeFrustum(source,drawing.tracking,eye):CameraMath::EyeProjection(source,drawing.tracking,eye,worldScale);
        memcpy(projection,p.m,64);drawnEyes|=1u<<eye;
        {std::lock_guard lock(stateMutex);++stereoCalls;}
    } else originalStereo(projection,firstEye,width,plane);
}
// Local patch (level in-game menu): while the in-game menu or a loading/briefing
// screen is up the game stops updating its camera, so every frame reuses the last
// head pose, pitch and roll included, and the screen (a head-relative plane) is
// pinned at that tilt. Once the pose is frozen, turn it about the head to heading
// only, the same as LevelReference, and rebuild the camera from the last update's
// inputs. The eyes keep their offsets from the head, the game world stays where it
// is and the screen comes out level. Runs under stateMutex, once per frozen pose.
void LevelFrozenView(uint64_t frame) {
    if(updatedSincePresent || !current.active)presentsWithoutUpdate=0;else ++presentsWithoutUpdate;
    updatedSincePresent=false;
    if(!levelMenu || !current.active || current.leveled)return;
    bool menu=gameMenuOpen;
    // The in-game menu is known to be open: level at once. Anything else must stay
    // frozen for a while first, so a brief stall in gameplay is left alone.
    if(presentsWithoutUpdate<(menu?1u:frozenLevelFrames))return;
    uint64_t id=current.tracking.id;
    auto& t=current.tracking;
    auto fwd=CameraMath::Rotate(t.head.orientation,{0,0,-1}),right=CameraMath::Rotate(t.head.orientation,{1,0,0});
    double pitch=std::asin(std::clamp(double(fwd.y),-1.,1.))*180/3.14159265358979;
    double roll=std::asin(std::clamp(double(right.y),-1.,1.))*180/3.14159265358979;
    auto level=LevelHeading(t.head.orientation);
    auto turn=CameraMath::Multiply(level,CameraMath::Inverse(t.head.orientation));
    const auto head=t.head.position;
    for(auto& e:t.eyes) {
        auto offset=CameraMath::Rotate(turn,{e.pose.position.x-head.x,e.pose.position.y-head.y,e.pose.position.z-head.z});
        e.pose.position={head.x+offset.x,head.y+offset.y,head.z+offset.z};
        e.pose.orientation=CameraMath::Multiply(turn,e.pose.orientation);
    }
    t.head.orientation=level;
    current.tiltedWorld=current.world;
    current.world=CameraMath::HeadWorld(lastRenderBase,reference,t.head,worldScale);
    current.view=CameraMath::InverseRigid(current.world);
    current.manager=lastManagerRaw;
    for(int col=0;col<3;col++) {
        float scale=0;for(int row=0;row<3;row++)scale+=current.manager.m[row*4+col]*lastOldView.m[row*4+col];
        for(int row=0;row<3;row++)current.manager.m[row*4+col]=current.view.m[row*4+col]*scale;
        current.manager.m[12+col]+=(current.view.m[12+col]-lastOldView.m[12+col])*scale;
    }
    current.leveled=true;
    FILE* f{};if(!fopen_s(&f,"DeusExHRVR-camera.log","a")) {
        fprintf(f,"levelMenu frame=%llu pose=%llu reason=%s removedPitch=%.1f removedRoll=%.1f\n",frame,id,menu?"menu":"frozen",pitch,roll);fclose(f);
    }
}
#if defined(_M_IX86)
// Local patch (level in-game menu, reader buttons): the in-game menu
// (map/objectives/inventory), the e-reader and the news reader are the game's
// NsGameMenu, NsIReader and NsNewsReader movie controllers: Scaleform screens, not
// IMenu classes (title, pause, ending, making-of) that ScreenMode can see. Hook
// their activate/deactivate (vtable slots 4/5, found through the game's RTTI) to
// know when they are open. Each hook is a pass-through: it saves every register
// and flag, notes the event, restores them and jumps into the original function,
// so it cannot disturb the function's arguments or calling convention. Every
// target is verified and hooked on its own: a mismatch skips only that hook and
// never affects the camera hooks. Verification bytes stop before any absolute
// address, which ASLR relocates.
struct ScreenHook {uintptr_t address;const char* name;const char* event;const char* bytes;size_t length;};
const ScreenHook screenHooks[]={
    {0x7e8630,"GameMenu","open","\x53\x56\xbb\x01\x00\x00\x00",7},{0x7e8be0,"GameMenu","close","\x53\x56\x8b\xf1\x8b\x4e\x30",7},
    {0x7f08c0,"IReader","open","\x83\xec\x30\x53\x56\x8b\xf1",7},{0x7f0530,"IReader","close","\x83\xec\x30\x53\x56\x8b\xf1\x57",8},
    {0x7f7e70,"NewsReader","open","\x83\xec\x20\x53\x56\x8b\xf1",7},{0x7f76c0,"NewsReader","close","\x83\xec\x20\x53\x56\x8b\xf1\x33",8},
};
constexpr int screenHookCount=int(sizeof(screenHooks)/sizeof(screenHooks[0]));
void* screenOriginals[screenHookCount]{};
void __cdecl NoteScreen(int index,void* self) {
    bool open=index%2==0;
    if(index<2)gameMenuOpen=open;else readerOpen=open;
    FILE* f{};if(!fopen_s(&f,"DeusExHRVR-camera.log","a")) {
        fprintf(f,"screen %s %s self=%p frame=%llu\n",screenHooks[index].name,screenHooks[index].event,self,frameId.load());fclose(f);
    }
}
#define SCREEN_STUB(n) __declspec(naked) void ScreenStub##n() { \
    __asm pushad __asm pushfd __asm push ecx __asm push n __asm call NoteScreen __asm add esp,8 \
    __asm popfd __asm popad __asm jmp dword ptr [screenOriginals+n*4] }
SCREEN_STUB(0) SCREEN_STUB(1) SCREEN_STUB(2) SCREEN_STUB(3) SCREEN_STUB(4) SCREEN_STUB(5)
#undef SCREEN_STUB
void InstallScreenHooks() {
    void* stubs[]={(void*)&ScreenStub0,(void*)&ScreenStub1,(void*)&ScreenStub2,
                   (void*)&ScreenStub3,(void*)&ScreenStub4,(void*)&ScreenStub5};
    static_assert(sizeof(stubs)/sizeof(stubs[0])==screenHookCount,"one stub per screen hook");
    FILE* f{};if(fopen_s(&f,"DeusExHRVR-camera.log","a"))f=nullptr;
    for(int i=0;i<screenHookCount;i++) {
        auto& h=screenHooks[i];auto target=reinterpret_cast<void*>(VA(h.address));
        const char* result="hooked";
        if(memcmp(target,h.bytes,h.length))result="skipped: unexpected bytes";
        else if(MH_CreateHook(target,stubs[i],&screenOriginals[i])!=MH_OK)result="skipped: create failed";
        else if(MH_EnableHook(target)!=MH_OK){MH_RemoveHook(target);result="skipped: enable failed";}
        if(f)fprintf(f,"screen hook %s %s at 0x%x: %s\n",h.name,h.event,unsigned(h.address),result);
    }
    if(f)fclose(f);
}
#endif
void Install() {
    base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    const unsigned char updateBytes[]={0x55,0x8b,0xec,0x83,0xe4,0xf0,0x83,0xec,0x34};
    const unsigned char sceneBytes[]={0x55,0x8b,0xec,0x83,0xe4,0xf0,0x83,0xec,0x64};
    bool supported=nt->FileHeader.TimeDateStamp==0x52840914 && nt->OptionalHeader.SizeOfImage==0x1c54000;
    supported=supported&&!memcmp(reinterpret_cast<void*>(VA(0x6a15c0)),updateBytes,sizeof(updateBytes))&&
        !memcmp(reinterpret_cast<void*>(VA(0x53a5b0)),sceneBytes,sizeof(sceneBytes));
    if(!supported)return; // Probe and other executable versions are never patched.
    auto init=MH_Initialize();if(init!=MH_OK&&init!=MH_ERROR_ALREADY_INITIALIZED)return;
    auto update=reinterpret_cast<void*>(VA(0x6a15c0)),scene=reinterpret_cast<void*>(VA(0x53a5b0));
    struct Hook {uintptr_t address;void* hook;void** original;const char* bytes;size_t length;};
    Hook hooks[]={
        {0x6a15c0,(void*)&UpdateHook,(void**)&originalUpdate,"\x55\x8b\xec\x83\xe4\xf0\x83\xec\x34",9},
        {0x53a5b0,(void*)&CreateHook,(void**)&originalCreate,"\x55\x8b\xec\x83\xe4\xf0\x83\xec\x64",9},
        {0x6a00f0,(void*)&WorldHook,(void**)&originalWorld,"\x8b\x49\x30\x8b\x01",5},
        {0x6a0100,(void*)&ViewHook,(void**)&originalView,"\x8b\x49\x30\x8b\x01",5},
        {0x6a0110,(void*)&ManagerHook,(void**)&originalManager,"\x8d\x81\xb0\x13\x00\x00\xc3",7},
        {0x546520,(void*)&DrawHook,(void**)&originalDraw,"\x55\x8b\xec\x83\xe4\xf0\x83\xec\x34",9},
        {0x51ebf0,(void*)&StereoHook,(void**)&originalStereo,"\x55\x8b\xec\x83\xe4\xf0\x81\xec\x1c\x01\x00\x00",12},
        {0x532c70,(void*)&PrimitiveHook,(void**)&originalPrimitive,"\x83\xec\x20\x53\x8b\x5c\x24\x28",8},
        {0x550930,(void*)&MatricesHook,(void**)&originalMatrices,"\x55\x8b\xec\x83\xe4\xf0\x81\xec\x84\x00\x00\x00",12}
        ,{0x545cf0,(void*)&UniformsHook,(void**)&originalUniforms,"\x55\x8b\xec\x83\xe4\xf0\x81\xec\x04\x02\x00\x00",12}
        ,{0x552130,(void*)&RenderStateHook,(void**)&originalRenderState,"\x56\x8b\xf1\x80\x7e\x19\x00",7}
        ,{0x986cd0,(void*)&VideoCreateHook,(void**)&originalVideoCreate,"\x33\xc0\x56\x8b\xf1",5}
        ,{0x986b40,(void*)&VideoDestroyHook,(void**)&originalVideoDestroy,"\x56\x8b\xf1\x57",4}
        ,{0x71bb20,(void*)&BillboardsHook,(void**)&originalBillboards,"\x55\x8b\xec\x83\xe4\xf0",6}
        ,{0x74f3e0,(void*)&WeaponMuzzleHook,(void**)&originalWeaponMuzzle,"\x53\x56\x8b\xf1\x8b\x46\x78",7}
        ,{0x750dc0,(void*)&WeaponAimHook,(void**)&originalWeaponAim,"\x55\x8b\xec\x83\xe4\xf0",6}
        ,{0x720930,(void*)&ActorDrawHook,(void**)&originalActorDraw,"\x56\x8b\xf1\x8b\x46\x08",6}
        ,{0x723190,(void*)&WeaponDrawHook,(void**)&originalWeaponDraw,"\x8b\x44\x24\x08\x56",5}
        ,{0x60c200,(void*)&SkeletonHook,(void**)&originalSkeleton,"\x55\x8b\xec\x83\xe4\xf0",6}
        ,{0x4899d0,(void*)&AttachmentHook,(void**)&originalAttachment,"\x55\x8b\xec\x83\xe4\xf0",6}
        ,{0x5b1a70,(void*)&BoundsHook,(void**)&originalBounds,"\x55\x8b\xec\x83\xe4\xf0",6}
        ,{0x689b40,(void*)&SenseUpdateHook,(void**)&originalSenseUpdate,"\x55\x8b\xec\x83\xe4\xf0",6}
        ,{0x69fc90,(void*)&PlayerWorldHook,(void**)&originalPlayerWorld,"\x8d\x41\x40\xc3",4}
        ,{0x4aeae0,(void*)&InputAxisHook,(void**)&originalInputAxis,"\x80\x7c\x24\x08\x00",5}
    };
    for(auto& h:hooks)if(memcmp((void*)VA(h.address),h.bytes,h.length))return;
    bool enabled=true;
    for(auto& h:hooks)if(MH_CreateHook((void*)VA(h.address),h.hook,h.original)!=MH_OK){enabled=false;break;}
    if(enabled) {
        for(auto& h:hooks)MH_QueueEnableHook((void*)VA(h.address));enabled=MH_ApplyQueued()==MH_OK;
    }
    if(!enabled)for(auto& h:hooks){MH_DisableHook((void*)VA(h.address));MH_RemoveHook((void*)VA(h.address));}
#if defined(_M_IX86)
    if(enabled)InstallScreenHooks(); // after, and independent of, the camera hooks
#endif
    wchar_t config[MAX_PATH]{};GetFullPathNameW(L"DeusExHRVR.ini",MAX_PATH,config,nullptr);
    wchar_t scaleText[32];GetPrivateProfileStringW(L"VR",L"WorldUnitsPerMetre",L"100",scaleText,32,config);
    float scale=static_cast<float>(_wtof(scaleText));if(std::isfinite(scale)&&scale>=10&&scale<=1000)worldScale=scale;
    lockVerticalCamera=GetPrivateProfileIntW(L"VR",L"LockVerticalCamera",0,config)!=0;
    levelRecenter=GetPrivateProfileIntW(L"VR",L"LevelRecenter",1,config)!=0;
    levelMenu=GetPrivateProfileIntW(L"VR",L"LevelMenu",1,config)!=0;
    yawOnlyCamera=GetPrivateProfileIntW(L"VR",L"YawOnlyCamera",0,config)!=0;
    bobTrace=GetPrivateProfileIntW(L"VR",L"BobTrace",0,config)!=0;
    // Optional stutter fix from HRDCfix (github.com/imring/HRDCfix, MIT).
    // 0x54c800 spin-waits on Sleep(1) while a busy flag is set. The game never
    // calls timeBeginPeriod, so each Sleep(1) can last a whole timer tick
    // (~15.6 ms, nearly two frames at 120 Hz); Sleep(0) only yields. Bytes
    // 13-16 hold Sleep's import address, relocated each launch, so they are
    // not compared. An already-patched byte simply doesn't match.
    if(GetPrivateProfileIntW(L"VR",L"SleepFix",0,config)!=0) {
        static const unsigned char spin[]={0x56,0x8b,0xf1,0x8a,0x46,0x08,0x84,0xc0,0x74,0x13,0x57,0x8b,0x3d,0,0,0,0,0x6a,0x01};
        auto code=reinterpret_cast<unsigned char*>(VA(0x54c800));
        bool match=true;
        for(size_t i=0;i<sizeof(spin);i++)if((i<13||i>16)&&code[i]!=spin[i]){match=false;break;}
        DWORD old{};
        if(match&&VirtualProtect(code+0x12,1,PAGE_EXECUTE_READWRITE,&old)) {
            code[0x12]=0x00; // push 1 -> push 0
            VirtualProtect(code+0x12,1,old,&old);
            FlushInstructionCache(GetCurrentProcess(),code+0x12,1);
            sleepFix=true;
        }
    }
    auto readInt=[&](const wchar_t* key,int fallback,int hi){return std::clamp(static_cast<int>(GetPrivateProfileIntW(L"VR",key,fallback,config)),0,hi);};
    auto readFloat=[&](const wchar_t* key,const wchar_t* fallback,float lo,float hi,float& target) {
        wchar_t text[32]{};GetPrivateProfileStringW(L"VR",key,fallback,text,32,config);
        float v=static_cast<float>(_wtof(text));if(std::isfinite(v)&&v>=lo&&v<=hi)target=v;
    };
    yawSwing.a.windowMs[0]=readInt(L"HeadSwayYawMs",0,2000);
    yawSwing.b.windowMs[0]=readInt(L"HeadSwayYawMs2",0,2000);
    readFloat(L"HeadSwayYawLimit",L"1.5",0,20,yawSwing.a.limit[0]);
    yawSwing.b.limit[0]=yawSwing.a.limit[0];
    yawSwing.a.enabled=yawSwing.a.windowMs[0]>0;yawSwing.b.enabled=yawSwing.b.windowMs[0]>0;
    stanceHold.enabled=GetPrivateProfileIntW(L"VR",L"StanceHold",0,config)!=0;
    readFloat(L"StanceHoldTrigger",L"60",5,400,stanceHold.trigger);
    swayHold.windowMs=readInt(L"SideSwayHoldMs",0,2000);
    stanceHold.Reset();
    motionControls=DirectionConfig::MotionEnabled(config);
    experimentalMotionControls=motionControls && GetPrivateProfileIntW(L"VR",L"ExperimentalMotionControls",0,config)!=0;
    controllerHideArms=GetPrivateProfileIntW(L"VR",L"ControllerHideArms",1,config)!=0;
    interactionAim=DirectionConfig::Read(config,L"InteractionAim");
    movementDirection=DirectionConfig::Read(config,L"MovementDirection");
    immersiveScope=GetPrivateProfileIntW(L"VR",L"ImmersiveScope",0,config)!=0;
    GetPrivateProfileStringW(L"VR",L"ScopeMagnification",L"4",scaleText,32,config);
    {float value=static_cast<float>(_wtof(scaleText));if(std::isfinite(value)&&value>=1&&value<=12)scopeMagnification=value;}
    GetPrivateProfileStringW(L"VR",L"ControllerMuzzleForwardMetres",L"0.25",scaleText,32,config);
    float muzzleForward=static_cast<float>(_wtof(scaleText));
    if(std::isfinite(muzzleForward) && muzzleForward>=0 && muzzleForward<=1)controllerMuzzleForward=muzzleForward;
    // Luma port: load the native shader-swap table from beside the companion.
    // Gated by [Luma] Enable (default on) so the feature is opt-out. The
    // table lives at <game>/DeusExHRVR/shaders/dxhr/table.csv; a missing or
    // empty table leaves the swap inactive and NoteBound is a no-op.
    bool lumaEnable=GetPrivateProfileIntW(L"Luma",L"Enable",1,config)!=0;
    if(lumaEnable) {
        // config holds the absolute path to DeusExHRVR.ini in the game folder;
        // its directory is the game root the shader table is relative to.
        std::filesystem::path configPath=config;
        shaderSwap.Load(configPath.parent_path());
        // Phase 2: substitution defaults off; opt in via ini. F12 can toggle
        // it live regardless of this starting state.
        bool sub=GetPrivateProfileIntW(L"Luma",L"SubstituteShaders",0,config)!=0;
        shaderSwap.SetSubstitutionEnabled(sub);
        // Phase 3: injected-pass enables. All default off until tested.
        // XeGTAO experiment withdrawn: retain the game's native AO.
        lumaPasses.SetXeGTAOEnabled(false);
        lumaPasses.SetSMAAEnabled(GetPrivateProfileIntW(L"Luma",L"SMAAEnable",0,config)!=0);
        lumaPasses.SetModulateLightingEnabled(GetPrivateProfileIntW(L"Luma",L"ModulateLightingEnable",0,config)!=0);
        // LumaSettings cbuffer values. Defaults match Luma's DXHR main.cpp
        // (lines 1589-1599): "not vanilla like" tuned values. Override via ini.
        // These take effect on the next Bind() (lumaSettingsCB is init'd later
        // in SetShaderSwapDevice, but setting values now is fine — SetDefaults
        // runs first, then these override before any bind happens).
        auto readFloat=[&](const wchar_t* key,float def)->float{
            wchar_t buf[32]{};
            if (!GetPrivateProfileStringW(L"Luma",key,L"",buf,32,config)) return def;
            wchar_t* end{};
            float v=wcstof(buf,&end);
            while (end && (*end==L' ' || *end==L'\t')) ++end;
            return end!=buf && end && !*end && std::isfinite(v) && v>=0.0f && v<=10.0f ? v : def;
        };
        auto& gs=lumaSettingsCB.Get().GameSettings;
        gs.BloomIntensity=readFloat(L"BloomIntensity",0.8f);
        gs.FogIntensity=readFloat(L"FogIntensity",0.0f);
        gs.ColorGradingIntensity=readFloat(L"ColorGradingIntensity",1.0f);
        gs.DesaturationIntensity=readFloat(L"DesaturationIntensity",0.333f);
        gs.AmbientLightingIntensity=readFloat(L"AmbientLightingIntensity",0.8f);
        gs.EmissiveIntensity=readFloat(L"EmissiveIntensity",0.667f);
        gs.HDRBoostIntensity=readFloat(L"HDRBoostIntensity",1.0f);
        gs.LightingColor[0]=readFloat(L"LightingRed",1.0f);
        gs.LightingColor[1]=readFloat(L"LightingGreen",1.0f);
        gs.LightingColor[2]=readFloat(L"LightingBlue",1.0f);
        gs.AmbientLightColor[0]=readFloat(L"AmbientRed",1.0f);
        gs.AmbientLightColor[1]=readFloat(L"AmbientGreen",1.0f);
        gs.AmbientLightColor[2]=readFloat(L"AmbientBlue",1.0f);
        lumaSettingsCB.MarkDirty();
        // Phase 4: overlap dedup. Default on — skip Luma substitution for
        // shaders DXHRVR already corrects per-eye (projected light/shadow via
        // F3, identified by EffectShader::UsesCentreViewMatrix).
        shaderSwap.SetDedupWithDXHRVR(GetPrivateProfileIntW(L"Luma",L"DedupWithDXHRVR",1,config)!=0);
        // Always retain native ambient occlusion.
        {
            shaderSwap.AddSkipHash(0xD44718C4u); // GenerateAmbientOcclusion (DC)
            shaderSwap.AddSkipHash(0x7A054979u); // GenerateAmbientOcclusion (OG)
        }
    }
    FILE* f{};if(!fopen_s(&f,"DeusExHRVR-camera.log","a")) {
        fprintf(f,"Camera hooks base=%p enabled=%d unitsPerMetre=%g lockVerticalCamera=%d levelRecenter=%d levelMenu=%d yawOnlyCamera=%d stanceHold=%d/%g/%g yawMs=%d/%d yawLimit=%g sideSwayHold=%d sleepFix=%d F6=toggle F9=recenter\n",reinterpret_cast<void*>(base),enabled,worldScale,lockVerticalCamera,levelRecenter,levelMenu,yawOnlyCamera,stanceHold.enabled,stanceHold.trigger,stanceHold.rate,yawSwing.a.windowMs[0],yawSwing.b.windowMs[0],yawSwing.a.limit[0],swayHold.windowMs,int(sleepFix));fclose(f);
    }
}
}
Transport::RenderInfo OnPresent(uint64_t frame,bool capture) {
    static std::once_flag once;std::call_once(once,Install);
    frameId=frame;
    // Luma port: reset per-frame scheduling flags at frame start (mirrors
    // Luma resetting game_device_data on frame boundary).
    if(lumaPasses.Loaded()) lumaPasses.OnFrameStart();
    // Luma port: update LumaSettings per-frame (frame index + resolution).
    // Resolution comes from HeadsetDisplay (queried at startup). Luma's
    // main.cpp does this at line 528-529.
    if(lumaSettingsCB.Initialized()) {
        lumaSettingsCB.SetFrameIndex(static_cast<uint32_t>(frame));
        // Use headset eye resolution as the output resolution (each eye is
        // rendered at this size). HeadsetDisplay::Active() + Settings hold it.
        if(HeadsetDisplay::Active()) {
            auto s=HeadsetDisplay::GetSettings();
            lumaSettingsCB.SetOutputResolution(static_cast<float>(s.width),
                                               static_cast<float>(s.height));
        }
    }
    // Detailed camera dumps perform synchronous file IO. Never schedule them
    // periodically on the render thread; F8 is the explicit diagnostic request.
    budget=capture?32:0;
    std::lock_guard lock(stateMutex);
    // Menus/videos can keep presenting while simulation (and camera updates)
    // is paused. Refresh here as well, before the following frame is built.
    RefreshScreenMode();if(screenReasons)current.active=false;
    if(screenReasons || gameMenuOpen || !requested || recenterRequested)calibration.Cancel();
    LevelFrozenView(frame);
    if(effectsCapture){SaveEffects(frame);effectCount=0;shaderTrace.End();}effectsCapture=capture;
    if(capture)shaderTrace.Begin(frame+1);
    auto completed=pairInfo;pairInfo={};
    scenes.Complete(frame);
    lastWorld={};
    if(completed.mode==1 && completed.eyeMask!=3)completed.mode=2;
    static uint32_t lastMode=99;
    if(completed.mode!=lastMode || capture) {
        FILE* f{};if(!fopen_s(&f,"DeusExHRVR-camera.log","a")) {
            fprintf(f,"nativePair frame=%llu mode=%u eyeMask=%u pose=%llu active=%d taggedScenes=%llu stereoCalls=%llu\n",frame,completed.mode,completed.eyeMask,completed.tracking.id,current.active,taggedScenes,stereoCalls);fclose(f);
        }
        lastMode=completed.mode;
    }
    if(capture) {
        SaveTrace(frame);
        FILE* motion{};if(!fopen_s(&motion,"DeusExHRVR-camera.log","a")) {
            fprintf(motion,"controller enabled=%d valid=%u active=%d weapon=%p instance=%p modelDraws=%llu muzzleQueries=%llu aimQueries=%llu hiddenArms=%llu owner=%p attachments=%llu bounds=%llu\n",
                experimentalMotionControls,current.tracking.rightController.valid,weaponPose.active,weaponPose.weapon,weaponPose.instance,
                controllerDraws.load(),controllerMuzzles.load(),controllerAimQueries.load(),controllerHiddenArms.load(),weaponPose.owner,
                controllerAttachments.load(),controllerBounds.load());
            Matrix(motion,"controllerMuzzle",weaponPose.muzzle.m);
            fprintf(motion,"immersiveScope enabled=%d active=%d owned=%d zoom=%g\n",immersiveScope,immersiveScopeActive,autoScopeOwned,scopeMagnification);
            fprintf(motion,"directions interaction=%s movement=%s interactionQueries=%llu movementAxes=%llu motionControls=%d inputIndex=%u actions=%p/%p lastInput=%g,%g lastOutput=%g,%g heading=%g\n",
                DirectionConfig::Name(interactionAim),DirectionConfig::Name(movementDirection),
                interactionQueries.load(),movementAxes.load(),motionControls,current.inputIndex,walkAction.load(),strafeAction.load(),
                lastMoveInput[0],lastMoveInput[1],lastMoveOutput[0],lastMoveOutput[1],lastMoveHeading);
            {std::lock_guard lock(attachmentTraceMutex);for(const auto& t:attachmentTrace)if(t.queries)
                fprintf(motion,"attachment caller=%08x bone=%d owner=%d queries=%llu corrected=%llu\n",
                    unsigned(t.caller),t.bone,t.owner,t.queries,t.corrected);}
            fclose(motion);
        }
        FILE* f{};if(!fopen_s(&f,"DeusExHRVR-camera.log","a")){fprintf(f,"HUD plane draws=%llu matrices=%llu frame=%llu trackingReadContentions=%llu rejectedSamples=%llu\n",hudDraws,hudMatrices,frame,trackingReader.reused,trackingReader.rejected);fclose(f);}
        // Luma port: fold shader-swap match stats into the capture log so the
        // dry run can be confirmed without a separate file read.
        if(shaderSwap.Active()) {
            auto ss=shaderSwap.GetStats();
            FILE* f{};if(!fopen_s(&f,"DeusExHRVR-camera.log","a")){fprintf(f,"shaderSwap notes=%llu matches=%llu unique=%llu substitutions=%llu enabled=%d frame=%llu\n",ss.notes,ss.matches,ss.uniqueMatches,ss.substitutions,int(shaderSwap.SubstitutionEnabled()),frame);fclose(f);}
        }
        // Phase 3: injected-pass counts.
        if(lumaPasses.Loaded()) {
            auto ps=lumaPasses.GetStats();
            FILE* f{};if(!fopen_s(&f,"DeusExHRVR-camera.log","a")){fprintf(f,"lumaPasses xegtao=%llu smaa=%llu modulate=%llu enabled=xg%d/smaa%d/mod%d frame=%llu\n",ps.xegtaoRuns,ps.smaaRuns,ps.modulateRuns,int(lumaPasses.XeGTAOEnabled()),int(lumaPasses.SMAAEnabled()),int(lumaPasses.ModulateLightingEnabled()),frame);fclose(f);}
        }
    }
    bool f6=(GetAsyncKeyState(VK_F6)&0x8000)!=0,f9=(GetAsyncKeyState(VK_F9)&0x8000)!=0;
    if(capture) {
        FILE* f{};if(!fopen_s(&f,"DeusExHRVR-effects.log","a")) {
            fprintf(f,"sprites frame=%llu flaresRemoved=%llu skyFix=%d skyCorrections=%llu renderPitchLock=%d screenReasons=%u\n",frame,removedFlareSprites,skyFix,skyCorrections,lockVerticalCamera,screenReasons);fclose(f);
        }
    }
    bool f7=(GetAsyncKeyState(VK_F7)&0x8000)!=0;
    if(f7&&!f7Down){effectsFix=!effectsFix;FILE* f{};if(!fopen_s(&f,"DeusExHRVR-effects.log","a")){fprintf(f,"effectsFix=%d frame=%llu\n",effectsFix,frame);fclose(f);}}f7Down=f7;
    bool f4=(GetAsyncKeyState(VK_F4)&0x8000)!=0;
    if(f4&&!f4Down){eyeViewFix=!eyeViewFix;FILE* f{};if(!fopen_s(&f,"DeusExHRVR-effects.log","a")){fprintf(f,"eyeViewFix=%d frame=%llu\n",eyeViewFix,frame);fclose(f);}}f4Down=f4;
    bool f3=(GetAsyncKeyState(VK_F3)&0x8000)!=0;
    bool f11=(GetAsyncKeyState(VK_F11)&0x8000)!=0;
    if(f11&&!f11Down){skyFix=!skyFix;FILE* f{};if(!fopen_s(&f,"DeusExHRVR-effects.log","a")){fprintf(f,"skyFix=%d frame=%llu\n",skyFix,frame);fclose(f);}}f11Down=f11;
    if(f3&&!f3Down){instanceFix=!instanceFix;FILE* f{};if(!fopen_s(&f,"DeusExHRVR-effects.log","a")){fprintf(f,"instanceFix=%d frame=%llu\n",instanceFix,frame);fclose(f);}}f3Down=f3;
    if(capture){FILE* f{};if(!fopen_s(&f,"DeusExHRVR-effects.log","a")){fprintf(f,"instanceFix=%d correctedDraws=%llu frame=%llu\n",instanceFix,instanceCorrections,frame);fclose(f);}}
    // Luma port: F12 toggles live shader substitution (off by default; opt in
    // via [Luma] SubstituteShaders=1). Lets you A/B the swap in-headset without
    // restarting. Matches the existing F3/F4/F7 toggle pattern.
    bool f12=(GetAsyncKeyState(VK_F12)&0x8000)!=0;
    if(f12&&!f12Down && !(GetAsyncKeyState(VK_CONTROL)&0x8000)){
        bool on=ToggleShaderSubstitution();
        FILE* f{};if(!fopen_s(&f,"DeusExHRVR-effects.log","a")){fprintf(f,"lumaSubstitute=%d frame=%llu\n",on,frame);fclose(f);}
    }f12Down=f12;
    if(f6&&!f6Down){requested=!requested;referenceValid=false;current.active=false;}
    if(f9&&!f9Down)recenterRequested=true;
    if((f6&&!f6Down)||(f9&&!f9Down)) {
        FILE* f{};if(!fopen_s(&f,"DeusExHRVR-camera.log","a")){fprintf(f,"tracking requested=%d recenter=%d frame=%llu\n",requested,recenterRequested,frame);fclose(f);}
    }
    f6Down=f6;f9Down=f9;return completed;
}
void SetChannel(Transport::Header* header){std::lock_guard lock(stateMutex);channel=header;trackingReader={};if(!header){current.active=false;referenceValid=false;}}
// Local patch (snap turn): the game's own camera heading while full tracked VR
// is showing gameplay. False in menus, terminals, scope and other screen modes.
// Bits 32 (in-game menu) and 64 (e-reader, news reader) select [ScreenButtons] only;
// the display stays tracked.
unsigned CurrentScreenReasons(){std::lock_guard lock(stateMutex);return screenReasons|(gameMenuOpen?32u:0u)|(readerOpen?64u:0u);}
bool SnapTurnView(float& yaw) {
    std::lock_guard lock(stateMutex);
    auto now=GetTickCount64();
    if(!current.active || screenReasons || gameMenuOpen || !current.playerInstance || now<current.tracking.tick || now-current.tracking.tick>=250)return false;
    yaw=std::atan2(current.originalWorld.m[9],current.originalWorld.m[8]);
    return std::isfinite(yaw);
}
// Local patch (snap turn): the game's own quickbar auto-hide, g_quickBarAutoHide,
// the setting the tilde key toggles. The registration at 0xa68190 passes the
// value's address (0x01c79eb8) to the setting's constructor, which keeps it at
// +0x104 of the setting object (0x01c7bb60) and writes the default there; the
// game's own setter writes that one byte. All of it is checked before touching
// anything. Returns the value found (before setting it, if asked), or -1 when
// the layout doesn't match.
int QuickBarAutoHide(bool set) {
    if(!base)return -1;
    auto code=reinterpret_cast<const unsigned char*>(VA(0xa68190));
    auto dword=[&](size_t at){uint32_t v;memcpy(&v,code+at,4);return uintptr_t(v);};
    static const unsigned char pushes[]={0x6a,0x01,0x6a,0x00,0x6a,0x00,0x68}; // push 1; push 0; push 0; push value
    auto value=VA(0x01c79eb8),object=VA(0x01c7bb60),name=VA(0xaa4004);
    if(memcmp(code,pushes,sizeof(pushes)) || dword(7)!=value || code[11]!=0x68 || dword(12)!=name ||
        code[16]!=0xb9 || dword(17)!=object || strcmp(reinterpret_cast<const char*>(name),"g_quickBarAutoHide") ||
        *reinterpret_cast<const uintptr_t*>(object+0x104)!=value)return -1;
    auto flag=reinterpret_cast<volatile unsigned char*>(value);
    int found=*flag;
    if(set && !found)*flag=1;
    return found;
}

void SetShaderSwapDevice(ID3D11Device* device){
    shaderSwap.SetDevice(device);
    // Phase 3: also init the injected-pass subsystem now that the device is
    // available. Loads the injected-pass .cso blobs from the same compiled/
    // dir the shader swap uses. Missing blobs leave the corresponding pass
    // disabled; the first call to OnDraw is a no-op until enabled via ini.
    if(device) {
        // The compiled/ dir is <game>/DeusExHRVR/shaders/dxhr/compiled/, which
        // is shaderSwap's shaderDir + "compiled". We reconstruct it from the
        // exe path (same logic NativeTransport uses for the host exe).
        wchar_t module[MAX_PATH]{};HMODULE self{};
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&SetShaderSwapDevice),&self);
        GetModuleFileNameW(self,module,MAX_PATH);
        wchar_t* slash=wcsrchr(module,L'\\');
        if(slash) {
            slash[1]=0;
            std::filesystem::path compiled=std::filesystem::path(module)/L"DeusExHRVR"/L"shaders"/L"dxhr"/L"compiled";
            lumaPasses.Load(compiled, device);
        }
        // LumaSettings cbuffer: init + set defaults + cross-link so both
        // ShaderSwap (Phase 2) and LumaPasses (Phase 3) can bind it at b13
        // before their shaders run.
        lumaSettingsCB.Init(device);
        shaderSwap.SetLumaSettingsCB(&lumaSettingsCB);
        lumaPasses.SetLumaSettingsCB(&lumaSettingsCB);
    }
}
bool ImmersiveScopeButton(){return GetTickCount64()<scopePulseUntil.load();}
bool ToggleShaderSubstitution(){bool on=!shaderSwap.SubstitutionEnabled();shaderSwap.SetSubstitutionEnabled(on);return on;}
}

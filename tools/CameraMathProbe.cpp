#include "CameraMath.h"
#include "DirectionConfig.h"
#include "NativeBounds.h"
#include "SceneCache.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
void Check(bool value,const char* name){if(!value){fprintf(stderr,"FAIL %s\n",name);std::exit(1);}}
bool Near(float a,float b){return std::abs(a-b)<0.0001f;}
#include "GamepadChecks.h"
#include "ImmersiveScope.h"
#include "WeaponCalibrationChecks.h"
int main(int argc,char** argv) {
    CheckGamepadMapping();
    CheckWeaponCalibration();
    {
        ImmersiveScope::Gesture gesture;Transport::Tracking t{};
        t.valid=1;t.rightController.valid=1;t.head.orientation.w=t.rightController.aim.orientation.w=1;
        t.rightController.aim.position={.06f,-.08f,-.18f};t.tick=100;
        Check(!gesture.Update(t,true,100),"scope gesture requires dwell");
        t.tick=260;Check(gesture.Update(t,true,260),"scope enters with aligned controller near eye");
        t.rightController.aim.position.z=-.34f;t.tick=280;
        Check(gesture.Update(t,true,280),"scope exit hysteresis prevents threshold chatter");
        t.rightController.aim.position.z=-.5f;t.tick=300;
        Check(!gesture.Update(t,true,300),"scope exits when rifle lowered");
        t.rightController.aim.position.z=-.18f;t.tick=400;gesture.Update(t,true,400);
        Check(!gesture.Update(t,true,700),"stale tracking cannot activate scope");
        for(auto& eye:t.eyes){eye.pose.orientation.w=1;eye.left=-.7f;eye.right=.8f;eye.up=.75f;eye.down=-.65f;}
        t.head.position={1,2,3};t.eyes[0].pose.position={.968f,2,3};t.eyes[1].pose.position={1.032f,2,3};
        auto zoomed=ImmersiveScope::Zoom(t,4);
        for(unsigned eye=0;eye<2;++eye){auto optical=CameraMath::EyeWorld(zoomed,eye,300);
            Check(Near(optical.m[12],0)&&Near(optical.m[13],0)&&Near(optical.m[14],0),"scope eyes share firing origin");}
        for(unsigned eye=0;eye<2;++eye){
            CameraMath::Matrix depth{};depth.m[10]=1;depth.m[14]=-.1f;
            auto projection=CameraMath::EyeProjection(depth,zoomed,eye,300);
            auto hud=CameraMath::HudClipTransform(zoomed,eye);
            for(float distance:{300.f,3000.f,30000.f})for(int axis:{0,1})
                Check(Near((projection.m[8+axis]*distance+projection.m[12+axis])/distance,hud.m[12+axis]/hud.m[15]),"scope reticle follows firing ray at every distance in both eyes");
        }
        Check(Near(t.eyes[0].pose.position.x,.968f),"scope does not change physical eye poses");
        Check(Near(std::tan(zoomed.eyes[0].left)*4,std::tan(t.eyes[0].left)),"scope scales asymmetric frustum tangents");
        Check(t.eyes[0].left==-.7f,"scope leaves compositor frustum unchanged");
    }
    {
        SceneCache<uint64_t> scenes;
        for(uintptr_t i=1;i<=5000;i++)scenes.Store(reinterpret_cast<void*>(i),42,10);
        for(uintptr_t i=1;i<=5000;i++)Check(scenes.Find(reinterpret_cast<void*>(i))==42,"large native pair retains every camera tag");
        scenes.Store(reinterpret_cast<void*>(1),43,11);
        scenes.Complete(11);
        Check(scenes.Size()==5000,"recent completed scenes retained");
        scenes.Complete(12);
        Check(scenes.Size()==1 && scenes.Find(reinterpret_cast<void*>(1))==43,"old scenes expire without erasing reused address");
        scenes.Store(reinterpret_cast<void*>(2),44,14);
        scenes.Complete(13);
        Check(scenes.Size()==1 && scenes.Find(reinterpret_cast<void*>(2))==44,"future queued scene survives cleanup");
        scenes.Erase(reinterpret_cast<void*>(2));
        Check(!scenes.Find(reinterpret_cast<void*>(2)),"untracked reuse removes old tag");
    }
    if(argc==2) {
        // Replay a private camera-history CSV through the production cache.
        // The bounded recording can begin in the middle of a frame.
        std::ifstream input(argv[1]);Check(bool(input),"open camera history");
        std::string line;std::getline(input,line);
        SceneCache<uint64_t> scenes;uint64_t first=0,previous=0,draws=0,recovered=0;
        while(std::getline(input,line)) {
            std::istringstream row(line);std::string columns[7];
            for(auto& c:columns)Check(bool(std::getline(row,c,',')),"parse camera history");
            uint64_t frame=std::stoull(columns[0]),pose=std::stoull(columns[2]);
            auto scene=reinterpret_cast<void*>(static_cast<uintptr_t>(std::stoull(columns[3])));
            int event=std::stoi(columns[4]),reason=std::stoi(columns[5]);
            if(!first)first=frame;
            if(previous && previous!=frame)scenes.Complete(previous);
            previous=frame;
            if(event==1) {
                if(reason==5 || reason==6 || reason==7)scenes.Store(scene,pose,frame);else scenes.Erase(scene);
            } else if(event==2 && frame!=first) {
                Check(scenes.Find(scene)!=0,"recorded world draw retains its camera");
                if(pose)Check(scenes.Find(scene)==pose,"recorded draw retains the correct pose");
                ++draws;if(reason==1)++recovered;
            }
        }
        Check(draws>0,"camera replay contains draws");
        printf("PASS camera history replay: %llu draws, %llu previously missing camera tags recovered\n",draws,recovered);
    }
    {
        Transport::Header mailbox{};Transport::TrackingReader reader;Transport::Tracking sample{},result{};
        sample.id=7;sample.tick=1000;sample.valid=1;Transport::WriteTracking(&mailbox,sample);
        Check(reader.Read(&mailbox,result,1000) && result.id==7,"read fresh tracking");
        InterlockedExchange(&mailbox.trackingLock,1);
        Check(reader.Read(&mailbox,result,1011) && result.id==7,"writer contention preserves fresh camera sample");
        Check(!reader.Read(&mailbox,result,1250),"contended sample expires after 250ms");
        InterlockedExchange(&mailbox.trackingLock,0);
        sample.id=8;sample.tick=1250;Transport::WriteTracking(&mailbox,sample);
        Check(reader.Read(&mailbox,result,1251) && result.id==8,"fresh sample replaces contended cache");
        sample.valid=0;Transport::WriteTracking(&mailbox,sample);
        Check(!reader.Read(&mailbox,result,1252),"runtime tracking loss invalidates cached sample");
        Check(!reader.Read(nullptr,result,1253) && !reader.latest.valid,"closed channel clears sample cache");
    }
    using namespace CameraMath;
    {
        struct Volume {float data[20]{};uint32_t type{},canary=0x12345678;} v;
        static_assert(offsetof(Volume,type)==0x50);
        v.data[0]=100;v.data[1]=-500;v.data[2]=90;v.data[3]=25;
        Check(NativeBounds::ExpandWeapon(&v,1200) && v.type==0 && v.canary==0x12345678,
            "controlled gun uses finite sphere accepted by loading cell callbacks");
        Check(v.data[0]==100 && v.data[1]==-500 && v.data[2]==90 && v.data[3]==1225,
            "expanded sphere preserves world centre and includes native bounds plus controller reach");
        v={};v.type=5;v.data[0]=3;v.data[5]=4;v.data[10]=12;v.data[12]=99;
        Check(NativeBounds::ExpandWeapon(&v,1200) && v.type==0 && v.data[0]==99 && v.data[3]>=1213,
            "native box converts to a containing finite sphere");
        v.type=7;auto old=v;
        Check(!NativeBounds::ExpandWeapon(&v,1200) && !std::memcmp(&v,&old,sizeof(v)),
            "unknown bounds retain original game data");
    }
    {
        using namespace DirectionConfig;
        Check(Parse(L"headset",Source::Mouse)==Source::Headset && Parse(L"Controller",Source::Mouse)==Source::Controller,
            "direction settings accept readable case-insensitive device names");
        Check(Parse(L"invalid",Source::Mouse)==Source::Mouse,"invalid direction safely retains mouse mode");
        Matrix level{{1,0,0,0, 0,0,-1,0, 0,1,0,0, 10,20,30,1}};
        for(float nativeYaw:{-2.f,0.f,1.5f})for(float turn:{-1.2f,0.f,1.4f})for(float pitch:{-1.5707963f,-.8f,0.f,.9f,1.5707963f}) {
            auto base=Multiply(level,Rotation({0,0,std::sin(nativeYaw/2),std::cos(nativeYaw/2)}));
            auto yawed=Multiply(base,Rotation({0,0,std::sin(turn/2),std::cos(turn/2)}));
            auto aimed=Multiply(Rotation({std::sin(pitch/2),0,0,std::cos(pitch/2)}),yawed);
            aimed.m[12]+=200;aimed.m[14]+=150;
            auto move=HorizontalDirection(aimed,base);
            Check(Near(move.m[10],0) && Near(std::hypot(move.m[8],move.m[9]),1),"movement remains horizontal and normalized through vertical aim");
            Check(Near(move.m[8],yawed.m[8]) && Near(move.m[9],yawed.m[9]),"head/controller pitch does not alter walking heading");
            Check(Near(HeadingDelta(base,aimed),turn),"walking yaw delta preserves native heading convention across turns");
            for(auto input:{MovementAxes{0,1},MovementAxes{1,0},MovementAxes{0,-1},MovementAxes{-.4f,.6f}}) {
                auto axes=ReorientMovement(input.strafe,input.walk,base,aimed);
                Check(Near(std::hypot(axes.strafe,axes.walk),std::hypot(input.strafe,input.walk)),"walking remap preserves analog speed");
                // Native input angle is atan2(strafe,walk), whose yaw sense
                // opposes ordinary Cartesian rotation of an (x,y) vector.
                float expected=std::atan2(input.strafe,input.walk)+turn;
                Check(Near(std::remainder(std::atan2(axes.strafe,axes.walk)-expected,6.28318530718f),0),
                    "native movement angle adds selected heading instead of mirroring it");
                auto restored=ReorientMovement(axes.strafe,axes.walk,aimed,base);
                Check(Near(restored.strafe,input.strafe) && Near(restored.walk,input.walk),"movement heading remap is reversible");
            }
            for(int j=12;j<15;j++)Check(Near(move.m[j],base.m[j]),"walking reference position stays at native player camera");
        }
        Matrix left{{0,1,0,0, 0,0,-1,0, -1,0,0,0, 0,0,0,1}};
        auto axes=ReorientMovement(0,1,level,left);
        Check(Near(axes.strafe,1) && Near(axes.walk,0),"regression: native north plus headset west uses positive quarter-turn input");
        axes=ReorientMovement(1,0,level,left);
        Check(Near(axes.strafe,0) && Near(axes.walk,-1),"quarter-turn remaps the strafe axis with the opposite cross term");
        Matrix right{{0,-1,0,0, 0,0,-1,0, 1,0,0,0, 0,0,0,1}};
        axes=ReorientMovement(0,1,level,right);
        Check(Near(axes.strafe,-1) && Near(axes.walk,0),"east and west heading corrections are opposite");
    }
    {
        Matrix base{{1,0,0,0, 0,0,-1,0, 0,1,0,0, 100,200,300,1}};
        Pose reference{{0,0,0,1},{0,0,0}};
        for(float yaw:{-.7f,0.f,.8f}) {
            Pose aim{{0,std::sin(yaw/2),0,std::cos(yaw/2)},{.3f,-.2f,-.4f}};
            auto hand=HeadWorld(base,reference,aim,300);
            auto muzzle=ControllerMuzzle(base,reference,aim,300,.25f);
            auto firing=FiringFromMuzzle(muzzle);
            for(int j=0;j<3;j++) {
                Check(Near(-muzzle.m[4+j],hand.m[8+j]),"native -Y muzzle direction follows controller aim");
                Check(Near(muzzle.m[8+j],-hand.m[4+j]),"gun top follows controller up, not down");
                Check(Near(firing.m[8+j],hand.m[8+j]),"player raycast +Z aims along the visible gun barrel");
                Check(Near(firing.m[12+j],muzzle.m[12+j]),"player shot starts at the controlled muzzle");
                Check(Near(muzzle.m[12+j],hand.m[12+j]+hand.m[8+j]*75),"muzzle offset uses game world scale");
            }
            auto native=base;auto delta=Multiply(InverseRigid(native),muzzle);
            auto visible=Multiply(native,delta);
            for(int i=0;i<16;i++)Check(Near(visible.m[i],muzzle.m[i]),"visual muzzle and native firing query share one target transform");
            auto bone=base;bone.m[12]+=15;bone.m[14]+=20;
            auto nativeRelative=Multiply(bone,InverseRigid(native));
            auto controlledRelative=Multiply(Multiply(bone,delta),InverseRigid(muzzle));
            for(int i=0;i<16;i++)Check(std::abs(nativeRelative.m[i]-controlledRelative.m[i])<.001f,"weapon animation is preserved relative to its muzzle");
            auto expectedBone=Multiply(bone,delta);
            for(float nativeW:{0.f,1.f}) {
                auto skin=bone;skin.m[15]=nativeW;
                auto moved=MoveSkinMatrix(skin,delta);
                for(int i=0;i<15;i++)Check(Near(moved.m[i],expectedBone.m[i]),"native zero-w skinning bones retain controller translation");
                Check(moved.m[15]==nativeW,"native skinning padding is preserved");
            }
        }
    }
    {
        Matrix level{{1,0,0,0, 0,0,-1,0, 0,1,0,0, 10,20,30,1}};
        Pose neutral{{0,0,0,1},{0,0,0}};
        for(float yaw:{-2.f,0.f,1.3f})for(float pitch:{-1.55f,-.4f,0.f,.7f,1.55f}) {
            auto base=Multiply(level,Rotation({0,0,std::sin(yaw/2),std::cos(yaw/2)}));
            auto aiming=Multiply(Rotation({std::sin(pitch/2),0,0,std::cos(pitch/2)}),base);
            auto savedAim=aiming;
            auto camera=WithoutLookPitch(aiming);
            Check(!memcmp(&aiming,&savedAim,sizeof(aiming)),"camera lock leaves native gun aiming matrix unchanged");
            Check(Near(aiming.m[10],std::sin(pitch)),"native gun still aims up and down");
            for(int i=0;i<16;i++)Check(Near(camera.m[i],base.m[i]),"camera lock preserves yaw and position across near-vertical native aim");
            for(float headPitch:{-.6f,0.f,.5f}) {
                auto head=neutral;head.orientation={std::sin(headPitch/2),0,0,std::cos(headPitch/2)};
                auto tracked=HeadWorld(camera,neutral,head,300);
                Check(Near(tracked.m[10],std::sin(headPitch)),"headset pitch stays independent of native gun pitch");
            }
        }
    }
    Matrix game=Rotation({0,0,0,1});game.m[12]=10;game.m[13]=20;game.m[14]=30;
    Pose reference{{0,0,0,1},{0,0,0}},head=reference;
    auto neutral=HeadWorld(game,reference,head,100);
    for(int i=0;i<16;i++)Check(Near(neutral.m[i],game.m[i]),"neutral pose preserves game camera");
    head.position={0.1f,0.2f,-0.3f};
    auto moved=HeadWorld(game,reference,head,100);
    Check(Near(moved.m[12],20)&&Near(moved.m[13],0)&&Near(moved.m[14],60),"right/up/forward conversion");
    head=reference;head.orientation={0,std::sqrt(.5f),0,std::sqrt(.5f)};
    auto turned=HeadWorld(game,reference,head,100);
    Check(Near(turned.m[8],-1)&&Near(turned.m[10],0),"head yaw turns forward toward left");
    auto identity=Multiply(turned,InverseRigid(turned));
    for(int i=0;i<16;i++)Check(Near(identity.m[i],i%5==0?1.f:0.f),"rigid inverse");
    Transport::Tracking t{};t.head=reference;t.valid=1;
    for(int i=0;i<2;i++) {t.eyes[i].pose=reference;t.eyes[i].pose.position.x=i?.032f:-.032f;
        t.eyes[i].left=-.8f;t.eyes[i].right=.9f;t.eyes[i].up=.85f;t.eyes[i].down=-.75f;}
    Matrix p;p.m[10]=1.0001f;p.m[14]=-1.0001f;
    for(unsigned eye=0;eye<2;eye++) {
        auto ep=EyeProjection(p,t,eye,100);
        // An eye-local frustum edge must project to exactly the clip-space edge.
        for(float side:{-1.f,1.f}) {
            float angle=side<0?t.eyes[eye].left:t.eyes[eye].right;
            float x=t.eyes[eye].pose.position.x*100+std::tan(angle)*1000;
            float clipX=x*ep.m[0]+1000*ep.m[8]+ep.m[12];
            Check(Near(clipX/1000,side),"asymmetric eye frustum and IPD");
        }
    }
    t.eyes[0].left=-.9f;t.eyes[0].right=.7f;
    t.eyes[1].left=-.7f;t.eyes[1].right=.9f;
    for(unsigned eye=0;eye<2;eye++) {
        Matrix world=Rotation({0,0,0,1});world.m[12]=100;world.m[13]=-20;world.m[14]=300;
        auto centreView=InverseRigid(world);
        auto fullEyeView=Multiply(centreView,InverseRigid(EyeWorld(t,eye,300)));
        auto oldCombined=Multiply(centreView,EyeProjection(p,t,eye,300));
        auto newCombined=Multiply(fullEyeView,EyeFrustum(p,t,eye));
        for(int i=0;i<16;i++)Check(Near(oldCombined.m[i],newCombined.m[i]),"explicit eye view preserves geometry clip positions");
        auto eyeAbsolute=Multiply(EyeWorld(t,eye,300),world);
        auto viewIdentity=Multiply(eyeAbsolute,fullEyeView);
        for(int i=0;i<16;i++)Check(Near(viewIdentity.m[i],i%5==0?1.f:0.f),"shader eye position and view agree");
        auto reconstruct=DepthToWorld(world,t,eye,300);
        auto project=EyeProjection(p,t,eye,300);
        for(float z:{100.f,1000.f})for(float u:{.1f,.5f,.9f})for(float v:{.2f,.8f}) {
            float input[]={u*z,v*z,z,1},point[4]{},clip[4]{};
            for(int j=0;j<4;j++)for(int k=0;k<4;k++)point[j]+=input[k]*reconstruct.m[k*4+j];
            point[0]-=100;point[1]+=20;point[2]-=300;
            for(int j=0;j<4;j++)for(int k=0;k<4;k++)clip[j]+=point[k]*project.m[k*4+j];
            Check(Near(clip[0]/clip[3],2*u-1)&&Near(clip[1]/clip[3],1-2*v),"lighting depth reconstruction matches asymmetric eye projection");
        }
    }
    {
        // A captured projected-shadow transform. Both eyes must sample the
        // same effect location for one surface point, including canted views.
        Matrix effect{{-.000248601f,.00188389f,-.00010397f,0,
            .00197977f,.000278757f,.00000880958f,0,
            -.000136736f,.000610938f,.000316581f,0,
            -2.01755f,-7.06894f,-.484532f,1}};
        auto sample=t;
        for(float cant:{0.f,.08f})for(unsigned eye=0;eye<2;eye++) {
            float angle=eye?cant:-cant;
            sample.eyes[eye].pose.orientation={0,std::sin(angle/2),0,std::cos(angle/2)};
            auto eyeWorld=EyeWorld(sample,eye,300);
            auto corrected=Multiply(eyeWorld,effect);
            auto centreToEye=InverseRigid(eyeWorld);
            for(float depth:{30.f,300.f,3000.f}) {
                float centre[]={17,-21,depth,1},local[4]{},expected[4]{},actual[4]{};
                for(int j=0;j<4;j++)for(int k=0;k<4;k++) {
                    local[j]+=centre[k]*centreToEye.m[k*4+j];
                    expected[j]+=centre[k]*effect.m[k*4+j];
                }
                for(int j=0;j<4;j++)for(int k=0;k<4;k++)actual[j]+=local[k]*corrected.m[k*4+j];
                for(int j=0;j<4;j++)Check(Near(actual[j],expected[j]),"projected effect stays on one surface for both eyes");
            }
        }
    }
    {
        auto dome=Rotation({0,0,0,1});dome.m[12]=58577;dome.m[13]=-9471;
        auto camera=Rotation({0,0,0,1});camera.m[12]=58580;camera.m[13]=-9470;
        for(unsigned eye=0;eye<2;eye++) {
            auto sky=SkyProjection(dome,camera,p,t,eye);
            auto moved=camera;moved.m[12]+=100;moved.m[14]-=200;
            auto same=SkyProjection(dome,moved,p,t,eye);
            for(int i=0;i<16;i++)Check(Near(sky.m[i],same.m[i]),"sky ignores head translation");
            const auto& e=t.eyes[eye];float l=std::tan(e.left),r=std::tan(e.right);
            for(float z:{100.f,10000.f}) {
                float ndc=(.2f*z*sky.m[0]+z*sky.m[8]+sky.m[12])/z;
                float ray=(ndc*(r-l)+(r+l))*.5f;
                Check(Near(ray,.2f),"cloud direction has no finite IPD disparity in asymmetric eyes");
            }
        }
        auto turned=Rotation({0,std::sqrt(.5f),0,std::sqrt(.5f)});
        auto sky=SkyProjection(dome,turned,p,t,0);
        auto expected=Multiply(InverseRigid(turned),EyeProjection(p,t,0,0));
        for(int i=0;i<16;i++)Check(Near(sky.m[i],expected.m[i]),"sky follows world orientation under head turns");
    }
    for(unsigned eye=0;eye<2;eye++) {
        auto hud=HudClipTransform(t,eye);
        for(float x:{-.9f,0.f,.9f}) {
            float clipX=x*hud.m[0]+hud.m[12],clipW=x*hud.m[3]+hud.m[15];
            const auto& e=t.eyes[eye];float l=std::tan(e.left),r=std::tan(e.right);
            float ray=(clipX/clipW*(r-l)+(r+l))*.5f;
            float commonX=e.pose.position.x+2.f*ray;
            Check(Near(commonX,x*1.6f),"both HUD eye rays meet the same plane point");
        }
    }
    puts("PASS tracking mailbox contention/expiry, camera math, asymmetric stereo frustum and binocular HUD alignment");
}

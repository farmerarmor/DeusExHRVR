#pragma once
#include "WeaponCalibration.h"
inline void CheckWeaponCalibration(){
    using namespace WeaponCalibration;
    auto equal=[](const Matrix& a,const Matrix& b){for(int j=0;j<16;++j)if(std::abs(a.m[j]-b.m[j])>.0002f)return false;return true;};
    auto identity=Identity();auto raw=identity;raw.m[12]=15;raw.m[13]=-3;raw.m[14]=8;
    auto moved=CameraMath::Rotation({0,0,std::sin(.3f),std::cos(.3f)});moved.m[12]=15.2f;moved.m[13]=-3.1f;moved.m[14]=8.05f;
    Session s;
    auto r=s.Update(raw,identity,1,100,3,true,100);
    Check(r.event==Event::None,"calibration waits for one second");
    for(uint64_t t=200;t<1100;t+=100)s.Update(raw,identity,1,100,3,true,t);
    r=s.Update(raw,identity,1,100,3,true,1100);
    Check(r.event==Event::Frozen,"calibration freezes at one second");
    r=s.Update(moved,identity,1,100,3,true,1200);
    Check(equal(r.muzzle,raw),"calibration freezes world position and rotation while hand moves");
    r=s.Update(moved,identity,1,100,1,true,1300);
    Check(r.event==Event::Save&&equal(CameraMath::Multiply(r.offset,moved),raw),"saved local offset preserves frozen pose on release");
    auto saved=r.offset;
    Check(s.Update(moved,saved,1,100,0,true,1400).event==Event::None,"second stick release cannot save twice");
    Check(equal(Units(Metres(raw,300),300),raw),"calibration translations roundtrip game units");
    // A calibrated camera's forward and origin match the actual firing matrix.
    auto camera=Camera(saved),firing=CameraMath::FiringFromMuzzle(saved);
    for(int j=0;j<3;++j)Check(Near(camera.m[8+j],firing.m[8+j])&&Near(camera.m[12+j],firing.m[12+j]),"calibrated scope matches firing line");
    for(int cancel=0;cancel<4;++cancel){
        Session interrupted;
        for(uint64_t t=100;t<=1100;t+=100)interrupted.Update(raw,identity,1,100,3,true,t);
        auto result=interrupted.Update(moved,identity,cancel==0?2:1,cancel==1?101:100,0,cancel!=2,cancel==3?1500:1200);
        Check(result.event==Event::Cancelled,"weapon change tracking loss and sampling gaps cancel without saving");
    }
    puts("PASS weapon calibration freeze, firing alignment, cancellation and per-weapon persistence math");
    wchar_t folder[MAX_PATH]{},file[MAX_PATH]{};Check(GetTempPathW(MAX_PATH,folder)&&GetTempFileNameW(folder,L"wrc",0,file),"create isolated calibration test file");
    {
        Store store(file);Check(equal(store.Get(1),identity),"new weapon starts with neutral offset");
        Check(store.Save(1,saved),"save calibrated weapon");
        Check(equal(store.Get(2),identity),"another weapon is unaffected");
        auto bad=saved;bad.m[0]=NAN;Check(!store.Save(1,bad),"reject nonfinite saved offset");
        bad=identity;bad.m[0]=-1;Check(!store.Save(1,bad),"reject mirrored offset");
    }
    {Store restarted(file);Check(equal(restarted.Get(1),saved),"calibration persists across a fresh store");}
    Check(DeleteFileW(file)!=0,"remove isolated calibration test file");
}

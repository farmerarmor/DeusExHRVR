#pragma once
#include "CameraMath.h"
#include <map>
#include <sstream>
#include <locale>
#include <iomanip>
#include <string>

namespace WeaponCalibration {
using CameraMath::Matrix;
inline Matrix Identity(){Matrix m{};m.m[0]=m.m[5]=m.m[10]=m.m[15]=1;return m;}
inline Matrix Metres(Matrix m,float scale){for(int j=12;j<15;++j)m.m[j]/=scale;return m;}
inline Matrix Units(Matrix m,float scale){for(int j=12;j<15;++j)m.m[j]*=scale;return m;}
inline Matrix Camera(const Matrix& muzzle){
    auto m=muzzle;
    for(int j=0;j<3;++j){m.m[j]=-muzzle.m[j];m.m[4+j]=-muzzle.m[8+j];m.m[8+j]=-muzzle.m[4+j];}
    return m;
}
inline bool Valid(const Matrix& m){
    for(float v:m.m)if(!std::isfinite(v))return false;
    if(std::abs(m.m[3])+std::abs(m.m[7])+std::abs(m.m[11])>.001f || std::abs(m.m[15]-1)>.001f)return false;
    if(std::hypot(m.m[12],m.m[13],m.m[14])>2.f)return false;
    for(int i=0;i<3;++i)for(int j=0;j<3;++j){float d=0;for(int k=0;k<3;++k)d+=m.m[i*4+k]*m.m[j*4+k];if(std::abs(d-(i==j?1.f:0.f))>.002f)return false;}
    float det=m.m[0]*(m.m[5]*m.m[10]-m.m[6]*m.m[9])-m.m[1]*(m.m[4]*m.m[10]-m.m[6]*m.m[8])+m.m[2]*(m.m[4]*m.m[9]-m.m[5]*m.m[8]);
    return std::abs(det-1)<.002f;
}
class Store {
    std::map<uint32_t,Matrix> offsets;
    std::wstring path;
    static std::wstring Section(uint32_t key){wchar_t s[32];swprintf_s(s,L"Weapon_%08X",key);return s;}
public:
    explicit Store(const wchar_t* file=L"DeusExHRVR-weapons.ini"){wchar_t full[MAX_PATH]{};if(GetFullPathNameW(file,MAX_PATH,full,nullptr))path=full;else path=file;}
    Matrix Get(uint32_t key){
        auto found=offsets.find(key);if(found!=offsets.end())return found->second;
        wchar_t text[1024]{};GetPrivateProfileStringW(Section(key).c_str(),L"Offset",L"",text,1024,path.c_str());
        Matrix m=Identity(),parsed{};std::wistringstream in(text);in.imbue(std::locale::classic());bool ok=true;
        for(float& v:parsed.m)if(!(in>>v)){ok=false;break;}
        in>>std::ws;if(ok&&in.eof()&&Valid(parsed))m=parsed;
        offsets[key]=m;return m;
    }
    bool Save(uint32_t key,const Matrix& m){
        if(!Valid(m))return false;
        std::wostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(9);
        for(float v:m.m)out<<v<<L' ';
        if(!WritePrivateProfileStringW(Section(key).c_str(),L"Offset",out.str().c_str(),path.c_str()))return false;
        offsets[key]=m;return true;
    }
};
enum class Event {None,Frozen,Save,Cancelled};
struct Result {Matrix muzzle,offset;Event event=Event::None;};
class Session {
    bool pending{},frozen{},blocked{};
    uint64_t start{},last{};uintptr_t instance{};uint32_t weapon{};
    Matrix held{};
public:
    bool Busy()const{return pending||frozen;}
    void Cancel(){pending=frozen=false;blocked=true;}
    Result Update(const Matrix& raw,const Matrix& offset,uint32_t key,uintptr_t token,
                  unsigned clicks,bool eligible,uint64_t now){
        Result r{CameraMath::Multiply(offset,raw),offset};
        if(!eligible || (last&&(now<last||now-last>=250)) ||
           ((pending||frozen)&&(weapon!=key||instance!=token))){
            if(pending||frozen)r.event=Event::Cancelled;
            Cancel();last=now;return r;
        }
        last=now;
        if(blocked){if(!clicks)blocked=false;return r;}
        if(clicks==3){
            if(!pending&&!frozen){pending=true;start=now;weapon=key;instance=token;}
            if(pending&&now-start>=1000){pending=false;frozen=true;held=r.muzzle;r.event=Event::Frozen;}
            if(frozen)r.muzzle=held;
        }else if(frozen){
            r.offset=CameraMath::Multiply(held,CameraMath::InverseRigid(raw));
            r.event=Valid(r.offset)?Event::Save:Event::Cancelled;
            if(r.event==Event::Save)r.muzzle=held;
            Cancel();
        }else if(pending){Cancel();}
        return r;
    }
};
}

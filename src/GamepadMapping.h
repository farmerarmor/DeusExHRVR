#pragma once
#include "SharedPair.h"
#include <algorithm>
#include <cmath>
#include <Xinput.h>
namespace GamepadMapping {
struct Input {
    bool active{},a{},b{},x{},y{},leftClick{},rightClick{},menu{};
    float leftX{},leftY{},rightX{},rightY{},leftTrigger{},rightTrigger{},leftGrip{},rightGrip{};
};
inline float Finite(float v){return std::isfinite(v)?v:0.f;}
inline int16_t Stick(float v){return static_cast<int16_t>(std::lround(std::clamp(Finite(v),-1.f,1.f)*32767));}
inline uint8_t Trigger(float v){return static_cast<uint8_t>(std::lround(std::clamp(Finite(v),0.f,1.f)*255));}
class Mapper {
    bool menuDown{},longMenu{},rightDown{},dpadUsed{};
    bool consumedX{},consumedY{};
    uint64_t menuStart{},rightStart{},backUntil{},startUntil{},scopeUntil{},lastTick{};
public:
    void Reset(){*this={};}
    Transport::Gamepad Update(const Input& in,uint64_t now) {
        Transport::Gamepad out{};
        if(!in.active || (lastTick && now<lastTick)){Reset();return out;}
        lastTick=now;out.valid=1;
        out.leftX=Stick(in.leftX);out.leftY=Stick(in.leftY);
        out.rightX=Stick(in.rightX);out.rightY=Stick(in.rightY);
        out.leftTrigger=Trigger(in.leftTrigger);out.rightTrigger=Trigger(in.rightTrigger);
        auto button=[&](bool on,uint16_t bit){if(on)out.buttons|=bit;};
        button(in.a,XINPUT_GAMEPAD_A);button(in.b,XINPUT_GAMEPAD_Y);
        if(!in.x)consumedX=false;
        if(!in.y)consumedY=false;
        if(in.rightClick){consumedX|=in.x;consumedY|=in.y;}
        button(in.x&&!consumedX,XINPUT_GAMEPAD_X);
        button(in.y&&!consumedY,XINPUT_GAMEPAD_B);
        button(in.leftGrip>.5f,XINPUT_GAMEPAD_LEFT_SHOULDER);
        button(in.rightGrip>.5f,XINPUT_GAMEPAD_RIGHT_SHOULDER);
        button(in.leftClick,XINPUT_GAMEPAD_LEFT_THUMB);
        if(in.rightClick && !rightDown){rightStart=now;dpadUsed=false;}
        if(in.rightClick) {
            button(in.x,XINPUT_GAMEPAD_BACK);
            button(in.y,XINPUT_GAMEPAD_START);
            if(in.x || in.y){dpadUsed=true;scopeUntil=0;}
            // Four cardinal choices prevent diagonal stick motion from firing
            // two augmentations. The modifier always consumes walking axes.
            out.leftX=out.leftY=0;
            float x=Finite(in.leftX),y=Finite(in.leftY);
            if(std::max(std::abs(x),std::abs(y))>=.5f) {
                dpadUsed=true;
                if(std::abs(y)>=std::abs(x))button(true,y>0?XINPUT_GAMEPAD_DPAD_UP:XINPUT_GAMEPAD_DPAD_DOWN);
                else button(true,x>0?XINPUT_GAMEPAD_DPAD_RIGHT:XINPUT_GAMEPAD_DPAD_LEFT);
            }
        } else if(rightDown && !dpadUsed && now-rightStart<350)scopeUntil=now+120;
        rightDown=in.rightClick;
        if(in.menu && !menuDown){menuStart=now;longMenu=false;}
        if(in.menu && !longMenu && now-menuStart>=1500){startUntil=now+120;longMenu=true;}
        if(!in.menu && menuDown && !longMenu) {
            if(now-menuStart>=1500)startUntil=now+120;else backUntil=now+120;
        }
        menuDown=in.menu;
        button(now<backUntil,XINPUT_GAMEPAD_BACK);button(now<startUntil,XINPUT_GAMEPAD_START);
        button(now<scopeUntil,XINPUT_GAMEPAD_RIGHT_THUMB);
        return out;
    }
};
}

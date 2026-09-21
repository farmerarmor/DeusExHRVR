#pragma once
#include "GamepadMapping.h"
#include <limits>
inline void CheckGamepadMapping() {
    using namespace GamepadMapping;
    Mapper m;Input i;i.active=true;
    i.a=i.b=i.x=i.y=i.leftClick=true;i.leftGrip=i.rightGrip=1;
    i.leftTrigger=.5f;i.rightTrigger=1;i.leftY=1;i.rightX=-1;
    m.Update(i,100);
    auto p=m.Update(i,250);
    Check(p.buttons==(XINPUT_GAMEPAD_A|XINPUT_GAMEPAD_B|XINPUT_GAMEPAD_X|XINPUT_GAMEPAD_Y|
        XINPUT_GAMEPAD_LEFT_THUMB|XINPUT_GAMEPAD_LEFT_SHOULDER|XINPUT_GAMEPAD_RIGHT_SHOULDER),"Xbox face buttons, crouch and bumpers");
    Check(p.leftTrigger==128 && p.rightTrigger==255 && p.leftY==32767 && p.rightX==-32767,"analog triggers and Xbox stick signs/ranges");
    Check(m.Update(i,2000).buttons==p.buttons,"held face buttons survive for inventory and lethal takedown");
    i={};i.active=true;i.b=true;m.Reset();
    Check(m.Update(i,100).buttons==XINPUT_GAMEPAD_Y && m.Update(i,2000).buttons==XINPUT_GAMEPAD_Y,
        "right B maps to Xbox Y including held quick inventory");
    i.b=false;i.y=true;
    Check(m.Update(i,2100).buttons==XINPUT_GAMEPAD_B && m.Update(i,4000).buttons==XINPUT_GAMEPAD_B,
        "left Y maps to Xbox B including held lethal takedown");
    i={};i.active=true;m.Reset();i.menu=true;
    Check(m.Update(i,100).buttons==0 && m.Update(i,1599).buttons==0,"menu press waits to distinguish short from long");
    i.menu=false;p=m.Update(i,1599);
    Check(p.buttons==XINPUT_GAMEPAD_BACK,"menu release at 1499ms opens only in-game menu");
    Check(m.Update(i,1720).buttons==0,"short-menu pulse releases");
    m.Reset();i.menu=true;m.Update(i,100);
    Check(m.Update(i,1600).buttons==XINPUT_GAMEPAD_START,"menu at exactly 1500ms opens pause");
    Check(m.Update(i,2000).buttons==0,"held menu does not repeatedly toggle pause");
    i.menu=false;Check(m.Update(i,2100).buttons==0,"long-menu release never also opens inventory");
    m.Reset();i.menu=true;m.Update(i,100);i.menu=false;
    Check(m.Update(i,1800).buttons==XINPUT_GAMEPAD_START,"long menu still works across a skipped sample at the threshold");
    m.Reset();i={};i.active=true;i.rightClick=true;
    Check(m.Update(i,100).buttons==0,"right click delays scope until gesture is known");
    i.rightClick=false;Check(m.Update(i,200).buttons==XINPUT_GAMEPAD_RIGHT_THUMB,"right stick tap retains scope");
    Check(m.Update(i,321).buttons==0,"scope pulse releases");
    for(auto bits:{XINPUT_GAMEPAD_DPAD_UP,XINPUT_GAMEPAD_DPAD_DOWN,XINPUT_GAMEPAD_DPAD_LEFT,XINPUT_GAMEPAD_DPAD_RIGHT}) {
        m.Reset();i={};i.active=true;i.rightClick=true;
        i.leftX=bits==XINPUT_GAMEPAD_DPAD_LEFT?-1.f:bits==XINPUT_GAMEPAD_DPAD_RIGHT?1.f:0.f;
        i.leftY=bits==XINPUT_GAMEPAD_DPAD_DOWN?-1.f:bits==XINPUT_GAMEPAD_DPAD_UP?1.f:0.f;
        p=m.Update(i,100);
        Check(p.buttons==bits && p.leftX==0 && p.leftY==0,"modifier sends D-pad and consumes walking");
        i.rightClick=false;p=m.Update(i,200);
        Check(!(p.buttons&XINPUT_GAMEPAD_RIGHT_THUMB),"D-pad chord release never also toggles scope");
        Check(p.leftX || p.leftY,"walking resumes after modifier release");
    }
    for(bool pause:{false,true}) {
        m.Reset();i={};i.active=true;i.rightClick=true;
        i.x=!pause;i.y=pause;
        const auto menuBit=pause?XINPUT_GAMEPAD_START:XINPUT_GAMEPAD_BACK;
        Check(m.Update(i,100).buttons==menuBit,"right-stick plus left face button sends only its menu");
        Check(m.Update(i,150).buttons==menuBit,"menu chord stays held without face action");
        i.rightClick=false;
        Check(m.Update(i,200).buttons==0,"modifier released first does not fire scope, reload or takedown");
        i.x=i.y=false;m.Update(i,220);i.x=!pause;i.y=pause;
        Check(m.Update(i,240).buttons==(pause?XINPUT_GAMEPAD_B:XINPUT_GAMEPAD_X),"normal face action resumes after release");
        m.Reset();i.rightClick=true;m.Update(i,300);i.x=i.y=false;
        Check(m.Update(i,320).buttons==0,"face button released first releases menu");
        i.rightClick=false;Check(m.Update(i,340).buttons==0,"menu chord never also triggers scope");
    }
    m.Reset();i={};i.active=true;i.rightClick=true;i.x=true;i.leftY=1;
    p=m.Update(i,100);
    Check(p.buttons==(XINPUT_GAMEPAD_BACK|XINPUT_GAMEPAD_DPAD_UP)&&!p.leftY,"menu chord and D-pad coexist without walking");
    i.active=false;m.Update(i,110);i={};i.active=true;i.x=true;
    Check(m.Update(i,120).buttons==XINPUT_GAMEPAD_X,"focus loss clears consumed face buttons");
    for(bool releaseLeft:{false,true}) {
        m.Reset();i={};i.active=true;i.leftClick=true;
        Check(!m.Update(i,100).buttons,"left click grace period allows calibration chord");
        i.rightClick=true;i.leftY=1;i.rightX=1;i.rightTrigger=1;
        auto silent=m.Update(i,150);
        Check(!silent.buttons&&!silent.leftY&&!silent.rightX&&!silent.rightTrigger,"calibration consumes crouch scope movement firing and D-pad");
        m.Update(i,1200);if(releaseLeft)i.leftClick=false;else i.rightClick=false;
        Check(!m.Update(i,1300).buttons,"calibration consumes first stick release");
        i.leftClick=i.rightClick=false;Check(!m.Update(i,1400).buttons,"calibration consumes second stick release without scope pulse");
    }
    m.Reset();i={};i.active=true;i.leftClick=true;m.Update(i,100);i.leftClick=false;
    Check(m.Update(i,200).buttons==XINPUT_GAMEPAD_LEFT_THUMB,"short left stick tap retains crouch");
    m.Reset();i={};i.active=true;i.rightClick=true;i.leftX=.7f;i.leftY=.8f;
    Check(m.Update(i,100).buttons==XINPUT_GAMEPAD_DPAD_UP,"diagonal selects just one augmentation");
    i.leftX=i.leftY=.1f;Check(m.Update(i,200).buttons==0,"D-pad centre releases direction");
    m.Reset();i={};i.active=true;i.rightClick=true;m.Update(i,100);i.rightClick=false;
    Check(m.Update(i,600).buttons==0,"held unused modifier does not toggle scope");
    m.Reset();i.menu=i.rightClick=true;m.Update(i,100);i.active=false;
    p=m.Update(i,200);Check(!p.valid && !p.buttons && !p.leftX && !p.rightTrigger,"focus/controller loss releases all input");
    i={};i.active=true;Check(m.Update(i,300).buttons==0,"returning focus does not generate release gestures");
    i.leftX=std::numeric_limits<float>::quiet_NaN();i.rightTrigger=std::numeric_limits<float>::infinity();
    p=m.Update(i,400);Check(!p.leftX && !p.rightTrigger,"nonfinite controller values are neutral");
    puts("PASS Xbox mappings, D-pad modifier, scope tap, 1500ms menu gestures and focus-loss release");
}

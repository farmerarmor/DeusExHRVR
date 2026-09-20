#pragma once
#include "CameraMath.h"
#include <algorithm>
namespace ImmersiveScope {
class Gesture {
    bool engaged{};uint64_t since{};
public:
    void Reset(){engaged=false;since=0;}
    bool Update(const Transport::Tracking& t,bool eligible,uint64_t now) {
        if(!eligible || !t.valid || !t.rightController.valid || now<t.tick || now-t.tick>=250){Reset();return false;}
        auto d=CameraMath::Rotate(CameraMath::Inverse(t.head.orientation),{
            t.rightController.aim.position.x-t.head.position.x,
            t.rightController.aim.position.y-t.head.position.y,
            t.rightController.aim.position.z-t.head.position.z});
        auto f=CameraMath::Rotate(CameraMath::Multiply(CameraMath::Inverse(t.head.orientation),t.rightController.aim.orientation),{0,0,-1});
        const float radius=engaged?.42f:.28f;
        bool aligned=std::isfinite(d.x)&&std::isfinite(d.y)&&std::isfinite(d.z)&&
            d.x*d.x+d.y*d.y+d.z*d.z<radius*radius && d.z<.08f &&
            std::abs(d.x)<.22f && std::abs(d.y)<.25f && f.z<-(engaged?.35f:.65f);
        if(!aligned){Reset();return false;}
        if(!since)since=now;
        if(now>=since+150)engaged=true;
        return engaged;
    }
};
inline Transport::Tracking Zoom(const Transport::Tracking& input,float magnification) {
    auto t=input;const float zoom=std::clamp(magnification,1.f,12.f);
    // A rifle scope has one optical origin. Keeping physical eye offsets here
    // introduces magnified parallax between its reticle and the firing ray.
    // Retain each eye orientation/frustum, but project from the same origin.
    for(auto& e:t.eyes){e.pose.position=t.head.position;e.left=std::atan(std::tan(e.left)/zoom);e.right=std::atan(std::tan(e.right)/zoom);e.up=std::atan(std::tan(e.up)/zoom);e.down=std::atan(std::tan(e.down)/zoom);}
    return t;
}
}

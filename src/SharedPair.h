#pragma once
#include <windows.h>
#include <cstdint>
#include <cwchar>
namespace Transport {
inline constexpr uint32_t Magic=0x52485844, Version=5;
struct Quaternion {float x,y,z,w;};
struct Vector {float x,y,z;};
struct Pose {Quaternion orientation;Vector position;};
struct Eye {Pose pose;float left,right,up,down;};
struct Controller {Pose aim;uint32_t valid;};
struct Gamepad {
    uint32_t valid;
    uint16_t buttons;uint8_t leftTrigger,rightTrigger;
    int16_t leftX,leftY,rightX,rightY;
};
static_assert(sizeof(Gamepad)==16);
struct alignas(8) Tracking {
    uint64_t id,tick;
    Pose head;
    Eye eyes[2];
    uint32_t valid;
    Controller rightController;
    Gamepad gamepad;
    uint32_t controllerButtons; // Raw stick clicks: bit 0 left, bit 1 right (focused input only).
};
struct RenderInfo {Tracking tracking;uint32_t mode,eyeMask;};
static_assert(sizeof(Tracking)==192 && sizeof(RenderInfo)==200);
// Identical ABI in the x86 game and x64 companion. GPU keyed mutex protects
// texture contents + frameId; generation publishes a new texture description.
struct alignas(8) Header {
    uint32_t magic,version,pid,reserved;
    volatile LONG generation;
    uint32_t width,height,format;
    uint64_t sharedHandle;
    LUID adapter;
    uint64_t frameId;
    uint32_t swapEyes,reserved2;
    volatile LONG trackingLock;
    uint32_t reserved3;
    Tracking tracking;
    RenderInfo rendered;
};
static_assert(sizeof(Header)==464);
inline bool ReadTracking(Header* h,Tracking& result) {
    if(!h || InterlockedCompareExchange(&h->trackingLock,1,0))return false;
    result=h->tracking;InterlockedExchange(&h->trackingLock,0);return true;
}
inline void WriteTracking(Header* h,const Tracking& value) {
    if(!h || InterlockedCompareExchange(&h->trackingLock,1,0))return;
    h->tracking=value;InterlockedExchange(&h->trackingLock,0);
}
// A contended nonblocking mailbox read is not loss of tracking. Preserve the
// last sample briefly, but honour newly received invalid data and sample age.
struct TrackingReader {
    Tracking latest{};
    uint64_t reused{},rejected{};
    bool Read(Header* h,Tracking& result,uint64_t now) {
        if(!h){latest={};++rejected;return false;}
        Tracking incoming{};
        if(ReadTracking(h,incoming))latest=incoming;
        else ++reused;
        if(!latest.valid || !latest.tick || now<latest.tick || now-latest.tick>=250){++rejected;return false;}
        result=latest;return true;
    }
};
inline void Name(wchar_t (&name)[96],DWORD pid) {swprintf_s(name,L"Local\\DeusExHRVR-%lu",pid);}
inline void FrameEventName(wchar_t (&name)[96],DWORD pid) {swprintf_s(name,L"Local\\DeusExHRVR-frame-%lu",pid);}
}

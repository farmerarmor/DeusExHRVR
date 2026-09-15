#pragma once
#include <windows.h>
#include <cstdint>
#include <cwchar>
namespace HeadsetDisplay {
inline constexpr uint32_t Magic=0x44525844;
struct Settings {uint32_t magic,width,height;float refreshHz;};
static_assert(sizeof(Settings)==16);
inline bool Valid(const Settings& s) {
    return s.magic==Magic && s.width>0 && s.width<=16384 && s.height>0 && s.height<=8192;
}
inline void Name(wchar_t (&name)[96],DWORD pid) {swprintf_s(name,L"Local\\DeusExHRVR-display-%lu",pid);}
// x86 engine hooks, installed outside DllMain before render-context creation.
void Install();
bool Active();
// Returns the cached headset settings (width/height/refresh). Only valid
// when Active() returns true.
Settings GetSettings();
}

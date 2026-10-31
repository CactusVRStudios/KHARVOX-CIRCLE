#pragma once
#include <windows.h>
#include <cwchar>

namespace circle::motor {
// Deliberately no inherited DOOM addresses, shader patches or engine writes.
// Replace only after the Motor camera and render re-entry are measured and validated.
inline bool engineLayoutValidated() { return false; }
inline bool isTargetProcess() {
    wchar_t path[32768]{};
    if (!GetModuleFileNameW(nullptr, path, 32768)) return false;
    const wchar_t* base = wcsrchr(path, L'\\');
    return _wcsicmp(base ? base + 1 : path, L"TheGreatCircle.exe") == 0;
}
inline bool wantInterop() {
    wchar_t mode[32]{};
    GetEnvironmentVariableW(L"KHARVOX_CIRCLE_MODE", mode, 32);
    return _wcsicmp(mode, L"attach") != 0 && _wcsicmp(mode, L"off") != 0;
}
}

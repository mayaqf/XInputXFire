// xinput_probe.cpp - System32 の各 XInput DLL がコントローラを認識するか判定
// xinput.h / xinput.lib を使わず自前定義(プロキシ側と同様の理由)。
#include <windows.h>
#include <cstdio>

struct XINPUT_GAMEPAD {
    WORD  wButtons;
    BYTE  bLeftTrigger;
    BYTE  bRightTrigger;
    short sThumbLX;
    short sThumbLY;
    short sThumbRX;
    short sThumbRY;
};
struct XINPUT_STATE {
    DWORD dwPacketNumber;
    XINPUT_GAMEPAD Gamepad;
};
typedef DWORD (WINAPI *PFN_GetState)(DWORD, XINPUT_STATE*);

static HMODULE LoadSys(const wchar_t* name) {
    wchar_t dir[MAX_PATH], path[MAX_PATH];
    UINT n = GetSystemDirectoryW(dir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return nullptr;
    wsprintfW(path, L"%s\\%s", dir, name);
    return LoadLibraryExW(path, nullptr, 0);
}

static void Probe(const wchar_t* name) {
    HMODULE h = LoadSys(name);
    if (!h) { wprintf(L"[%s] LoadLibrary FAILED\n", name); return; }
    auto fn = (PFN_GetState)GetProcAddress(h, "XInputGetState");
    if (!fn) { wprintf(L"[%s] XInputGetState not found\n", name); return; }
    for (DWORD i = 0; i < 4; ++i) {
        XINPUT_STATE st{};
        DWORD hr = fn(i, &st);
        if (hr == 0) { // ERROR_SUCCESS
            wprintf(L"[%s] idx=%lu CONNECTED pkt=%lu L2=%u R2=%u btn=0x%04x\n",
                name, i, st.dwPacketNumber, st.Gamepad.bLeftTrigger,
                st.Gamepad.bRightTrigger, st.Gamepad.wButtons);
        }
    }
    wprintf(L"[%s] done (only connected shown)\n", name);
}

int main() {
    Probe(L"xinput1_4.dll");
    Probe(L"XInput9_1_0.dll");
    Probe(L"xinput1_3.dll");
    return 0;
}
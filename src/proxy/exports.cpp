// exports.cpp - XInput エクスポート関数の転送 + GetState 連射改変
// エクスポート名と序数は XInputProxy.def で正規 xinput1_3.dll に合わせて固定。
// 各関数: StickyInit(初回1回) → 本物関数ポインタNULL判定 → 転送。
// GetState のみ転送後に XFireEngine::Apply で DPAD/ABXY を連射改変。
#include "XInputProxy.h"
#include "Config.h"
#include "XFireEngine.h"
#include "Announcer.h"
#include "DiagLog.h"
#include <atomic>
#include <cstdio>

namespace {
SRWLOCK g_stickyLock = SRWLOCK_INIT;
std::atomic<int> g_stickyDone{0};
} // namespace

// 初回エクスポート呼出時に1回だけ。DllMainでは呼ばない(ローダーロック回避)。
// 順序: 本物DLLロード → ini読込 → QPC初期化 → 起動診断ログ1行 → 起動音再生。
void StickyInit() {
    if (g_stickyDone.load(std::memory_order_acquire)) return;
    AcquireSRWLockExclusive(&g_stickyLock);
    if (!g_stickyDone.load(std::memory_order_relaxed)) {
        bool ok = RealXInputLoader::LoadOnce();
        Config::LoadOnce();
        XFireEngine::InitOnce();
        g_stickyDone.store(1, std::memory_order_release);
        char b[256];
        const RealXInput& r = RealXInputLoader::Get();
        sprintf_s(b, "[STICKYINIT] LoadOnce=%d hDll=%p GetState=%p SetState=%p Cap=%p Enable=%p",
            ok ? 1 : 0, r.hDll, (void*)r.GetState, (void*)r.SetState, (void*)r.GetCapabilities, (void*)r.Enable);
        DiagLog::Log(b);
        Announcer::StartupBeep(); // 初回エクスポート(プロキシロード)完了をビープで通知
    }
    ReleaseSRWLockExclusive(&g_stickyLock);
}

extern "C" DWORD WINAPI XInputGetState(DWORD dwUserIndex, XINPUT_STATE* pState) {
    StickyInit();
    const RealXInput& r = RealXInputLoader::Get();
    if (!r.GetState) return ERROR_DEVICE_NOT_CONNECTED;
    if (!pState) return ERROR_BAD_ARGUMENTS;
    DWORD hr = r.GetState(dwUserIndex, pState);
    if (hr != ERROR_SUCCESS) return hr; // 未接続等はそのまま透過
    XFireEngine::Apply(dwUserIndex, pState); // トリガ押下時のみ DPAD/ABXY を連射改変
    return hr;
}

extern "C" DWORD WINAPI XInputSetState(DWORD dwUserIndex, XINPUT_VIBRATION* pVibration) {
    StickyInit();
    const RealXInput& r = RealXInputLoader::Get();
    if (!r.SetState) return ERROR_DEVICE_NOT_CONNECTED;
    return r.SetState(dwUserIndex, pVibration);
}

extern "C" DWORD WINAPI XInputGetCapabilities(DWORD dwUserIndex, DWORD dwFlags, XINPUT_CAPABILITIES* pCapabilities) {
    StickyInit();
    const RealXInput& r = RealXInputLoader::Get();
    if (!r.GetCapabilities) return ERROR_DEVICE_NOT_CONNECTED;
    return r.GetCapabilities(dwUserIndex, dwFlags, pCapabilities);
}

extern "C" void WINAPI XInputEnable(BOOL enable) {
    StickyInit();
    const RealXInput& r = RealXInputLoader::Get();
    if (r.Enable) r.Enable(enable);
}

extern "C" DWORD WINAPI XInputGetKeystroke(DWORD dwUserIndex, DWORD dwReserved, PXINPUT_KEYSTROKE pKeystroke) {
    StickyInit();
    const RealXInput& r = RealXInputLoader::Get();
    if (!r.GetKeystroke) return ERROR_DEVICE_NOT_CONNECTED;
    return r.GetKeystroke(dwUserIndex, dwReserved, pKeystroke);
}

extern "C" DWORD WINAPI XInputGetBatteryInformation(DWORD dwUserIndex, BYTE devType, XINPUT_BATTERY_INFORMATION* pBatteryInformation) {
    StickyInit();
    const RealXInput& r = RealXInputLoader::Get();
    if (!r.GetBatteryInformation) return ERROR_DEVICE_NOT_CONNECTED;
    return r.GetBatteryInformation(dwUserIndex, devType, pBatteryInformation);
}

extern "C" DWORD WINAPI XInputGetDSoundAudioDeviceGuids(DWORD dwUserIndex, GUID* pRenderGuid, GUID* pCaptureGuid) {
    StickyInit();
    const RealXInput& r = RealXInputLoader::Get();
    if (!r.GetDSoundAudioDeviceGuids) return ERROR_NOT_SUPPORTED; // xinput1_4 には不在
    return r.GetDSoundAudioDeviceGuids(dwUserIndex, pRenderGuid, pCaptureGuid);
}

extern "C" DWORD WINAPI XInputGetAudioDeviceIds(DWORD dwUserIndex, wchar_t* pRenderDeviceId, UINT* pRenderCount, wchar_t* pCaptureDeviceId, UINT* pCaptureCount) {
    StickyInit();
    const RealXInput& r = RealXInputLoader::Get();
    if (!r.GetAudioDeviceIds) return ERROR_NOT_SUPPORTED; // xinput1_3 には不在(1.4 で追加)
    return r.GetAudioDeviceIds(dwUserIndex, pRenderDeviceId, pRenderCount, pCaptureDeviceId, pCaptureCount);
}
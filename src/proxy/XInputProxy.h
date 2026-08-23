// XInputProxy.h - プロキシDLL共通定義
// xinput.h を include しない(同ヘッダ内の #pragma comment(lib,"xinput*.lib") が
// 本物DLLとリンク衝突するため)。構造体・ボタン定数を自前定義し、本物DLLは
// 動的ロード(GetProcAddress)のみで利用する。xinput.lib は一切リンクしない。
#pragma once
#include <windows.h>

// ---- XInput ボタンビット定数(XINPUT_GAMEPAD_*) ----
#define XINPUT_GAMEPAD_DPAD_UP          0x0001
#define XINPUT_GAMEPAD_DPAD_DOWN        0x0002
#define XINPUT_GAMEPAD_DPAD_LEFT        0x0004
#define XINPUT_GAMEPAD_DPAD_RIGHT       0x0008
#define XINPUT_GAMEPAD_START            0x0010
#define XINPUT_GAMEPAD_BACK             0x0020
#define XINPUT_GAMEPAD_LEFT_THUMB       0x0040
#define XINPUT_GAMEPAD_RIGHT_THUMB      0x0080
#define XINPUT_GAMEPAD_LEFT_SHOULDER    0x0100
#define XINPUT_GAMEPAD_RIGHT_SHOULDER   0x0200
#define XINPUT_GAMEPAD_A                0x1000
#define XINPUT_GAMEPAD_B                0x2000
#define XINPUT_GAMEPAD_X                0x4000
#define XINPUT_GAMEPAD_Y                0x8000

// ---- XInput 構造体(Windows ABI・自然境界, 1.3/1.4 共通) ----
#pragma pack(push, 8)
typedef struct _XINPUT_GAMEPAD {
    WORD  wButtons;
    BYTE  bLeftTrigger;
    BYTE  bRightTrigger;
    SHORT sThumbLX;
    SHORT sThumbLY;
    SHORT sThumbRX;
    SHORT sThumbRY;
} XINPUT_GAMEPAD;

typedef struct _XINPUT_STATE {
    DWORD          dwPacketNumber;
    XINPUT_GAMEPAD Gamepad;
} XINPUT_STATE;

typedef struct _XINPUT_VIBRATION {
    WORD wLeftMotorSpeed;
    WORD wRightMotorSpeed;
} XINPUT_VIBRATION;

typedef struct _XINPUT_CAPABILITIES {
    BYTE             Type;
    BYTE             SubType;
    WORD             Flags;
    XINPUT_GAMEPAD   Gamepad;
    XINPUT_VIBRATION Vibration;
} XINPUT_CAPABILITIES;

typedef struct _XINPUT_KEYSTROKE {
    DWORD VirtualKey;
    WCHAR Unicode;
    WORD  Flags;
    BYTE  UserIndex;
    BYTE  HidCode;
} XINPUT_KEYSTROKE, *PXINPUT_KEYSTROKE;

typedef struct _XINPUT_BATTERY_INFORMATION {
    BYTE BatteryType;
    BYTE BatteryLevel;
} XINPUT_BATTERY_INFORMATION;
#pragma pack(pop)

// ---- 本物DLLの関数ポインタ型 ----
typedef DWORD (WINAPI *PFN_XInputGetState)(DWORD, XINPUT_STATE*);
typedef DWORD (WINAPI *PFN_XInputSetState)(DWORD, XINPUT_VIBRATION*);
typedef DWORD (WINAPI *PFN_XInputGetCapabilities)(DWORD, DWORD, XINPUT_CAPABILITIES*);
typedef void   (WINAPI *PFN_XInputEnable)(BOOL);
typedef DWORD (WINAPI *PFN_XInputGetKeystroke)(DWORD, DWORD, PXINPUT_KEYSTROKE);
typedef DWORD (WINAPI *PFN_XInputGetBatteryInformation)(DWORD, BYTE, XINPUT_BATTERY_INFORMATION*);
typedef DWORD (WINAPI *PFN_XInputGetDSoundAudioDeviceGuids)(DWORD, GUID*, GUID*);
typedef DWORD (WINAPI *PFN_XInputGetAudioDeviceIds)(DWORD, wchar_t*, UINT*, wchar_t*, UINT*);

// ---- 本物DLLの関数ポインタテーブル ----
struct RealXInput {
    HMODULE hDll;
    PFN_XInputGetState                GetState;
    PFN_XInputSetState                SetState;
    PFN_XInputGetCapabilities         GetCapabilities;
    PFN_XInputEnable                  Enable;
    PFN_XInputGetKeystroke            GetKeystroke;
    PFN_XInputGetBatteryInformation  GetBatteryInformation;
    PFN_XInputGetDSoundAudioDeviceGuids GetDSoundAudioDeviceGuids;
    PFN_XInputGetAudioDeviceIds       GetAudioDeviceIds; // 1.4のみ
};

namespace RealXInputLoader {
    bool LoadOnce();                  // 初回エクスポート呼出時に1回(スレッドセーフ)
    const RealXInput& Get();
}
// Config.h - ini 設定の遅延読込・キャッシュ
#pragma once
#include "XInputProxy.h"

struct XFireConfig {
    DWORD onMs             = 50;   // ON区間(ms)
    DWORD offMs            = 50;   // OFF区間(ms)
    DWORD firstOnMs        = 200;  // 押下直後の最初のON区間(ms・0=無効=OnMsと同値・既定200)
    BYTE  triggerThreshold = 150;  // 押下判定閾値(0-255)
    BYTE  hysteresisLow    = 140;  // 離上判定閾値(ON->OFF)
    WORD  targetButtons    = XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN
                          | XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT
                          | XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B
                          | XINPUT_GAMEPAD_X | XINPUT_GAMEPAD_Y; // 連射対象ビットマスク(既定=DPAD全方向+ABXY)
    bool  enableLT         = true;
    bool  enableRT         = true;
    // --- 連射マスタートグル(LT/RT ゲートの上掛け) ---
    WORD  toggleButtons    = XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_A; // トグル切替コンボ(LB|A)。全ビット同時押下の立ち上がりで ON/OFF 切替
    bool  defaultEnabled   = false; // 起動時のマスター状態(false=OFF・メニュー操作安全)
    bool  announceEnabled  = true; // トグル切替時の SAPI 音声アナウンス有無
    // --- 起動音(プロキシロード完了の通知・DLL 埋め込み WAVE リソース再生) ---
    bool  startupSound = true; // 起動音の有無(true=再生 / false=無効)
};

namespace Config {
    // 値域ポリシー(クランプ範囲)。本ヘッダが唯一の定義源。
    // OnMs/OffMs は 0 禁止(=フェーズwhileループの暴走防止)。
    inline constexpr DWORD kOnMsMin      = 1;     // OnMs/OffMs 下限
    inline constexpr DWORD kOnMsMax      = 10000; // OnMs/OffMs/FirstOnMs 上限
    inline constexpr DWORD kFirstOnMsMin = 0;     // FirstOnMs 下限(0=無効=OnMsと同値)
    inline constexpr BYTE  kByteMax      = 255;   // 閾値系(TriggerThreshold/HysteresisLow)上限

    // デフォルト値で初期化(targetButtons = DPAD全方向+ABXY)
    void ApplyDefaults(XFireConfig& cfg);
    // 初回エクスポート呼出時に1回だけ実行(スレッドセーフ)。
    // exeと同フォルダの XInputXFire.ini を読込。不在時はデフォルト値。
    void LoadOnce();
    // ロード済み設定への参照(LoadOnce後のみ有効)
    const XFireConfig& Get();
#ifdef XFIRE_TEST
    // テスト専用: 設定を直接注入(LoadOnce なしで即座に有効化)。
    void SetForTest(const XFireConfig& cfg);
    // テスト専用: ロード済みフラグをクリアし、次回 LoadOnce を有効化する。
    void ResetForTest();
    // テスト専用: LoadOnce で読む ini パスを注入する(nullptr で exe 同梱の既定に戻す)。
    void SetIniPathForTest(const wchar_t* path);
#endif
}
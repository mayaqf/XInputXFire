// XFireEngine.h - 連射タイミング制御コア
// 連射マスタートグル(既定OFF)の上に LT/RT 押下中のみ、連射対象ボタン(DPAD/ABXY)を
// QPC ベースの周期で ON/OFF 反転。トリガ値(bLeftTrigger/bRightTrigger)は触らない
// (ゲームへそのまま伝達)。4コントローラ(dwUserIndex 0-3)は独立状態。
#pragma once
#include "XInputProxy.h"

namespace XFireEngine {
    // 初回エクスポート呼出時に1回だけ実行(QPC周波数取得・マスター既定状態設定)。スレッドセーフ。
    void InitOnce();
    // XInputGetState 転送後に呼ぶ。コンボキー(LB+A)立ち上がりでマスターON/OFF切替。
    // マスターONかつ LT/RT 押下中なら pState の DPAD/ABXY を連射改変。トリガ値は変更しない。
    // 位相クロックは対象ボタン押下基準: 押下直後の最初のON区間は FirstOnMs(0=OnMsと同値)、
    // その後 OnMs/OffMs サイクル。対象ボタンを離すと位相リセットされ次回押下でFirstOnMsから再開。
    void Apply(DWORD dwUserIndex, XINPUT_STATE* pState);

#ifdef XFIRE_TEST
    // --- テスト専用API(本番ビルドには含まれない) ---
    // 現在時刻(ms)を外部注入。設定後は Apply が QPC ではなくこの値を使う。
    void SetTestClockNowMs(double nowMs);
    // 全コントローラの内部状態をリセット(マスター状態はリセットしない)。
    void ResetControllerState();
    // 連射マスター有効状態を強制設定(テスト用。本番はコンボキーで切替)。
    void SetMasterEnabled(bool enabled);
#endif
}
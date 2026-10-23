// XFireEngine.h - 連射タイミング制御コア
// 連射マスタートグル(既定OFF)の上に2種の連射モードを搭載:
//  (1) トリガモード: LT/RT 押下中のみ連射対象ボタン(DPAD/ABXY)を QPC 周期で ON/OFF 反転
//  (2) ボタン単独モード([RapidFire]): トリガ不要でボタン押しっぱなしで連射
// 両モードともトリガ値は押下ホールド期間中の held 以外は触らない(ゲームへそのまま伝達)。
// トリガの離上判定は HysteresisLow 未満で即解除(連射/所有権も即停止・Issue #10 仕様行)。
// TriggerOffDelayMs は判定に効かず、その後のゲームへ渡すトリガ押下(押下値ホールド)のみを
// ON 継続させる(期限で物理値へ降下・再押下で打ち切り+re-anchor)。
// 両リストの重複ボタンは「トリガ優先」で解決(トリガ活性時はトリガモードが駆動)。
// 4コントローラ(dwUserIndex 0-3)は独立状態。
#pragma once
#include "XInputProxy.h"

namespace XFireEngine {
    // 初回エクスポート呼出時に1回だけ実行(QPC周波数取得・マスター既定状態設定)。スレッドセーフ。
    void InitOnce();
    // XInputGetState 転送後に呼ぶ。コンボキー(LB+A)立ち上がりでマスターON/OFF切替。
    // マスターON時: (1)LT/RT 押下中はトリガモードが対象ボタンを連射、
    //               (2)ボタン押しっぱなし(トリガ不要)はボタン単独モードが連射。
    // トリガ値は変更しない。位相クロックは対象ボタン押下基準: 押下直後の最初のON区間は
    // FirstOnMs(0=OnMsと同値)、その後 OnMs/OffMs サイクル。対象ボタンを離すと位相リセットされ
    // 次回押下でFirstOnMsから再開(各モード独立の第1/第2位相クロック)。
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
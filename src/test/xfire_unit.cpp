// xfire_unit.cpp - 連射エンジン単体テスト(XFIRE_TEST でモックQPC時刻注入)
// コントローラ不要。Apply の ON/OFF 周期・ヒステリシス・トリガ透過・
// 対象外ボタン保護・4コントローラ独立を検証。
#include "XInputProxy.h"
#include "Config.h"
#include "InputTransform.h"
#include "XFireEngine.h"
#include <cstdio>
#include <cstring>
#include <string>

static int g_failures = 0;
static int g_checks = 0;
static void Check(bool cond, const char* msg) {
    g_checks++;
    if (!cond) { fprintf(stderr, "FAIL: %s\n", msg); g_failures++; }
    else printf("ok: %s\n", msg);
}

static XINPUT_STATE MakeState(WORD buttons, BYTE lt, BYTE rt) {
    XINPUT_STATE s;
    ZeroMemory(&s, sizeof(s));
    s.Gamepad.wButtons = buttons;
    s.Gamepad.bLeftTrigger = lt;
    s.Gamepad.bRightTrigger = rt;
    return s;
}

int main() {
    XFireEngine::InitOnce();

    XFireConfig cfg;
    Config::ApplyDefaults(cfg);
    cfg.onMs = 50; cfg.offMs = 50;
    cfg.firstOnMs = 0; // T1-T15 は FirstOnMs 無効(初回ON==OnMs)前提で検証
    cfg.triggerThreshold = 128; cfg.hysteresisLow = 64;
    cfg.toggleButtons = 0; // テスト入力(A/LB 等)がコンボ検出と干渉しないようトグル機能無効化
    cfg.announceEnabled = false; // テスト中は実際の音声再生を避ける
    cfg.buttonTargetButtons = 0; // T1-T14 はトリガモード単体検証。ボタン単独モードは無効化
    Config::SetForTest(cfg);
    // 既存ケースはマスターON前提で検証(本番はコンボキーで切替)。
    XFireEngine::SetMasterEnabled(true);

    // T1: トリガ閾値未満 -> 発動せず(state そのまま)
    {
        XFireEngine::ResetControllerState();
        XFireEngine::SetTestClockNowMs(0.0);
        XINPUT_STATE s = MakeState(XINPUT_GAMEPAD_A, 0, 100); // RT=100 < 128
        XFireEngine::Apply(0, &s);
        Check(s.Gamepad.wButtons == XINPUT_GAMEPAD_A, "T1: below threshold -> unchanged");
        Check(s.Gamepad.bRightTrigger == 100, "T1: trigger unchanged");
    }

    // T2: トリガ閾値以上 + ボタン押下 -> ON区間で押下維持
    {
        XFireEngine::ResetControllerState();
        XFireEngine::SetTestClockNowMs(0.0);
        XINPUT_STATE s = MakeState(XINPUT_GAMEPAD_A, 0, 200);
        XFireEngine::Apply(0, &s);
        Check((s.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0, "T2: ON phase keeps A pressed");
    }

    // T3: ON(50ms)経過 -> OFF区間で対象ボタン離
    {
        XFireEngine::ResetControllerState();
        XFireEngine::SetTestClockNowMs(0.0);
        XINPUT_STATE s = MakeState(XINPUT_GAMEPAD_A, 0, 200);
        XFireEngine::Apply(0, &s);                 // t=0 ON
        XFireEngine::SetTestClockNowMs(60.0);
        XINPUT_STATE s2 = MakeState(XINPUT_GAMEPAD_A, 0, 200);
        XFireEngine::Apply(0, &s2);                 // t=60 OFF
        Check((s2.Gamepad.wButtons & XINPUT_GAMEPAD_A) == 0, "T3: OFF phase releases A");
    }

    // T4: 周期トグル ON->OFF->ON->OFF (50/50ms)
    {
        XFireEngine::ResetControllerState();
        XFireEngine::SetTestClockNowMs(0.0);
        XINPUT_STATE s0 = MakeState(XINPUT_GAMEPAD_A, 0, 200);
        XFireEngine::Apply(0, &s0);                // t=0  ON(0-50)
        XFireEngine::SetTestClockNowMs(60.0);
        XINPUT_STATE s1 = MakeState(XINPUT_GAMEPAD_A, 0, 200);
        XFireEngine::Apply(0, &s1);                // t=60 OFF(50-100)
        Check((s1.Gamepad.wButtons & XINPUT_GAMEPAD_A) == 0, "T4: t=60 OFF");
        XFireEngine::SetTestClockNowMs(115.0);
        XINPUT_STATE s2 = MakeState(XINPUT_GAMEPAD_A, 0, 200);
        XFireEngine::Apply(0, &s2);                // t=115 ON(100-150)
        Check((s2.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0, "T4: t=115 ON");
        XFireEngine::SetTestClockNowMs(165.0);
        XINPUT_STATE s3 = MakeState(XINPUT_GAMEPAD_A, 0, 200);
        XFireEngine::Apply(0, &s3);                // t=165 OFF(150-200)
        Check((s3.Gamepad.wButtons & XINPUT_GAMEPAD_A) == 0, "T4: t=165 OFF");
    }

    // T5: トリガ値は触らない
    {
        XFireEngine::ResetControllerState();
        XFireEngine::SetTestClockNowMs(0.0);
        XINPUT_STATE s = MakeState(XINPUT_GAMEPAD_A, 150, 200);
        XFireEngine::Apply(0, &s);
        Check(s.Gamepad.bLeftTrigger == 150, "T5: LT unchanged");
        Check(s.Gamepad.bRightTrigger == 200, "T5: RT unchanged");
    }

    // T6: 対象外ボタン(LB)は ON/OFF 両区間で触らない
    {
        XFireEngine::ResetControllerState();
        XFireEngine::SetTestClockNowMs(0.0);
        XINPUT_STATE s = MakeState(XINPUT_GAMEPAD_LEFT_SHOULDER, 0, 200);
        XFireEngine::Apply(0, &s);
        Check((s.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0, "T6: non-target LB unchanged(ON)");
        XFireEngine::SetTestClockNowMs(60.0);
        XINPUT_STATE s2 = MakeState(XINPUT_GAMEPAD_LEFT_SHOULDER, 0, 200);
        XFireEngine::Apply(0, &s2);
        Check((s2.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0, "T6b: non-target LB unchanged(OFF)");
    }

    // T7: 4コントローラ独立
    {
        XFireEngine::ResetControllerState();
        XFireEngine::SetTestClockNowMs(0.0);
        XINPUT_STATE s0 = MakeState(XINPUT_GAMEPAD_A, 0, 200);
        XFireEngine::Apply(0, &s0);                // ctrl0 ON
        XINPUT_STATE s1 = MakeState(XINPUT_GAMEPAD_A, 0, 0);
        XFireEngine::Apply(1, &s1);                // ctrl1 トリガなし -> 物理 A
        Check((s0.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0, "T7: ctrl0 ON");
        Check((s1.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0, "T7b: ctrl1 no trigger -> physical A");
        XFireEngine::SetTestClockNowMs(60.0);
        XINPUT_STATE s0b = MakeState(XINPUT_GAMEPAD_A, 0, 200);
        XFireEngine::Apply(0, &s0b);               // ctrl0 OFF
        Check((s0b.Gamepad.wButtons & XINPUT_GAMEPAD_A) == 0, "T7c: ctrl0 OFF independent");
    }

    // T8: ヒステリシス(押下後 64-127 は active 維持、<64 で離上)
    {
        XFireEngine::ResetControllerState();
        XFireEngine::SetTestClockNowMs(0.0);
        XINPUT_STATE s = MakeState(XINPUT_GAMEPAD_A, 0, 200);
        XFireEngine::Apply(0, &s);                // active
        XFireEngine::SetTestClockNowMs(10.0);     // ON区間内
        XINPUT_STATE s2 = MakeState(XINPUT_GAMEPAD_A, 0, 100); // 64<=100<128 -> active維持
        XFireEngine::Apply(0, &s2);
        Check((s2.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0, "T8: hysteresis keeps active in ON");
        XINPUT_STATE s3 = MakeState(XINPUT_GAMEPAD_A, 0, 50);  // <64 -> released
        XFireEngine::Apply(0, &s3);
        Check((s3.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0, "T8b: released -> physical A (no xfire)");
    }

    // T9: LT 単独でも発動
    {
        XFireEngine::ResetControllerState();
        XFireEngine::SetTestClockNowMs(0.0);
        XINPUT_STATE s = MakeState(XINPUT_GAMEPAD_A, 200, 0); // LT=200
        XFireEngine::Apply(0, &s);
        Check((s.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0, "T9: LT alone triggers xfire ON");
    }

    // T10: 押されていない対象ボタンは連射しない
    {
        XFireEngine::ResetControllerState();
        XFireEngine::SetTestClockNowMs(0.0);
        XINPUT_STATE s = MakeState(0, 0, 200); // ボタン無し、RT押下
        XFireEngine::Apply(0, &s);          // ON区間だが heldButtons=0
        Check(s.Gamepad.wButtons == 0, "T10: no held target -> no buttons forced");
    }

    // T11: wButtons が前回出力と異なれば dwPacketNumber が進む(差分型ゲーム向け)
    {
        XFireEngine::ResetControllerState();
        XFireEngine::SetTestClockNowMs(0.0);
        XINPUT_STATE s = MakeState(XINPUT_GAMEPAD_A, 0, 200);
        s.dwPacketNumber = 100;
        XFireEngine::Apply(0, &s); // ON: A押(prev=0) -> 変化 -> 101
        Check(s.dwPacketNumber == 101, "T11: first ON advances packet");
        XFireEngine::SetTestClockNowMs(10.0);
        XINPUT_STATE s2 = MakeState(XINPUT_GAMEPAD_A, 0, 200);
        s2.dwPacketNumber = 101;
        XFireEngine::Apply(0, &s2); // ON継続(prev=A) -> 変化なし -> 101
        Check(s2.dwPacketNumber == 101, "T11b: same ON keeps packet");
        XFireEngine::SetTestClockNowMs(60.0);
        XINPUT_STATE s3 = MakeState(XINPUT_GAMEPAD_A, 0, 200);
        s3.dwPacketNumber = 101;
        XFireEngine::Apply(0, &s3); // OFF: A離(prev=A) -> 変化 -> 102
        Check(s3.dwPacketNumber == 102, "T11c: OFF advances packet");
    }

    // T12: 複数対象ボタン(A+B)が連動トグル、対象外(LB)は保護
    {
        XFireEngine::ResetControllerState();
        XFireEngine::SetTestClockNowMs(0.0);
        XINPUT_STATE s = MakeState(XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_LEFT_SHOULDER, 0, 200);
        XFireEngine::Apply(0, &s); // ON: A,B 押(LB 対象外)
        Check((s.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0 && (s.Gamepad.wButtons & XINPUT_GAMEPAD_B) != 0, "T12: A,B pressed in ON");
        Check((s.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0, "T12b: LB non-target stays in ON");
        XFireEngine::SetTestClockNowMs(60.0);
        XINPUT_STATE s2 = MakeState(XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_LEFT_SHOULDER, 0, 200);
        XFireEngine::Apply(0, &s2); // OFF: A,B 離(LB 維持)
        Check((s2.Gamepad.wButtons & XINPUT_GAMEPAD_A) == 0 && (s2.Gamepad.wButtons & XINPUT_GAMEPAD_B) == 0, "T12c: A,B released in OFF");
        Check((s2.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0, "T12d: LB non-target stays in OFF");
    }

    // T13: 長時間未呼出後も while ループで位相が追いつく
    {
        XFireEngine::ResetControllerState();
        XFireEngine::SetTestClockNowMs(0.0);
        XINPUT_STATE s = MakeState(XINPUT_GAMEPAD_A, 0, 200);
        XFireEngine::Apply(0, &s); // ON(0-50)
        XFireEngine::SetTestClockNowMs(260.0);
        XINPUT_STATE s2 = MakeState(XINPUT_GAMEPAD_A, 0, 200);
        XFireEngine::Apply(0, &s2); // 260ms: ON/OFFx... -> OFF(250-300)
        Check((s2.Gamepad.wButtons & XINPUT_GAMEPAD_A) == 0, "T13: long gap catches up to OFF");
        XFireEngine::SetTestClockNowMs(270.0);
        XINPUT_STATE s3 = MakeState(XINPUT_GAMEPAD_A, 0, 200);
        XFireEngine::Apply(0, &s3); // まだ OFF(250-300)
        Check((s3.Gamepad.wButtons & XINPUT_GAMEPAD_A) == 0, "T13b: still OFF");
        XFireEngine::SetTestClockNowMs(305.0);
        XINPUT_STATE s4 = MakeState(XINPUT_GAMEPAD_A, 0, 200);
        XFireEngine::Apply(0, &s4); // ON(300-350)
        Check((s4.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0, "T13c: ON after catch-up");
    }

    // T14: LT+RT 同時押下でも発動(max 値判定)
    {
        XFireEngine::ResetControllerState();
        XFireEngine::SetTestClockNowMs(0.0);
        XINPUT_STATE s = MakeState(XINPUT_GAMEPAD_A, 130, 200);
        XFireEngine::Apply(0, &s);
        Check((s.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0, "T14: LT+RT both trigger ON");
    }

    // T8c: OFF区間中にトリガ離上 -> 対象ボタンは物理状態に戻る(連射停止)
    {
        XFireEngine::ResetControllerState();
        XFireEngine::SetTestClockNowMs(0.0);
        XINPUT_STATE s = MakeState(XINPUT_GAMEPAD_A, 0, 200);
        XFireEngine::Apply(0, &s);          // ON
        XFireEngine::SetTestClockNowMs(60.0);
        XINPUT_STATE s2 = MakeState(XINPUT_GAMEPAD_A, 0, 200);
        XFireEngine::Apply(0, &s2);         // OFF: A離
        Check((s2.Gamepad.wButtons & XINPUT_GAMEPAD_A) == 0, "T8c setup: OFF releases A");
        XINPUT_STATE s3 = MakeState(XINPUT_GAMEPAD_A, 0, 0); // トリガ0、物理A押下
        XFireEngine::Apply(0, &s3);         // released -> 物理 A
        Check((s3.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0, "T8c: released during OFF -> physical A restored");
    }

    // T15: マスタートグル — OFF時はトリガ押下でも連射せず、コンボ(LB|A)でON→連射、再コンボでOFF
    {
        XFireConfig tcfg;
        Config::ApplyDefaults(tcfg);
        tcfg.onMs = 50; tcfg.offMs = 50;
        tcfg.firstOnMs = 0;
        tcfg.triggerThreshold = 128; tcfg.hysteresisLow = 64;
        tcfg.toggleButtons = XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_A; // LB|A
        tcfg.announceEnabled = false;
        Config::SetForTest(tcfg);
        XFireEngine::SetMasterEnabled(false); // 起動時 OFF 相当
        XFireEngine::ResetControllerState();
        XFireEngine::SetTestClockNowMs(0.0);

        // OFF時: RT押下+A押下 -> 連射せず物理Aのまま
        XINPUT_STATE a = MakeState(XINPUT_GAMEPAD_A, 0, 200);
        XFireEngine::Apply(0, &a);
        Check(a.Gamepad.wButtons == XINPUT_GAMEPAD_A, "T15: master OFF -> no xfire (A stays physical)");

        // コンボ LB|A 立ち上がり -> ON(このフレームは素通し)
        XINPUT_STATE b = MakeState(XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_A, 0, 0);
        XFireEngine::Apply(0, &b);
        Check(b.Gamepad.wButtons == (XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_A), "T15b: combo frame passthrough");

        // ON時: RT押下+A押下 -> ON区間でA押下維持(連射発動)
        XFireEngine::ResetControllerState(); // フェーズ状態リセット(ON開始直後をON区間に)
        XFireEngine::SetTestClockNowMs(0.0);
        XINPUT_STATE c = MakeState(XINPUT_GAMEPAD_A, 0, 200);
        XFireEngine::Apply(0, &c);
        Check((c.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0, "T15c: master ON -> xfire ON phase");

        // 再コンボ LB|A 立ち上がり -> OFF
        XINPUT_STATE d = MakeState(XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_A, 0, 0);
        XFireEngine::Apply(0, &d);
        Check(d.Gamepad.wButtons == (XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_A), "T15d: combo toggles OFF passthrough");

        // OFF時: RT押下+A押下 -> 再び連射せず物理Aのまま
        XFireEngine::ResetControllerState();
        XFireEngine::SetTestClockNowMs(0.0);
        XINPUT_STATE e = MakeState(XINPUT_GAMEPAD_A, 0, 200);
        XFireEngine::Apply(0, &e);
        Check(e.Gamepad.wButtons == XINPUT_GAMEPAD_A, "T15e: master OFF again -> no xfire");

        // 後続テストのために元の設定へ戻す
        Config::SetForTest(cfg);
        XFireEngine::SetMasterEnabled(true);
    }

    // T16: FirstOnMs=200 -> 最初のONは200ms、以降はOnMs(50)/OffMs(50)サイクル
    {
        XFireConfig fcfg;
        Config::ApplyDefaults(fcfg);
        fcfg.onMs = 50; fcfg.offMs = 50; fcfg.firstOnMs = 200;
        fcfg.toggleButtons = 0; fcfg.announceEnabled = false;
        Config::SetForTest(fcfg);
        XFireEngine::ResetControllerState();

        XFireEngine::SetTestClockNowMs(0.0);
        XINPUT_STATE s0 = MakeState(XINPUT_GAMEPAD_A, 0, 200);
        XFireEngine::Apply(0, &s0);                 // t=0  最初のON(0-200)
        Check((s0.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0, "T16: t=0 first ON");

        XFireEngine::SetTestClockNowMs(100.0);
        XINPUT_STATE s1 = MakeState(XINPUT_GAMEPAD_A, 0, 200);
        XFireEngine::Apply(0, &s1);                 // t=100 まだFirstOnMs内
        Check((s1.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0, "T16: t=100 still first ON");

        XFireEngine::SetTestClockNowMs(210.0);
        XINPUT_STATE s2 = MakeState(XINPUT_GAMEPAD_A, 0, 200);
        XFireEngine::Apply(0, &s2);                 // t=210 FirstOnMs終了->OFF(200-250)
        Check((s2.Gamepad.wButtons & XINPUT_GAMEPAD_A) == 0, "T16: t=210 OFF after first ON");

        XFireEngine::SetTestClockNowMs(260.0);
        XINPUT_STATE s3 = MakeState(XINPUT_GAMEPAD_A, 0, 200);
        XFireEngine::Apply(0, &s3);                 // t=260 ON(OnMs 250-300)
        Check((s3.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0, "T16: t=260 normal ON (OnMs)");

        // 後続テストのために元の設定へ戻す
        Config::SetForTest(cfg);
    }

    // T17: 対象ボタン離で位相リセット -> 次回押下で再びFirstOnMsから
    {
        XFireConfig fcfg;
        Config::ApplyDefaults(fcfg);
        fcfg.onMs = 50; fcfg.offMs = 50; fcfg.firstOnMs = 200;
        fcfg.toggleButtons = 0; fcfg.announceEnabled = false;
        Config::SetForTest(fcfg);
        XFireEngine::ResetControllerState();

        XFireEngine::SetTestClockNowMs(0.0);
        XINPUT_STATE s0 = MakeState(XINPUT_GAMEPAD_A, 0, 200);
        XFireEngine::Apply(0, &s0);                 // t=0 最初のON開始
        Check((s0.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0, "T17: t=0 first ON start");

        // t=50 でAを離す(トリガは押したまま) -> 位相リセット
        XFireEngine::SetTestClockNowMs(50.0);
        XINPUT_STATE s1 = MakeState(0, 0, 200);         // A離、RT押
        XFireEngine::Apply(0, &s1);
        Check(s1.Gamepad.wButtons == 0, "T17: t=50 A released -> no buttons");

        // t=60 でA再押下 -> 新たにFirstOnMs(200)から再スタート
        XFireEngine::SetTestClockNowMs(60.0);
        XINPUT_STATE s2 = MakeState(XINPUT_GAMEPAD_A, 0, 200);
        XFireEngine::Apply(0, &s2);                 // t=60 新FirstOnMs開始(60-260)
        Check((s2.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0, "T17: t=60 re-press restarts first ON");

        // t=250: 古いクロックなら既にOFFはずだが、新クロック(60基準)ではまだFirstOnMs内
        XFireEngine::SetTestClockNowMs(250.0);
        XINPUT_STATE s3 = MakeState(XINPUT_GAMEPAD_A, 0, 200);
        XFireEngine::Apply(0, &s3);                 // 250-60=190 < 200 -> まだON
        Check((s3.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0, "T17: t=250 still first ON (re-anchored)");

        // 後続テストのために元の設定へ戻す
        Config::SetForTest(cfg);
    }

    // T18: FirstOnMs=0(無効) -> 最初のONはOnMs(50)と同値
    {
        XFireConfig fcfg;
        Config::ApplyDefaults(fcfg);
        fcfg.onMs = 50; fcfg.offMs = 50; fcfg.firstOnMs = 0;
        fcfg.toggleButtons = 0; fcfg.announceEnabled = false;
        Config::SetForTest(fcfg);
        XFireEngine::ResetControllerState();

        XFireEngine::SetTestClockNowMs(0.0);
        XINPUT_STATE s0 = MakeState(XINPUT_GAMEPAD_A, 0, 200);
        XFireEngine::Apply(0, &s0);                 // t=0 ON(0-50)
        Check((s0.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0, "T18: t=0 ON");

        XFireEngine::SetTestClockNowMs(60.0);       // OnMs=50 なので t=60 はOFF
        XINPUT_STATE s1 = MakeState(XINPUT_GAMEPAD_A, 0, 200);
        XFireEngine::Apply(0, &s1);
        Check((s1.Gamepad.wButtons & XINPUT_GAMEPAD_A) == 0, "T18: t=60 OFF (first ON == OnMs)");

        // 後続テストのために元の設定へ戻す
        Config::SetForTest(cfg);
    }

    // T19: FirstOnMs の既定値は 200(出荷デフォルトの回帰防止)
    {
        XFireConfig dcfg;
        Config::ApplyDefaults(dcfg);
        Check(dcfg.firstOnMs == 200, "T19: default FirstOnMs == 200");
    }

    // T20: FormatButtons は ParseTargetButtons の逆(ラウンドトリップ)
    {
        XFireConfig dcfg;
        Config::ApplyDefaults(dcfg);
        // 既定 toggleButtons(LB|A) を文字列化→再解析で同一ビットマスクに戻る
        wchar_t buf[256] = {0};
        InputTransform::FormatButtons(dcfg.toggleButtons, buf, 256);
        Check(buf[0] != L'\0', "T20: FormatButtons(toggle default) non-empty");
        WORD round = InputTransform::ParseTargetButtons(buf);
        Check(round == dcfg.toggleButtons, "T20: toggle default round-trips via Format/Parse");
        // 既定 targetButtons(DPAD全方向+ABXY) もラウンドトリップ
        InputTransform::FormatButtons(dcfg.targetButtons, buf, 256);
        Check(InputTransform::ParseTargetButtons(buf) == dcfg.targetButtons, "T20b: target default round-trips");
        // mask==0 は空文字列
        InputTransform::FormatButtons(0, buf, 256);
        Check(buf[0] == L'\0', "T20c: FormatButtons(0) -> empty");
    }

    // T21: 値域ポリシー定数は期待値(クランプ範囲の回帰防止)
    {
        Check(Config::kOnMsMin == 1, "T21: kOnMsMin == 1");
        Check(Config::kOnMsMax == 10000, "T21b: kOnMsMax == 10000");
        Check(Config::kFirstOnMsMin == 0, "T21c: kFirstOnMsMin == 0");
        Check(Config::kByteMax == 255, "T21d: kByteMax == 255");
    }

    // ---- Config::LoadOnce 経路の検証(従来は SetForTest でバイパスされ未検証) ----
    // 一時 ini を書き、SetIniPathForTest + ResetForTest + LoadOnce で解析パスを通す。
    auto tempIniPath = [](const wchar_t* name) -> std::wstring {
        wchar_t tmp[MAX_PATH] = {0};
        DWORD n = GetTempPathW(MAX_PATH, tmp);
        std::wstring p(tmp, n);
        p += name;
        return p;
    };
    auto loadFromIni = [&](const std::wstring& path) -> const XFireConfig& {
        Config::SetIniPathForTest(path.c_str());
        Config::ResetForTest();
        Config::LoadOnce();
        return Config::Get();
    };

    // T22: 範囲外/非数値はクランプ/既定(安全不変条件・無限ループ防止の直接検証)
    {
        std::wstring path = tempIniPath(L"xfire_t22.ini");
        WritePrivateProfileStringW(L"XFire", L"OnMs", L"0", path.c_str());
        WritePrivateProfileStringW(L"XFire", L"OffMs", L"999999", path.c_str());
        WritePrivateProfileStringW(L"XFire", L"FirstOnMs", L"-1", path.c_str());
        WritePrivateProfileStringW(L"XFire", L"TriggerThreshold", L"999", path.c_str());
        WritePrivateProfileStringW(L"XFire", L"HysteresisLow", L"-5", path.c_str());
        const XFireConfig& c = loadFromIni(path);
        Check(c.onMs == 1, "T22: OnMs=0 -> clamped to 1");
        Check(c.offMs == 10000, "T22b: OffMs=999999 -> clamped to 10000");
        Check(c.firstOnMs == 0, "T22c: FirstOnMs=-1 -> clamped to 0");
        Check(c.triggerThreshold == 255, "T22d: TriggerThreshold=999 -> clamped to 255");
        Check(c.hysteresisLow == 0, "T22e: HysteresisLow=-5 -> clamped to 0");
        DeleteFileW(path.c_str());
    }

    // T22f: 非数値値は既定を採用(従来は 0 -> クランプ最小に黙って変換されていた)
    {
        std::wstring path = tempIniPath(L"xfire_t22f.ini");
        WritePrivateProfileStringW(L"XFire", L"OnMs", L"fast", path.c_str());
        const XFireConfig& c = loadFromIni(path);
        Check(c.onMs == 50, "T22f: OnMs=fast (non-numeric) -> default 50");
        DeleteFileW(path.c_str());
    }

    // T22g: 空値は既定を採用(従来は 0 -> クランプ最小になり TriggerThreshold= 空が
    //       即トリガ発動になっていた silent failure の直接回帰防止)
    {
        std::wstring path = tempIniPath(L"xfire_t22g.ini");
        WritePrivateProfileStringW(L"XFire", L"TriggerThreshold", L"", path.c_str());
        WritePrivateProfileStringW(L"XFire", L"OnMs", L"", path.c_str());
        const XFireConfig& c = loadFromIni(path);
        Check(c.triggerThreshold == 150, "T22g: TriggerThreshold= empty -> default 150 (not 0)");
        Check(c.onMs == 50, "T22g2: OnMs= empty -> default 50 (not clamped to 1)");
        DeleteFileW(path.c_str());
    }

    // T23: ToggleButtons キー不在 -> 既定 LB|A
    {
        std::wstring path = tempIniPath(L"xfire_t23.ini");
        WritePrivateProfileStringW(L"XFire", L"OnMs", L"50", path.c_str()); // ToggleButtons は書かない
        const XFireConfig& c = loadFromIni(path);
        Check(c.toggleButtons == (XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_A), "T23: ToggleButtons absent -> default LB|A");
        DeleteFileW(path.c_str());
    }

    // T24: ToggleButtons= 空 -> 0(トグル無効化・常時ON)
    {
        std::wstring path = tempIniPath(L"xfire_t24.ini");
        WritePrivateProfileStringW(L"XFire", L"ToggleButtons", L"", path.c_str());
        const XFireConfig& c = loadFromIni(path);
        Check(c.toggleButtons == 0, "T24: ToggleButtons= empty -> 0 (toggle disabled)");
        DeleteFileW(path.c_str());
    }

    // T25: TargetButtons= 空 -> デフォルト維持(DPAD全方向+ABXY)
    {
        std::wstring path = tempIniPath(L"xfire_t25.ini");
        WritePrivateProfileStringW(L"XFire", L"TargetButtons", L"", path.c_str());
        const XFireConfig& c = loadFromIni(path);
        WORD def = XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_LEFT
                 | XINPUT_GAMEPAD_DPAD_RIGHT | XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B
                 | XINPUT_GAMEPAD_X | XINPUT_GAMEPAD_Y;
        Check(c.targetButtons == def, "T25: TargetButtons= empty -> default maintained");
        DeleteFileW(path.c_str());
    }

    // T26: ToggleButtons=FOO(全トークン不明) -> 既定維持(不明名は無視・既定コンボが有効なまま)
    {
        std::wstring path = tempIniPath(L"xfire_t26.ini");
        WritePrivateProfileStringW(L"XFire", L"ToggleButtons", L"FOO", path.c_str());
        const XFireConfig& c = loadFromIni(path);
        Check(c.toggleButtons == (XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_A), "T26: ToggleButtons=FOO -> default maintained");
        DeleteFileW(path.c_str());
    }

    // T27: ParseTargetButtons は不明トークンを無視しつつ unknownCount で報告
    {
        int unk = 0;
        WORD m = InputTransform::ParseTargetButtons(L"A|FOO|B", &unk);
        Check(m == (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B), "T27: A|FOO|B -> A|B (FOO ignored)");
        Check(unk == 1, "T27b: unknown count == 1");
        // 空白/ケース耐性 + 不明複数
        int unk2 = 0;
        WORD m2 = InputTransform::ParseTargetButtons(L" a | xyz | b ", &unk2);
        Check(m2 == (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B), "T27c: ' a | xyz | b ' -> A|B (trim/case)");
        Check(unk2 == 1, "T27d: unknown count == 1 (xyz)");
    }

    // T28: ApplyDefaults の全既定値(出荷デフォルトの回帰防止)
    {
        XFireConfig d;
        Config::ApplyDefaults(d);
        Check(d.onMs == 50, "T28: default onMs == 50");
        Check(d.offMs == 50, "T28b: default offMs == 50");
        Check(d.firstOnMs == 200, "T28c: default firstOnMs == 200");
        Check(d.triggerThreshold == 150, "T28d: default triggerThreshold == 150");
        Check(d.hysteresisLow == 140, "T28e: default hysteresisLow == 140");
        Check(d.enableLT && d.enableRT, "T28f: default enableLT/RT true");
        Check(d.toggleButtons == (XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_A), "T28g: default toggleButtons LB|A");
        Check(d.defaultEnabled == false, "T28h: default defaultEnabled false");
        Check(d.announceEnabled == true, "T28i: default announceEnabled true");
        Check(d.startupSound == true, "T28j: default startupSound true");
        WORD def = XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_LEFT
                 | XINPUT_GAMEPAD_DPAD_RIGHT | XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B
                 | XINPUT_GAMEPAD_X | XINPUT_GAMEPAD_Y;
        Check(d.targetButtons == def, "T28k: default targetButtons DPAD+ABXY");
        // [RapidFire] 既定値
        Check(d.buttonFirstOnMs == 500, "T28l: default buttonFirstOnMs == 500");
        Check(d.buttonOnMs == 50, "T28m: default buttonOnMs == 50");
        Check(d.buttonOffMs == 50, "T28n: default buttonOffMs == 50");
        Check(d.buttonTargetButtons == XINPUT_GAMEPAD_A, "T28o: default buttonTargetButtons == A");
    }

    // T29: 有効値は LoadOnce 経由で正しく設定される(従来テストは「既定維持」パスのみ)
    {
        std::wstring path = tempIniPath(L"xfire_t29.ini");
        WritePrivateProfileStringW(L"XFire", L"ToggleButtons", L"A|B", path.c_str());
        WritePrivateProfileStringW(L"XFire", L"TargetButtons", L"X|Y", path.c_str());
        WritePrivateProfileStringW(L"XFire", L"OffMs", L"0", path.c_str());       // 下限クランプ
        WritePrivateProfileStringW(L"XFire", L"FirstOnMs", L"999999", path.c_str()); // 上限クランプ
        const XFireConfig& c = loadFromIni(path);
        Check(c.toggleButtons == (XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B), "T29: ToggleButtons=A|B -> A|B via LoadOnce");
        Check(c.targetButtons == (XINPUT_GAMEPAD_X | XINPUT_GAMEPAD_Y), "T29b: TargetButtons=X|Y -> X|Y via LoadOnce");
        Check(c.offMs == 1, "T29c: OffMs=0 -> clamped to 1");
        Check(c.firstOnMs == 10000, "T29d: FirstOnMs=999999 -> clamped to 10000");
        DeleteFileW(path.c_str());
    }

    // T30: bool 項目は 1/0/true/false/yes/no を受理
    {
        std::wstring path = tempIniPath(L"xfire_t30.ini");
        WritePrivateProfileStringW(L"XFire", L"EnableLT", L"0", path.c_str());
        WritePrivateProfileStringW(L"XFire", L"EnableRT", L"false", path.c_str());
        WritePrivateProfileStringW(L"XFire", L"AnnounceEnabled", L"yes", path.c_str());
        WritePrivateProfileStringW(L"XFire", L"StartupSound", L"no", path.c_str());
        const XFireConfig& c = loadFromIni(path);
        Check(c.enableLT == false, "T30: EnableLT=0 -> false");
        Check(c.enableRT == false, "T30b: EnableRT=false -> false");
        Check(c.announceEnabled == true, "T30c: AnnounceEnabled=yes -> true");
        Check(c.startupSound == false, "T30d: StartupSound=no -> false");
        DeleteFileW(path.c_str());
    }

    // T30e: bool 不明値は既定(EnableLT/RT 既定=true)。EnableRT 側も対称的に検証。
    {
        std::wstring path = tempIniPath(L"xfire_t30e.ini");
        WritePrivateProfileStringW(L"XFire", L"EnableLT", L"maybe", path.c_str());
        WritePrivateProfileStringW(L"XFire", L"EnableRT", L"maybe", path.c_str());
        const XFireConfig& c = loadFromIni(path);
        Check(c.enableLT == true, "T30e: EnableLT=maybe (unknown) -> default true");
        Check(c.enableRT == true, "T30e2: EnableRT=maybe (unknown) -> default true");
        DeleteFileW(path.c_str());
    }

    // T31: クリーンブレイク — 廃止キー EnableL2/EnableR2 は値を読まず無視(新キー不在=>既定 true)。
    // 旧キーに 0(無効) を書いても新キーが不在なら既定(true)のまま。旧キーのフォールバック読込が
    // 再導入されるとこのテストが失敗する(リネームの破壊的変更の回帰防止)。
    {
        std::wstring path = tempIniPath(L"xfire_t31.ini");
        WritePrivateProfileStringW(L"XFire", L"EnableL2", L"0", path.c_str());
        WritePrivateProfileStringW(L"XFire", L"EnableR2", L"0", path.c_str());
        const XFireConfig& c = loadFromIni(path);
        Check(c.enableLT == true, "T31: removed EnableL2=0 ignored -> enableLT default true");
        Check(c.enableRT == true, "T31b: removed EnableR2=0 ignored -> enableRT default true");
        DeleteFileW(path.c_str());
    }

    // T32: エンジンは enableLT/RT フラグでトリガを個別にゲートする(フラグ=false の側は発動しない)。
    // 既定config(両方true)で発動するのは既存テスト(T2/T3)で担保済み。ここでは抑制側を検証:
    // フラグ=false のトリガを押しっ放しでも OFF 区間相当(t=60)で対象ボタン(A)が物理押下のまま
    // トグルしないこと(=連射未発動)を確認する。フラグガードが削られると LT/RT が閾値を超えて
    // 連射発動し A が OFF 区間で強制離されるため、このテストが失敗する。
    {
        XFireConfig tcfg;
        Config::ApplyDefaults(tcfg);
        tcfg.onMs = 50; tcfg.offMs = 50; tcfg.firstOnMs = 0;
        tcfg.triggerThreshold = 128; tcfg.hysteresisLow = 64;
        tcfg.toggleButtons = 0; tcfg.announceEnabled = false;
        tcfg.buttonTargetButtons = 0; // T32 はトリガガート検証。ボタン単独モードは無効化
        XFireEngine::SetMasterEnabled(true);

        // enableLT=false -> LT=200+物理A で連射せず。t=60(OFF区間相当)でも A は物理押下のまま。
        tcfg.enableLT = false; tcfg.enableRT = true;
        Config::SetForTest(tcfg);
        XFireEngine::ResetControllerState();
        XFireEngine::SetTestClockNowMs(0.0);
        XINPUT_STATE a0 = MakeState(XINPUT_GAMEPAD_A, 200, 0);
        XFireEngine::Apply(0, &a0);                 // t=0
        XFireEngine::SetTestClockNowMs(60.0);
        XINPUT_STATE a1 = MakeState(XINPUT_GAMEPAD_A, 200, 0);
        XFireEngine::Apply(0, &a1);                 // t=60
        Check((a1.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0, "T32: enableLT=false -> LT suppressed, A stays physical at OFF phase");

        // enableRT=false -> RT=200+物理A で連射せず。t=60(OFF区間相当)でも A は物理押下のまま。
        tcfg.enableLT = true; tcfg.enableRT = false;
        Config::SetForTest(tcfg);
        XFireEngine::ResetControllerState();
        XFireEngine::SetTestClockNowMs(0.0);
        XINPUT_STATE b0 = MakeState(XINPUT_GAMEPAD_A, 0, 200);
        XFireEngine::Apply(0, &b0);                 // t=0
        XFireEngine::SetTestClockNowMs(60.0);
        XINPUT_STATE b1 = MakeState(XINPUT_GAMEPAD_A, 0, 200);
        XFireEngine::Apply(0, &b1);                 // t=60
        Check((b1.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0, "T32b: enableRT=false -> RT suppressed, A stays physical at OFF phase");
    }

    // T30f: bool true/1 受理(従来 EnableLT=true は 0->false になっていた silent failure の回帰防止)
    {
        std::wstring path = tempIniPath(L"xfire_t30f.ini");
        WritePrivateProfileStringW(L"XFire", L"EnableLT", L"true", path.c_str());
        WritePrivateProfileStringW(L"XFire", L"EnableRT", L"1", path.c_str());
        const XFireConfig& c = loadFromIni(path);
        Check(c.enableLT == true, "T30f: EnableLT=true -> true (was silently false)");
        Check(c.enableRT == true, "T30g: EnableRT=1 -> true");
        DeleteFileW(path.c_str());
    }

    // ---- ボタン単独連射([RapidFire])の検証 ----
    // 共通: トリガモードと分離するため targetButtons=0(トリガ連射対象なし)。
    //       buttonTargetButtons でボタン単独対象を指定。トグル無効・音声OFF。

    // T33: ボタン単独 A(トリガなし) -> FirstOnMs=500 初回ON、以降 50/50 サイクル
    {
        XFireConfig bcfg;
        Config::ApplyDefaults(bcfg);
        bcfg.onMs = 50; bcfg.offMs = 50; bcfg.firstOnMs = 0;
        bcfg.triggerThreshold = 128; bcfg.hysteresisLow = 64;
        bcfg.toggleButtons = 0; bcfg.announceEnabled = false;
        bcfg.targetButtons = 0;          // トリガモード無効化
        bcfg.buttonTargetButtons = XINPUT_GAMEPAD_A;
        bcfg.buttonFirstOnMs = 500; bcfg.buttonOnMs = 50; bcfg.buttonOffMs = 50;
        Config::SetForTest(bcfg);
        XFireEngine::SetMasterEnabled(true);
        XFireEngine::ResetControllerState();

        XFireEngine::SetTestClockNowMs(0.0);
        XINPUT_STATE s0 = MakeState(XINPUT_GAMEPAD_A, 0, 0); // トリガなし
        XFireEngine::Apply(0, &s0);                         // t=0 最初のON(0-500)
        Check((s0.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0, "T33: t=0 first ON (button-only)");

        XFireEngine::SetTestClockNowMs(100.0);
        XINPUT_STATE s1 = MakeState(XINPUT_GAMEPAD_A, 0, 0);
        XFireEngine::Apply(0, &s1);                         // t=100 まだ buttonFirstOnMs 内
        Check((s1.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0, "T33b: t=100 still first ON");

        XFireEngine::SetTestClockNowMs(510.0);
        XINPUT_STATE s2 = MakeState(XINPUT_GAMEPAD_A, 0, 0);
        XFireEngine::Apply(0, &s2);                         // t=510 FirstOnMs終了->OFF(500-550)
        Check((s2.Gamepad.wButtons & XINPUT_GAMEPAD_A) == 0, "T33c: t=510 OFF after first ON");

        XFireEngine::SetTestClockNowMs(560.0);
        XINPUT_STATE s3 = MakeState(XINPUT_GAMEPAD_A, 0, 0);
        XFireEngine::Apply(0, &s3);                         // t=560 ON(550-600)
        Check((s3.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0, "T33d: t=560 normal ON (buttonOnMs)");

        Config::SetForTest(cfg);
    }

    // T34: 重複なし B(ボタン単独リストのみ)はトリガ押下中も連射継続(他は独立)
    {
        XFireConfig bcfg;
        Config::ApplyDefaults(bcfg);
        bcfg.onMs = 50; bcfg.offMs = 50; bcfg.firstOnMs = 0;
        bcfg.triggerThreshold = 128; bcfg.hysteresisLow = 64;
        bcfg.toggleButtons = 0; bcfg.announceEnabled = false;
        bcfg.targetButtons = XINPUT_GAMEPAD_A;       // トリガリスト=A のみ(B と重複なし)
        bcfg.buttonTargetButtons = XINPUT_GAMEPAD_B;  // ボタン単独リスト=B のみ
        bcfg.buttonFirstOnMs = 0; bcfg.buttonOnMs = 50; bcfg.buttonOffMs = 50;
        Config::SetForTest(bcfg);
        XFireEngine::SetMasterEnabled(true);
        XFireEngine::ResetControllerState();

        // RT 押下(トリガ活性)+ B 押下。B はトリガリストに入らないのでボタン単独モードが駆動。
        XFireEngine::SetTestClockNowMs(0.0);
        XINPUT_STATE s0 = MakeState(XINPUT_GAMEPAD_B, 0, 200);
        XFireEngine::Apply(0, &s0);                         // t=0 ON(buttonOnMs 0-50)
        Check((s0.Gamepad.wButtons & XINPUT_GAMEPAD_B) != 0, "T34: t=0 B ON (button-only despite trigger)");

        XFireEngine::SetTestClockNowMs(60.0);
        XINPUT_STATE s1 = MakeState(XINPUT_GAMEPAD_B, 0, 200);
        XFireEngine::Apply(0, &s1);                         // t=60 OFF(50-100)
        Check((s1.Gamepad.wButtons & XINPUT_GAMEPAD_B) == 0, "T34b: t=60 B OFF (button-only continues cycling)");

        Config::SetForTest(cfg);
    }

    // T35: 重複 A(両リスト)・トリガ押下 -> トリガモード(FirstOnMs=200)が駆動・ボタンクロックはAをスキップ
    {
        XFireConfig bcfg;
        Config::ApplyDefaults(bcfg);
        bcfg.onMs = 50; bcfg.offMs = 50; bcfg.firstOnMs = 200;  // トリガ初回ON=200
        bcfg.triggerThreshold = 128; bcfg.hysteresisLow = 64;
        bcfg.toggleButtons = 0; bcfg.announceEnabled = false;
        bcfg.targetButtons = XINPUT_GAMEPAD_A;        // トリガリスト=A
        bcfg.buttonTargetButtons = XINPUT_GAMEPAD_A;  // ボタン単独リスト=A(重複)
        bcfg.buttonFirstOnMs = 500; bcfg.buttonOnMs = 50; bcfg.buttonOffMs = 50; // ボタン初回ON=500
        Config::SetForTest(bcfg);
        XFireEngine::SetMasterEnabled(true);
        XFireEngine::ResetControllerState();

        XFireEngine::SetTestClockNowMs(0.0);
        XINPUT_STATE s0 = MakeState(XINPUT_GAMEPAD_A, 0, 200); // トリガ活性
        XFireEngine::Apply(0, &s0);                             // t=0 トリガ初回ON(0-200)
        Check((s0.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0, "T35: t=0 ON (trigger owns overlap)");

        XFireEngine::SetTestClockNowMs(100.0);
        XINPUT_STATE s1 = MakeState(XINPUT_GAMEPAD_A, 0, 200);
        XFireEngine::Apply(0, &s1);                             // t=100 まだトリガFirstOnMs(200)内
        Check((s1.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0, "T35b: t=100 still trigger first ON (200)");

        // t=210: トリガクロック(200)なら OFF。ボタンクロック(500)が駆動していたらまだON。
        //       A が離れていればトリガ優先が確認できる。
        XFireEngine::SetTestClockNowMs(210.0);
        XINPUT_STATE s2 = MakeState(XINPUT_GAMEPAD_A, 0, 200);
        XFireEngine::Apply(0, &s2);                             // t=210 トリガOFF(200-250)
        Check((s2.Gamepad.wButtons & XINPUT_GAMEPAD_A) == 0, "T35c: t=210 OFF -> trigger clock (200) owns A, not button clock (500)");

        Config::SetForTest(cfg);
    }

    // T36: 重複 A(両リスト)・トリガ離し -> ボタン単独モード(FirstOnMs=500)が駆動
    {
        XFireConfig bcfg;
        Config::ApplyDefaults(bcfg);
        bcfg.onMs = 50; bcfg.offMs = 50; bcfg.firstOnMs = 200;
        bcfg.triggerThreshold = 128; bcfg.hysteresisLow = 64;
        bcfg.toggleButtons = 0; bcfg.announceEnabled = false;
        bcfg.targetButtons = XINPUT_GAMEPAD_A;
        bcfg.buttonTargetButtons = XINPUT_GAMEPAD_A;
        bcfg.buttonFirstOnMs = 500; bcfg.buttonOnMs = 50; bcfg.buttonOffMs = 50;
        Config::SetForTest(bcfg);
        XFireEngine::SetMasterEnabled(true);
        XFireEngine::ResetControllerState();

        // トリガなし(RT=0<128 -> 非活性) + A 押下 -> ボタン単独モード(初回ON=500)
        XFireEngine::SetTestClockNowMs(0.0);
        XINPUT_STATE s0 = MakeState(XINPUT_GAMEPAD_A, 0, 0);
        XFireEngine::Apply(0, &s0);                         // t=0 ボタン初回ON(0-500)
        Check((s0.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0, "T36: t=0 button first ON (500)");

        // t=210: トリガクロック(200)なら OFF。ボタンクロック(500)ならまだON。
        XFireEngine::SetTestClockNowMs(210.0);
        XINPUT_STATE s1 = MakeState(XINPUT_GAMEPAD_A, 0, 0);
        XFireEngine::Apply(0, &s1);                          // t=210 まだボタンFirstOnMs内
        Check((s1.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0, "T36b: t=210 still button first ON (500) -> button clock owns A when trigger inactive");

        XFireEngine::SetTestClockNowMs(510.0);
        XINPUT_STATE s2 = MakeState(XINPUT_GAMEPAD_A, 0, 0);
        XFireEngine::Apply(0, &s2);                          // t=510 ボタンOFF(500-550)
        Check((s2.Gamepad.wButtons & XINPUT_GAMEPAD_A) == 0, "T36c: t=510 OFF (button clock cycle)");

        Config::SetForTest(cfg);
    }

    // T37: ボタン単独 FirstOnMs=0 -> 初回ONは buttonOnMs と同値
    {
        XFireConfig bcfg;
        Config::ApplyDefaults(bcfg);
        bcfg.onMs = 50; bcfg.offMs = 50; bcfg.firstOnMs = 0;
        bcfg.triggerThreshold = 128; bcfg.hysteresisLow = 64;
        bcfg.toggleButtons = 0; bcfg.announceEnabled = false;
        bcfg.targetButtons = 0;
        bcfg.buttonTargetButtons = XINPUT_GAMEPAD_A;
        bcfg.buttonFirstOnMs = 0; bcfg.buttonOnMs = 50; bcfg.buttonOffMs = 50;
        Config::SetForTest(bcfg);
        XFireEngine::SetMasterEnabled(true);
        XFireEngine::ResetControllerState();

        XFireEngine::SetTestClockNowMs(0.0);
        XINPUT_STATE s0 = MakeState(XINPUT_GAMEPAD_A, 0, 0);
        XFireEngine::Apply(0, &s0);                          // t=0 ON(0-50)
        Check((s0.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0, "T37: t=0 ON");

        XFireEngine::SetTestClockNowMs(60.0);                // buttonOnMs=50 -> t=60 は OFF
        XINPUT_STATE s1 = MakeState(XINPUT_GAMEPAD_A, 0, 0);
        XFireEngine::Apply(0, &s1);
        Check((s1.Gamepad.wButtons & XINPUT_GAMEPAD_A) == 0, "T37b: t=60 OFF (first ON == buttonOnMs)");

        Config::SetForTest(cfg);
    }

    // T38: ボタン単独も対象離で位相リセット -> 再押下で FirstOnMs から再スタート
    {
        XFireConfig bcfg;
        Config::ApplyDefaults(bcfg);
        bcfg.onMs = 50; bcfg.offMs = 50; bcfg.firstOnMs = 0;
        bcfg.triggerThreshold = 128; bcfg.hysteresisLow = 64;
        bcfg.toggleButtons = 0; bcfg.announceEnabled = false;
        bcfg.targetButtons = 0;
        bcfg.buttonTargetButtons = XINPUT_GAMEPAD_A;
        bcfg.buttonFirstOnMs = 500; bcfg.buttonOnMs = 50; bcfg.buttonOffMs = 50;
        Config::SetForTest(bcfg);
        XFireEngine::SetMasterEnabled(true);
        XFireEngine::ResetControllerState();

        XFireEngine::SetTestClockNowMs(0.0);
        XINPUT_STATE s0 = MakeState(XINPUT_GAMEPAD_A, 0, 0);
        XFireEngine::Apply(0, &s0);                          // t=0 最初のON開始(0-500)
        Check((s0.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0, "T38: t=0 first ON start");

        XFireEngine::SetTestClockNowMs(50.0);
        XINPUT_STATE s1 = MakeState(0, 0, 0);                // A 離す -> 位相リセット
        XFireEngine::Apply(0, &s1);
        Check(s1.Gamepad.wButtons == 0, "T38b: t=50 A released -> no buttons");

        XFireEngine::SetTestClockNowMs(60.0);
        XINPUT_STATE s2 = MakeState(XINPUT_GAMEPAD_A, 0, 0);
        XFireEngine::Apply(0, &s2);                          // t=60 新FirstOnMs開始(60-560)
        Check((s2.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0, "T38c: t=60 re-press restarts first ON");

        // t=550: 古いクロック(0基準)なら既にOFFだが新クロック(60基準)ではまだFirstOnMs内
        XFireEngine::SetTestClockNowMs(550.0);
        XINPUT_STATE s3 = MakeState(XINPUT_GAMEPAD_A, 0, 0);
        XFireEngine::Apply(0, &s3);                          // 550-60=490 < 500 -> まだON
        Check((s3.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0, "T38d: t=550 still first ON (re-anchored)");

        Config::SetForTest(cfg);
    }

    // T39: [RapidFire] LoadOnce 経路の検証(既定・クランプ・対象解析)
    {
        // T39: [RapidFire] セクション不在 -> 既定(500/50/50/A)
        {
            std::wstring path = tempIniPath(L"xfire_t39.ini");
            WritePrivateProfileStringW(L"XFire", L"OnMs", L"50", path.c_str()); // [RapidFire] は書かない
            const XFireConfig& c = loadFromIni(path);
            Check(c.buttonFirstOnMs == 500, "T39: RapidFire absent -> buttonFirstOnMs default 500");
            Check(c.buttonOnMs == 50, "T39b: RapidFire absent -> buttonOnMs default 50");
            Check(c.buttonOffMs == 50, "T39c: RapidFire absent -> buttonOffMs default 50");
            Check(c.buttonTargetButtons == XINPUT_GAMEPAD_A, "T39d: RapidFire absent -> buttonTargetButtons default A");
            DeleteFileW(path.c_str());
        }
        // T39e: 範囲外はクランプ
        {
            std::wstring path = tempIniPath(L"xfire_t39e.ini");
            WritePrivateProfileStringW(L"RapidFire", L"FirstOnMs", L"-1", path.c_str());
            WritePrivateProfileStringW(L"RapidFire", L"OnMs", L"0", path.c_str());
            WritePrivateProfileStringW(L"RapidFire", L"OffMs", L"999999", path.c_str());
            const XFireConfig& c = loadFromIni(path);
            Check(c.buttonFirstOnMs == 0, "T39e: RapidFire.FirstOnMs=-1 -> clamped to 0");
            Check(c.buttonOnMs == 1, "T39f: RapidFire.OnMs=0 -> clamped to 1");
            Check(c.buttonOffMs == 10000, "T39g: RapidFire.OffMs=999999 -> clamped to 10000");
            DeleteFileW(path.c_str());
        }
        // T39h: TargetButtons 解析(空=既定維持・有効値=反映)
        {
            std::wstring path = tempIniPath(L"xfire_t39h.ini");
            WritePrivateProfileStringW(L"RapidFire", L"TargetButtons", L"", path.c_str()); // 空->既定維持
            const XFireConfig& c = loadFromIni(path);
            Check(c.buttonTargetButtons == XINPUT_GAMEPAD_A, "T39h: RapidFire.TargetButtons empty -> default A maintained");
            DeleteFileW(path.c_str());
        }
        {
            std::wstring path = tempIniPath(L"xfire_t39i.ini");
            WritePrivateProfileStringW(L"RapidFire", L"TargetButtons", L"X|Y", path.c_str());
            const XFireConfig& c = loadFromIni(path);
            Check(c.buttonTargetButtons == (XINPUT_GAMEPAD_X | XINPUT_GAMEPAD_Y), "T39i: RapidFire.TargetButtons=X|Y -> X|Y");
            DeleteFileW(path.c_str());
        }
    }

    // T40: マスター無効時はボタン単独連射も停止(A は物理のまま)
    {
        XFireConfig bcfg;
        Config::ApplyDefaults(bcfg);
        bcfg.onMs = 50; bcfg.offMs = 50; bcfg.firstOnMs = 0;
        bcfg.triggerThreshold = 128; bcfg.hysteresisLow = 64;
        bcfg.toggleButtons = 0; bcfg.announceEnabled = false;
        bcfg.targetButtons = 0;
        bcfg.buttonTargetButtons = XINPUT_GAMEPAD_A;
        bcfg.buttonFirstOnMs = 0; bcfg.buttonOnMs = 50; bcfg.buttonOffMs = 50;
        Config::SetForTest(bcfg);
        XFireEngine::SetMasterEnabled(false); // マスターOFF
        XFireEngine::ResetControllerState();

        XFireEngine::SetTestClockNowMs(0.0);
        XINPUT_STATE s0 = MakeState(XINPUT_GAMEPAD_A, 0, 0);
        XFireEngine::Apply(0, &s0);                          // マスターOFF -> 素通し
        Check((s0.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0, "T40: master OFF -> A physical at t=0");

        XFireEngine::SetTestClockNowMs(60.0);                // ボタン単独なら OFF 区間で A が離れるはず
        XINPUT_STATE s1 = MakeState(XINPUT_GAMEPAD_A, 0, 0);
        XFireEngine::Apply(0, &s1);
        Check((s1.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0, "T40b: master OFF -> A still physical at t=60 (no button-only)");

        XFireEngine::SetMasterEnabled(true);
        Config::SetForTest(cfg);
    }

    // T41: ボタン単独連射の ON/OFF で dwPacketNumber が進む(差分型ゲーム向け)
    {
        XFireConfig bcfg;
        Config::ApplyDefaults(bcfg);
        bcfg.onMs = 50; bcfg.offMs = 50; bcfg.firstOnMs = 0;
        bcfg.triggerThreshold = 128; bcfg.hysteresisLow = 64;
        bcfg.toggleButtons = 0; bcfg.announceEnabled = false;
        bcfg.targetButtons = 0;
        bcfg.buttonTargetButtons = XINPUT_GAMEPAD_A;
        bcfg.buttonFirstOnMs = 0; bcfg.buttonOnMs = 50; bcfg.buttonOffMs = 50;
        Config::SetForTest(bcfg);
        XFireEngine::SetMasterEnabled(true);
        XFireEngine::ResetControllerState();

        XFireEngine::SetTestClockNowMs(0.0);
        XINPUT_STATE s0 = MakeState(XINPUT_GAMEPAD_A, 0, 0);
        s0.dwPacketNumber = 100;
        XFireEngine::Apply(0, &s0);                          // ON: A押(prev=0)->変化->101
        Check(s0.dwPacketNumber == 101, "T41: first ON advances packet");

        XFireEngine::SetTestClockNowMs(10.0);
        XINPUT_STATE s1 = MakeState(XINPUT_GAMEPAD_A, 0, 0);
        s1.dwPacketNumber = 101;
        XFireEngine::Apply(0, &s1);                          // ON継続(prev=A)->変化なし->101
        Check(s1.dwPacketNumber == 101, "T41b: same ON keeps packet");

        XFireEngine::SetTestClockNowMs(60.0);
        XINPUT_STATE s2 = MakeState(XINPUT_GAMEPAD_A, 0, 0);
        s2.dwPacketNumber = 101;
        XFireEngine::Apply(0, &s2);                          // OFF: A離(prev=A)->変化->102
        Check(s2.dwPacketNumber == 102, "T41c: OFF advances packet");

        Config::SetForTest(cfg);
    }

    // T42: マスタートグル OFF 時にボタン単独の位相状態もリセット -> 再ONで FirstOnMs から再開
    //      (修正前: bStarted が残存し再ON後に古い位相で catch-up され OFF 区間になる非決定性)
    {
        XFireConfig bcfg;
        Config::ApplyDefaults(bcfg);
        bcfg.onMs = 50; bcfg.offMs = 50; bcfg.firstOnMs = 0;
        bcfg.triggerThreshold = 128; bcfg.hysteresisLow = 64;
        bcfg.toggleButtons = XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_A; // LB|A でトグル
        bcfg.announceEnabled = false;
        bcfg.targetButtons = 0;
        bcfg.buttonTargetButtons = XINPUT_GAMEPAD_A;
        bcfg.buttonFirstOnMs = 500; bcfg.buttonOnMs = 50; bcfg.buttonOffMs = 50;
        Config::SetForTest(bcfg);
        XFireEngine::SetMasterEnabled(true);
        XFireEngine::ResetControllerState();

        // t=0: A 押下(トリガなし) -> ボタン単独 初回ON(0-500)
        XFireEngine::SetTestClockNowMs(0.0);
        XINPUT_STATE s0 = MakeState(XINPUT_GAMEPAD_A, 0, 0);
        XFireEngine::Apply(0, &s0);
        Check((s0.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0, "T42: t=0 button first ON");

        // t=510: FirstOnMs(500)経過 -> OFF(500-550)
        XFireEngine::SetTestClockNowMs(510.0);
        XINPUT_STATE s1 = MakeState(XINPUT_GAMEPAD_A, 0, 0);
        XFireEngine::Apply(0, &s1);
        Check((s1.Gamepad.wButtons & XINPUT_GAMEPAD_A) == 0, "T42b: t=510 OFF (phase=500-550)");

        // t=515: LB+A コンボ立ち上がり -> マスター OFF(このフレームは素通し)
        XFireEngine::SetTestClockNowMs(515.0);
        XINPUT_STATE s2 = MakeState(XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_A, 0, 0);
        XFireEngine::Apply(0, &s2);

        // t=520: A 押下・マスターOFF -> 素通し(A 物理押下のまま・連射しない)
        XFireEngine::SetTestClockNowMs(520.0);
        XINPUT_STATE s3 = MakeState(XINPUT_GAMEPAD_A, 0, 0);
        XFireEngine::Apply(0, &s3);
        Check((s3.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0, "T42c-pre: master OFF -> A physical (no button-only)");

        // t=525: 再び LB+A コンボ立ち上がり -> マスター ON(このフレームは素通し)
        XFireEngine::SetTestClockNowMs(525.0);
        XINPUT_STATE s4 = MakeState(XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_A, 0, 0);
        XFireEngine::Apply(0, &s4);

        // t=540: A 押下(トリガなし)・マスターON -> 修正後は bStarted リセット済みで
        //        新たに FirstOnMs(500) から再開(540-1040) -> A 押下。
        //        修正前は bStarted が残存し古い位相(500基準・OFF区間)で catch-up され A が離れる。
        XFireEngine::SetTestClockNowMs(540.0);
        XINPUT_STATE s5 = MakeState(XINPUT_GAMEPAD_A, 0, 0);
        XFireEngine::Apply(0, &s5);
        Check((s5.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0, "T42c: t=540 re-ON restarts button first ON (phase reset on master OFF)");

        // t=640: 540 基準でまだ FirstOnMs(500) 内 -> A 押下維持(OnMs=50 サイクルなら既にOFF)
        XFireEngine::SetTestClockNowMs(640.0);
        XINPUT_STATE s6 = MakeState(XINPUT_GAMEPAD_A, 0, 0);
        XFireEngine::Apply(0, &s6);
        Check((s6.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0, "T42d: t=640 still first ON (re-anchored to 540)");

        XFireEngine::SetMasterEnabled(true);
        Config::SetForTest(cfg);
    }

    // 後続テストのためにメイン設定を復元(Config テストが g_cfg を書き換えたため)
    Config::SetIniPathForTest(nullptr);
    Config::SetForTest(cfg);
    XFireEngine::SetMasterEnabled(true);

    printf("\n%d checks, %d failed\n", g_checks, g_failures);
    if (g_failures == 0) { printf("ALL TESTS PASSED\n"); return 0; }
    return 1;
}
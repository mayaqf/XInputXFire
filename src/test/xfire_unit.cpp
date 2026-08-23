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

    // T30e: bool 不明値は既定(EnableLT 既定=true)
    {
        std::wstring path = tempIniPath(L"xfire_t30e.ini");
        WritePrivateProfileStringW(L"XFire", L"EnableLT", L"maybe", path.c_str());
        const XFireConfig& c = loadFromIni(path);
        Check(c.enableLT == true, "T30e: EnableLT=maybe (unknown) -> default true");
        DeleteFileW(path.c_str());
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

    // 後続テストのためにメイン設定を復元(Config テストが g_cfg を書き換えたため)
    Config::SetIniPathForTest(nullptr);
    Config::SetForTest(cfg);
    XFireEngine::SetMasterEnabled(true);

    printf("\n%d checks, %d failed\n", g_checks, g_failures);
    if (g_failures == 0) { printf("ALL TESTS PASSED\n"); return 0; }
    return 1;
}
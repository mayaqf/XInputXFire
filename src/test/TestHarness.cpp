// TestHarness.cpp - プロキシDLL実機テストベンチ
// プロキシDLL(xinput1_3.dll)と ini(XInputXFire.ini)を本 exe と同フォルダに配置して実行。
// 実コントローラ(ViGEm Xbox360 等)を polling し、プロキシ経由のトグル/連射/音声を
// 某14 起動なしで検証する。プロキシ側の Apply(コンボ検出+Announcer)がそのまま動くので、
// LB+A 押下で音声アナウンスも再生される。
//
// 使い方:
//   test_harness.exe            : 対話モード(Esc/Ctrl-C で終了)
//   test_harness.exe 30         : 30秒で終了
//   test_harness.exe --csv      : CSV 出力モード(従来)
//   test_harness.exe XInput9_1_0.dll : ロードDLL名変更(9.1.0 経路テスト)
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <conio.h>   // _kbhit/_getch
#include <vector>

// XINPUT_STATE とレイアウト互換(自然境界・16 bytes)
struct StateDump {
    DWORD  packet;
    WORD   buttons;
    BYTE   lt;
    BYTE   rt;
    SHORT  tlx, tly, trx, tr_;
};
typedef DWORD (WINAPI *FnGetState)(DWORD, StateDump*);

// XInput ボタンビット
static const WORD B_DU=0x0001, B_DD=0x0002, B_DL=0x0004, B_DR=0x0008;
static const WORD B_START=0x0010, B_BACK=0x0020, B_LS=0x0040, B_RS=0x0080;
static const WORD B_LB=0x0100, B_RB=0x0200;
static const WORD B_A=0x1000, B_B=0x2000, B_X=0x4000, B_Y=0x8000;
static const WORD B_TARGETS = B_DU|B_DD|B_DL|B_DR|B_A|B_B|B_X|B_Y;

static void ButtonNames(WORD b, char* out, size_t n) {
    out[0] = 0;
    char* p = out;
    auto add = [&](const char* s) {
        size_t len = strlen(s);
        if ((size_t)(p - out) + len + 2 < n) { if (p != out) *p++=' '; memcpy(p, s, len); p += len; *p = 0; }
    };
    if (b & B_DU) add("DU"); if (b & B_DD) add("DD"); if (b & B_DL) add("DL"); if (b & B_DR) add("DR");
    if (b & B_START) add("START"); if (b & B_BACK) add("BACK");
    if (b & B_LS) add("LS"); if (b & B_RS) add("RS");
    if (b & B_LB) add("LB"); if (b & B_RB) add("RB");
    if (b & B_A) add("A"); if (b & B_B) add("B"); if (b & B_X) add("X"); if (b & B_Y) add("Y");
    if (out[0] == 0) { strncpy(out, "-", n-1); out[n-1]=0; }
}

int main(int argc, char** argv) {
    // コンソール出力を UTF-8 に設定(ソースは UTF-8 保存・文字化け防止)。
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    int seconds = 0;            // 0 = 無制限(Esc で終了)
    bool csv = false;
    const wchar_t* dllName = L"xinput1_3.dll";
    wchar_t argw[64] = {0};
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--csv") == 0) { csv = true; continue; }
        if (argv[i][0] >= '0' && argv[i][0] <= '9') { seconds = atoi(argv[i]); continue; }
        MultiByteToWideChar(CP_ACP, 0, argv[i], -1, argw, 63);
        if (argw[0]) dllName = argw;
    }

    HMODULE h = LoadLibraryW(dllName);
    if (!h) { fprintf(stderr, "LoadLibrary %ls failed: %lu\n", dllName, GetLastError()); return 1; }
    FnGetState GetState = (FnGetState)GetProcAddress(h, "XInputGetState");
    if (!GetState) { fprintf(stderr, "XInputGetState not found in DLL\n"); return 2; }

    if (csv) {
        printf("time_ms,packet,buttons,lt,rt,A,B,X,Y,DU,DD,DL,DR\n");
        DWORD start = GetTickCount();
        while ((seconds == 0 || (int)(GetTickCount() - start) < seconds * 1000)) {
            StateDump s = {}; DWORD hr = GetState(0, &s); DWORD now = GetTickCount() - start;
            if (hr == ERROR_SUCCESS)
                printf("%lu,%lu,0x%04X,%u,%u,%d,%d,%d,%d,%d,%d,%d,%d\n", now, s.packet, s.buttons, s.lt, s.rt,
                    (s.buttons&B_A)?1:0,(s.buttons&B_B)?1:0,(s.buttons&B_X)?1:0,(s.buttons&B_Y)?1:0,
                    (s.buttons&B_DU)?1:0,(s.buttons&B_DD)?1:0,(s.buttons&B_DL)?1:0,(s.buttons&B_DR)?1:0);
            fflush(stdout); Sleep(16);
        }
        return 0;
    }

    // --- 対話モード ---
    printf("=== XInputXFire 実機テストベンチ ===\n");
    printf("プロキシ: %ls  / ini: XInputXFire.ini (同フォルダ)\n", dllName);
    printf("操作:\n");
    printf("  LB+A 同時押し -> 連射 ON/OFF 切替(音声アナウンス)\n");
    printf("  LT/RT 押しながら A/B/X/Y/十字 -> 連射(50ms周期)\n");
    printf("表示: 対象ボタンの変化毎に1行(dt=前回からの間隔ms・連射は約50msで規則的に並ぶ)\n");
    printf("終了: Esc または Ctrl-C%s\n\n", seconds ? " または指定秒数経過" : "");

    DWORD start = GetTickCount();
    WORD prevButtons = 0;
    WORD prevCombo = 0;
    const WORD COMBO = B_LB | B_A;
    DWORD prevToggleAt = 0;
    std::vector<DWORD> toggleTimes; // 対象ビット変化時刻(直近1秒・連射判定用)
    DWORD lastSummary = 0;
    int connState = -1; // -1=未確定, 0=未接続, 1=接続(起動時の未接続も1行出すため三状態)

    while (true) {
        if (_kbhit()) { int c = _getch(); if (c == 27) break; }   // Esc
        DWORD now = GetTickCount() - start;
        if (seconds && (int)now >= seconds * 1000) break;

        StateDump s = {};
        DWORD hr = GetState(0, &s);

        if (hr != ERROR_SUCCESS) {
            if (connState != 0) { printf("[%5.1fs] コントローラが見つかりません (hr=%lu)\n", now/1000.0, hr); connState = 0; }
            Sleep(200); continue;
        }
        if (connState != 1) { printf("[%5.1fs] コントローラ接続検出\n", now/1000.0); connState = 1; }

        // 連射対象ビットの変化を毎フレーム検出 → 1行出力。
        // 連射(50ms周期)は dt≈50ms で規則的に並ぶ、手動操作は dt が数百ms。
        WORD prevTarget = prevButtons & B_TARGETS;
        WORD curTarget  = s.buttons & B_TARGETS;
        if (curTarget != prevTarget) {
            DWORD dt = prevToggleAt ? (now - prevToggleAt) : 0;
            char bns[64]; ButtonNames(curTarget ^ prevTarget, bns, sizeof(bns));
            printf("[%6.1fs] %s  %-10s  dt=%lums\n", now/1000.0,
                curTarget ? "ON " : "off", bns, (unsigned long)dt);
            toggleTimes.push_back(now);
            prevToggleAt = now;
        }
        prevButtons = s.buttons;

        // 直近1秒以外のトグル時刻を破棄
        while (!toggleTimes.empty() && now - toggleTimes.front() > 1000)
            toggleTimes.erase(toggleTimes.begin());

        // コンボ(LB+A)立ち上がり検出 → イベント行
        WORD curCombo = s.buttons & COMBO;
        if (curCombo == COMBO && prevCombo != COMBO) {
            printf("[%6.1fs] >>> COMBO LB+A 押下 -> 連射トグル(音声アナウンス)\n", now/1000.0);
        }
        prevCombo = curCombo;

        // 1秒サマリ(LT/RT + 連射判定: 直近1秒の対象トグル回数 >= 6 で連射)
        if (now - lastSummary >= 1000) {
            bool trig = (s.lt >= 128 || s.rt >= 128);
            int tc = (int)toggleTimes.size();
            bool xfire = trig && tc >= 6;
            char bns[64]; ButtonNames(s.buttons, bns, sizeof(bns));
            printf("[%6.1fs] === LT=%3u RT=%3u | %-14s | トグル/秒=%-2d %s ===\n",
                now/1000.0, s.lt, s.rt, bns, tc, xfire ? "XFIRE" : "idle");
            lastSummary = now;
        }
        Sleep(16);
    }
    printf("\n終了しました。\n");
    return 0;
}
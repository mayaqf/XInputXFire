// XFireEngine.cpp - 連射タイミング制御コア
// 連射マスタートグル(既定OFF)の上に、LT/RT 押下中のみ連射対象ボタン(DPAD/ABXY)を
// QPC ベース周期で ON/OFF 反転。トリガ値(bLeftTrigger/bRightTrigger)は触らない。
// 4コントローラ独立。コンボキー(LB+A)立ち上がりでマスターON/OFF切替 + SAPI 音声。
#include "XFireEngine.h"
#include "Config.h"
#include "Announcer.h"
#include "DiagLog.h"
#include <atomic>

namespace {
struct ControllerState {
    bool   active       = false; // トリガ押下中(連射発動中)
    bool   phaseOn      = false; // true=ON区間(対象押), false=OFF区間(対象離)
    bool   started      = false; // フェーズ開始済み(次回押下でONから始めるためのリセット用)
    bool   firstPhase   = false; // 最初のON区間中(FirstOnMs を適用中)。対象ボタン離でリセット
    double phaseStartMs = 0.0;  // 現在フェーズ開始時刻(ms・QPC換算)
    WORD   prevOutputButtons = 0; // 前回プロキシ出力 wButtons(dwPacketNumber 更新判定用)
    WORD   prevInputButtons  = 0; // 前回物理入力 wButtons(トグルコンボ立ち上がり判定用)
};

double          g_qpcToMs = 0.0;
SRWLOCK         g_initLock = SRWLOCK_INIT;
std::atomic<int> g_inited{0};
ControllerState g_ctrl[4];
SRWLOCK         g_ctrlLock[4] = { SRWLOCK_INIT, SRWLOCK_INIT, SRWLOCK_INIT, SRWLOCK_INIT };
// 連射マスター有効状態(起動時は Config::defaultEnabled で初期化)。コンボキーで切替。
std::atomic<bool> g_xfireEnabled{false};

#ifdef XFIRE_TEST
double g_testNowMs = 0.0;
bool   g_testClockEnabled = false;
#endif

inline double NowMs() {
#ifdef XFIRE_TEST
    if (g_testClockEnabled) return g_testNowMs;
#endif
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart * g_qpcToMs;
}

// 最初のON区間の実効長(ms)。FirstOnMs=0(無効)のときは OnMs と同値。
inline DWORD FirstEffOnMs(const XFireConfig& cfg) {
    return cfg.firstOnMs > 0 ? cfg.firstOnMs : cfg.onMs;
}
}

void XFireEngine::InitOnce() {
    if (g_inited.load(std::memory_order_acquire)) return;
    AcquireSRWLockExclusive(&g_initLock);
    if (!g_inited.load(std::memory_order_relaxed)) {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        g_qpcToMs = (f.QuadPart > 0) ? 1000.0 / (double)f.QuadPart : 0.0;
        if (g_qpcToMs <= 0.0) DiagLog::Log("[XFIRE] QueryPerformanceFrequency returned 0 -> xfire disabled (passthrough only)");
        // 起動時のマスター状態を Config から設定(Config::LoadOnce は InitOnce 前に呼ばれる)。
        g_xfireEnabled.store(Config::Get().defaultEnabled, std::memory_order_release);
        g_inited.store(1, std::memory_order_release); // g_qpcToMs 書き込み完了を可視化
    }
    ReleaseSRWLockExclusive(&g_initLock);
}

void XFireEngine::Apply(DWORD idx, XINPUT_STATE* p) {
    if (!p || idx >= 4 || g_qpcToMs <= 0.0) return;
    const XFireConfig& cfg = Config::Get();
    XINPUT_GAMEPAD& g = p->Gamepad;

    ControllerState& s = g_ctrl[idx];
    AcquireSRWLockExclusive(&g_ctrlLock[idx]);

    // --- 連射マスタートグル: コンボキー(LB+A)立ち上がりで ON/OFF 切替 ---
    // マスター状態に関わらず常時検出(ON→OFF も OFF→ON も同操作)。
    WORD phys = g.wButtons; // 本物DLL が入れた物理状態(改変前)
    WORD tmask = cfg.toggleButtons;
    if (tmask && (phys & tmask) == tmask && (s.prevInputButtons & tmask) != tmask) {
        // コンボ全ビットが揃った立ち上がりエッジ → トグル(アトミック RMW: 複数コントローラ
        // 同時コンボでも失われないよう compare_exchange で反転)。
        bool cur = g_xfireEnabled.load(std::memory_order_relaxed);
        bool next;
        do { next = !cur; }
        while (!g_xfireEnabled.compare_exchange_weak(cur, next, std::memory_order_acq_rel, std::memory_order_relaxed));
        bool en = next;
        if (!en) { s.active = false; s.started = false; s.firstPhase = false; } // OFF時はフェーズ状態クリア
        s.prevInputButtons = phys;
        ReleaseSRWLockExclusive(&g_ctrlLock[idx]);
        if (cfg.announceEnabled) {
            Announcer::Speak(en ? L"Enabled cross fire." : L"Disabled cross fire.");
        }
        return; // トグル押下フレームは連射せず素通し(コンボ押下自体が連射発動しないよう分離)
    }
    s.prevInputButtons = phys;

    // --- マスターOFF なら連射せず素通し ---
    if (!g_xfireEnabled.load(std::memory_order_acquire)) {
        ReleaseSRWLockExclusive(&g_ctrlLock[idx]);
        return;
    }

    // 有効なトリガの最大値(LT/RT いずれか押下で発動)
    BYTE trig = 0;
    if (cfg.enableRT && g.bRightTrigger > trig) trig = g.bRightTrigger;
    if (cfg.enableLT && g.bLeftTrigger  > trig) trig = g.bLeftTrigger;

    // ヒステリシス: 押下判定は高閾値、離上判定は低閾値
    bool pressed = s.active ? (trig >= cfg.hysteresisLow)
                            : (trig >= cfg.triggerThreshold);
    if (!pressed) {
        s.active = false;
        s.started = false; // 次回押下時にON区間から開始
        s.firstPhase = false;
        ReleaseSRWLockExclusive(&g_ctrlLock[idx]);
        return; // 連射対象は物理状態のまま(本物DLLが pState に物理状態を入れている)
    }
    s.active = true;

    // 連射対象のうち物理的に押されているビット(毎回本物DLLから新鮮な物理状態で再計算)
    WORD heldButtons = g.wButtons & cfg.targetButtons;

    double now = NowMs();
    // 位相クロックは「対象ボタン押下」基準。対象をすべて離したらリセットし、
    // 次回押下で最初のON区間(FirstOnMs)から再スタート(連射ON中でも1回だけ押すを可能にする)。
    if (heldButtons == 0) {
        s.started = false;     // 対象なし → 位相リセット(下の forcing は heldButtons=0 で no-op)
        s.firstPhase = false;
    } else if (!s.started) {
        // 対象ボタン押下開始 → 最初のON区間(FirstOnMs)から
        s.phaseOn = true;
        s.firstPhase = true;
        s.phaseStartMs = now;
        s.started = true;
    } else {
        // while で長時間未呼出後も追いつかせる(ドリフト防止・位相保持)。
        // FirstEffOnMs は FirstOnMs>0 ならそれ、そうでなければ OnMs。OnMs/OffMs は
        // [Config::kOnMsMin, Config::kOnMsMax] クランプ済みなので dur>=1 で安全。
        double dur = (s.phaseOn && s.firstPhase) ? (double)FirstEffOnMs(cfg)
                    : s.phaseOn ? (double)cfg.onMs
                                : (double)cfg.offMs;
        while (now - s.phaseStartMs >= dur) {
            if (s.phaseOn && s.firstPhase) s.firstPhase = false; // 最初のON区間終了
            s.phaseOn = !s.phaseOn;
            s.phaseStartMs += dur;
            dur = (s.phaseOn && s.firstPhase) ? (double)FirstEffOnMs(cfg)
                  : s.phaseOn ? (double)cfg.onMs
                              : (double)cfg.offMs;
        }
    }

    if (s.phaseOn) {
        g.wButtons |= heldButtons;  // ON区間: 対象の押されているものを押下
    } else {
        g.wButtons &= static_cast<WORD>(~heldButtons); // OFF区間: 対象を離す
    }
    // トリガ値は触らない → ゲームへそのまま伝達

    // wButtons が前回出力と異なれば dwPacketNumber を進める。
    // dwPacketNumber の差分で入力変化を判定するゲームで、連射の ON/OFF トグルを検知させるため。
    if (g.wButtons != s.prevOutputButtons) {
        p->dwPacketNumber = p->dwPacketNumber + 1;
    }
    s.prevOutputButtons = g.wButtons;

    ReleaseSRWLockExclusive(&g_ctrlLock[idx]);
}

#ifdef XFIRE_TEST
void XFireEngine::SetTestClockNowMs(double nowMs) { g_testNowMs = nowMs; g_testClockEnabled = true; }
void XFireEngine::ResetControllerState() {
    for (int i = 0; i < 4; ++i) {
        AcquireSRWLockExclusive(&g_ctrlLock[i]);
        g_ctrl[i] = ControllerState();
        ReleaseSRWLockExclusive(&g_ctrlLock[i]);
    }
}
void XFireEngine::SetMasterEnabled(bool enabled) {
    g_xfireEnabled.store(enabled, std::memory_order_release);
}
#endif
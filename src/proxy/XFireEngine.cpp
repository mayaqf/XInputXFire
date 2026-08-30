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
    // --- ボタン単独連射(トリガ不要)の第2位相クロック。トリガモードとは独立 ---
    bool   bPhaseOn     = false; // true=ON区間, false=OFF区間
    bool   bStarted     = false; // フェーズ開始済み(リセット用)
    bool   bFirstPhase  = false; // 最初のON区間中(buttonFirstOnMs 適用中)
    double bPhaseStartMs = 0.0; // 現在フェーズ開始時刻(ms・QPC換算)
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
// ボタン単独連射の最初のON区間の実効長(ms)。buttonFirstOnMs=0 のときは buttonOnMs と同値。
inline DWORD ButtonFirstEffOnMs(const XFireConfig& cfg) {
    return cfg.buttonFirstOnMs > 0 ? cfg.buttonFirstOnMs : cfg.buttonOnMs;
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
        if (!en) {
            // OFF時は両モードのフェーズ状態をクリア(再ON時に各モード FirstOnMs から再開)。
            s.active = false; s.started = false; s.firstPhase = false;
            s.bStarted = false; s.bFirstPhase = false;
        }
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

    // 有効なトリガの最大値(LT/RT いずれか押下で発動)。トリガモードの活性判定に用いる。
    BYTE trig = 0;
    if (cfg.enableRT && g.bRightTrigger > trig) trig = g.bRightTrigger;
    if (cfg.enableLT && g.bLeftTrigger  > trig) trig = g.bLeftTrigger;

    // ヒステリシス: 押下判定は高閾値、離上判定は低閾値
    bool trigActive = s.active ? (trig >= cfg.hysteresisLow)
                               : (trig >= cfg.triggerThreshold);
    s.active = trigActive;
    // ※トリガ非押下でも早期returnしない: ボタン単独連射はトリガ状態に関わらず動くため。

    // 所有権(重複解決ルール: トリガ優先)
    //   triggerHeld : トリガ連射対象のうち物理押下ビット
    //   buttonHeld  : ボタン単独連射対象のうち物理押下ビット
    //   triggerOwned: トリガモードが駆動するビット(トリガ活性時のみ・重複含む)
    //   buttonOwned : ボタン単独モードが駆動するビット(triggerOwned と排他)
    // 両リストに入っているボタンは、トリガ活性時はトリガモードが、非活性時はボタン単独モードが駆動。
    // トリガリストのみのボタンはトリガ活性時のみ連射。ボタン単独リストのみのボタンは常時連射。
    WORD triggerHeld  = g.wButtons & cfg.targetButtons;
    WORD buttonHeld   = g.wButtons & cfg.buttonTargetButtons;
    WORD triggerOwned = trigActive ? triggerHeld : 0;
    WORD buttonOwned  = buttonHeld & static_cast<WORD>(~triggerOwned);

    double now = NowMs();

    // 位相クロック共通処理: 対象ボタン押下基準。対象をすべて離したらリセットし、
    // 次回押下で最初のON区間(firstEffMs)から再スタート(連射ON中でも1回だけ押すを可能にする)。
    // while で長時間未呼出後も追いつかせる(ドリフト防止・位相保持)。dur はクランプ済みで >=1。
    auto runClock = [&now](bool& phaseOn, bool& started, bool& firstPhase, double& phaseStartMs,
                           WORD owned, double firstEffMs, double onMs, double offMs) {
        if (owned == 0) {
            started = false;     // 対象なし → 位相リセット(下の forcing は owned=0 で no-op)
            firstPhase = false;
            return;
        }
        if (!started) {
            // 対象ボタン押下開始 → 最初のON区間(firstEffMs)から
            phaseOn = true;
            firstPhase = true;
            phaseStartMs = now;
            started = true;
            return;
        }
        double dur = (phaseOn && firstPhase) ? firstEffMs
                    : phaseOn ? onMs
                              : offMs;
        while (now - phaseStartMs >= dur) {
            if (phaseOn && firstPhase) firstPhase = false; // 最初のON区間終了
            phaseOn = !phaseOn;
            phaseStartMs += dur;
            dur = (phaseOn && firstPhase) ? firstEffMs
                  : phaseOn ? onMs
                            : offMs;
        }
    };

    // トリガモードの位相クロック(トリガ非活性時は triggerOwned=0 でリセットされる)
    runClock(s.phaseOn, s.started, s.firstPhase, s.phaseStartMs,
             triggerOwned, (double)FirstEffOnMs(cfg), (double)cfg.onMs, (double)cfg.offMs);
    // ボタン単独モードの位相クロック(トリガとは独立・triggerOwned と排他ビットで駆動)
    runClock(s.bPhaseOn, s.bStarted, s.bFirstPhase, s.bPhaseStartMs,
             buttonOwned, (double)ButtonFirstEffOnMs(cfg), (double)cfg.buttonOnMs, (double)cfg.buttonOffMs);

    // 適用: 両モードの駆動ビットは排他なので衝突しない。非所有ビットは物理状態を素通り。
    //   ON区間: 対象を押下 / OFF区間: 対象を離す。
    if (s.phaseOn)  g.wButtons |= triggerOwned;
    else            g.wButtons &= static_cast<WORD>(~triggerOwned);
    if (s.bPhaseOn) g.wButtons |= buttonOwned;
    else            g.wButtons &= static_cast<WORD>(~buttonOwned);
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